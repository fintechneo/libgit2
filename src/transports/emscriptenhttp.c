/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "common.h"

#ifdef __EMSCRIPTEN__

#include "git2.h"
#include "http_parser.h"
#include "buffer.h"
#include "netops.h"
#include "global.h"
#include "remote.h"
#include "smart.h"
#include "auth.h"
#include "http.h"
#include "auth_negotiate.h"
#include "streams/tls.h"
#include "streams/socket.h"

git_http_auth_scheme auth_schemes[] = {
	{ GIT_AUTHTYPE_NEGOTIATE, "Negotiate", GIT_CREDTYPE_DEFAULT, git_http_auth_negotiate },
	{ GIT_AUTHTYPE_BASIC, "Basic", GIT_CREDTYPE_USERPASS_PLAINTEXT, git_http_auth_basic },
};

static const char *upload_pack_service = "upload-pack";
static const char *upload_pack_ls_service_url = "/info/refs?service=git-upload-pack";
static const char *upload_pack_service_url = "/git-upload-pack";
static const char *receive_pack_service = "receive-pack";
static const char *receive_pack_ls_service_url = "/info/refs?service=git-receive-pack";
static const char *receive_pack_service_url = "/git-receive-pack";
static const char *get_verb = "GET";
static const char *post_verb = "POST";

#define AUTH_HEADER_SERVER "Authorization"
#define AUTH_HEADER_PROXY  "Proxy-Authorization"

#define SERVER_TYPE_REMOTE "remote"
#define SERVER_TYPE_PROXY  "proxy"

#define OWNING_SUBTRANSPORT(s) ((http_subtransport *)(s)->parent.subtransport)

#define PARSE_ERROR_GENERIC	-1
#define PARSE_ERROR_REPLAY	-2
/** Look at the user field */
#define PARSE_ERROR_EXT         -3

#define CHUNK_SIZE	4096

enum last_cb {
	NONE,
	FIELD,
	VALUE
};

typedef struct {
	git_smart_subtransport_stream parent;
	const char *service;
	const char *service_url;
	char *redirect_url;
	const char *verb;
	char *chunk_buffer;
	unsigned chunk_buffer_len;
	unsigned sent_request : 1,
		received_response : 1,
		chunked : 1;
} http_stream;

typedef struct {
	git_net_url url;
	git_stream *stream;

	git_cred *cred;
	git_cred *url_cred;

	git_vector auth_challenges;
	git_vector auth_contexts;
} http_server;

typedef struct {
	git_smart_subtransport parent;
	transport_smart *owner;
	git_stream *gitserver_stream;
	bool connected;

	http_server server;

	http_server proxy;
	char *proxy_url;
	git_proxy_options proxy_opts;

	/* Parser structures */
	http_parser parser;
	http_parser_settings settings;
	gitno_buffer parse_buffer;
	git_buf parse_header_name;
	git_buf parse_header_value;
	char parse_buffer_data[NETIO_BUFSIZE];
	char *content_type;
	char *content_length;
	char *location;
	enum last_cb last_cb;
	int parse_error;
	int error;
	unsigned parse_finished : 1,
	    replay_count : 3;
} http_subtransport;

typedef struct {
	http_stream *s;
	http_subtransport *t;

	/* Target buffer details from read() */
	char *buffer;
	size_t buf_size;
	size_t *bytes_read;
} parser_context;

static bool credtype_match(git_http_auth_scheme *scheme, void *data)
{
	unsigned int credtype = *(unsigned int *)data;

	return !!(scheme->credtypes & credtype);
}

static bool challenge_match(git_http_auth_scheme *scheme, void *data)
{
	const char *scheme_name = scheme->name;
	const char *challenge = (const char *)data;
	size_t scheme_len;

	scheme_len = strlen(scheme_name);
	return (strncasecmp(challenge, scheme_name, scheme_len) == 0 &&
		(challenge[scheme_len] == '\0' || challenge[scheme_len] == ' '));
}

static int auth_context_match(
	git_http_auth_context **out,
	http_server *server,
	bool (*scheme_match)(git_http_auth_scheme *scheme, void *data),
	void *data)
{
	git_http_auth_scheme *scheme = NULL;
	git_http_auth_context *context = NULL, *c;
	size_t i;

	*out = NULL;

	for (i = 0; i < ARRAY_SIZE(auth_schemes); i++) {
		if (scheme_match(&auth_schemes[i], data)) {
			scheme = &auth_schemes[i];
			break;
		}
	}

	if (!scheme)
		return 0;

	/* See if authentication has already started for this scheme */
	git_vector_foreach(&server->auth_contexts, i, c) {
		if (c->type == scheme->type) {
			context = c;
			break;
		}
	}

	if (!context) {
		if (scheme->init_context(&context, &server->url) < 0)
			return -1;
		else if (!context)
			return 0;
		else if (git_vector_insert(&server->auth_contexts, context) < 0)
			return -1;
	}

	*out = context;

	return 0;
}

static int gen_request(
	git_buf *buf,
	http_stream *s,
	size_t content_length)
{
	http_subtransport *t = OWNING_SUBTRANSPORT(s);
	const char *path = t->server.url.path ? t->server.url.path : "/";
	size_t i;

	git_buf_printf(buf, "%s %s%s HTTP/1.1\r\n",
		s->verb, path, s->service_url);

	git_buf_puts(buf, "User-Agent: ");
	git_http__user_agent(buf);
	git_buf_puts(buf, "\r\n");
	git_buf_printf(buf, "Host: %s", t->server.url.host);
	
	git_buf_puts(buf, "\r\n");

	if (s->chunked || content_length > 0) {
		git_buf_printf(buf, "Accept: application/x-git-%s-result\r\n", s->service);
		git_buf_printf(buf, "Content-Type: application/x-git-%s-request\r\n", s->service);

		if (s->chunked)
			git_buf_puts(buf, "Transfer-Encoding: chunked\r\n");
		else
			git_buf_printf(buf, "Content-Length: %"PRIuZ "\r\n", content_length);
	} else
		git_buf_puts(buf, "Accept: */*\r\n");

	for (i = 0; i < t->owner->custom_headers.count; i++) {
		if (t->owner->custom_headers.strings[i])
			git_buf_printf(buf, "%s\r\n", t->owner->custom_headers.strings[i]);
	}

	git_buf_puts(buf, "\r\n");

	if (git_buf_oom(buf))
		return -1;

	return 0;
}

static int parse_authenticate_response(
	http_server *server,
	int *allowed_types)
{
	git_http_auth_context *context;
	char *challenge;
	size_t i;

	git_vector_foreach(&server->auth_challenges, i, challenge) {
		if (auth_context_match(&context, server,
		    challenge_match, challenge) < 0)
			return -1;
		else if (!context)
			continue;

		if (context->set_challenge &&
			context->set_challenge(context, challenge) < 0)
			return -1;

		*allowed_types |= context->credtypes;
	}

	return 0;
}

static int on_header_ready(http_subtransport *t)
{
	git_buf *name = &t->parse_header_name;
	git_buf *value = &t->parse_header_value;

	if (!strcasecmp("Content-Type", git_buf_cstr(name))) {
		if (t->content_type) {
			git_error_set(GIT_ERROR_NET, "multiple Content-Type headers");
			return -1;
		}

		t->content_type = git__strdup(git_buf_cstr(value));
		GIT_ERROR_CHECK_ALLOC(t->content_type);
	}
	else if (!strcasecmp("Content-Length", git_buf_cstr(name))) {
		if (t->content_length) {
			git_error_set(GIT_ERROR_NET, "multiple Content-Length headers");
			return -1;
		}

		t->content_length = git__strdup(git_buf_cstr(value));
		GIT_ERROR_CHECK_ALLOC(t->content_length);
	}
	else if (!strcasecmp("Proxy-Authenticate", git_buf_cstr(name))) {
		char *dup = git__strdup(git_buf_cstr(value));
		GIT_ERROR_CHECK_ALLOC(dup);

		if (git_vector_insert(&t->proxy.auth_challenges, dup) < 0)
			return -1;
	}
	else if (!strcasecmp("WWW-Authenticate", git_buf_cstr(name))) {
		char *dup = git__strdup(git_buf_cstr(value));
		GIT_ERROR_CHECK_ALLOC(dup);

		if (git_vector_insert(&t->server.auth_challenges, dup) < 0)
			return -1;
	}
	else if (!strcasecmp("Location", git_buf_cstr(name))) {
		if (t->location) {
			git_error_set(GIT_ERROR_NET, "multiple Location headers");
			return -1;
		}

		t->location = git__strdup(git_buf_cstr(value));
		GIT_ERROR_CHECK_ALLOC(t->location);
	}

	return 0;
}

static int on_header_field(http_parser *parser, const char *str, size_t len)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;

	/* Both parse_header_name and parse_header_value are populated
	 * and ready for consumption */
	if (VALUE == t->last_cb)
		if (on_header_ready(t) < 0)
			return t->parse_error = PARSE_ERROR_GENERIC;

	if (NONE == t->last_cb || VALUE == t->last_cb)
		git_buf_clear(&t->parse_header_name);

	if (git_buf_put(&t->parse_header_name, str, len) < 0)
		return t->parse_error = PARSE_ERROR_GENERIC;

	t->last_cb = FIELD;
	return 0;
}

static int on_header_value(http_parser *parser, const char *str, size_t len)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;

	assert(NONE != t->last_cb);

	if (FIELD == t->last_cb)
		git_buf_clear(&t->parse_header_value);

	if (git_buf_put(&t->parse_header_value, str, len) < 0)
		return t->parse_error = PARSE_ERROR_GENERIC;

	t->last_cb = VALUE;
	return 0;
}

GIT_INLINE(void) free_cred(git_cred **cred)
{
	if (*cred) {
		git_cred_free(*cred);
		(*cred) = NULL;
	}
}

static int on_auth_required(
	git_cred **creds,
	http_parser *parser,
	const char *url,
	const char *type,
	git_cred_acquire_cb callback,
	void *callback_payload,
	const char *username,
	int allowed_types)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;
	int ret;

	if (!allowed_types) {
		git_error_set(GIT_ERROR_NET, "%s requested authentication but did not negotiate mechanisms", type);
		t->parse_error = PARSE_ERROR_GENERIC;
		return t->parse_error;
	}

	if (callback) {
		free_cred(creds);
		ret = callback(creds, url, username, allowed_types, callback_payload);

		if (ret == GIT_PASSTHROUGH) {
			/* treat GIT_PASSTHROUGH as if callback isn't set */
		} else if (ret < 0) {
			t->error = ret;
			t->parse_error = PARSE_ERROR_EXT;
			return t->parse_error;
		} else {
			assert(*creds);

			if (!((*creds)->credtype & allowed_types)) {
				git_error_set(GIT_ERROR_NET, "%s credential provider returned an invalid cred type", type);
				t->parse_error = PARSE_ERROR_GENERIC;
				return t->parse_error;
			}

			/* Successfully acquired a credential. */
			t->parse_error = PARSE_ERROR_REPLAY;
			return 0;
		}
	}

	git_error_set(GIT_ERROR_NET, "%s authentication required but no callback set",
		type);
	t->parse_error = PARSE_ERROR_GENERIC;
	return t->parse_error;
}

static int on_headers_complete(http_parser *parser)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;
	http_stream *s = ctx->s;
	git_buf buf = GIT_BUF_INIT;

	/* Both parse_header_name and parse_header_value are populated
	 * and ready for consumption. */
	if (t->last_cb == VALUE && on_header_ready(t) < 0)
		return t->parse_error = PARSE_ERROR_GENERIC;

	/* Check for a 200 HTTP status code. */
	if (parser->status_code != 200) {
		git_error_set(GIT_ERROR_NET,
			"unexpected HTTP status code: %d",
			parser->status_code);
		return t->parse_error = PARSE_ERROR_GENERIC;
	}

	/* The response must contain a Content-Type header. */
	if (!t->content_type) {
		git_error_set(GIT_ERROR_NET, "no Content-Type header in response");
		return t->parse_error = PARSE_ERROR_GENERIC;
	}

	/* The Content-Type header must match our expectation. */
	if (get_verb == s->verb)
		git_buf_printf(&buf,
			"application/x-git-%s-advertisement",
			ctx->s->service);
	else
		git_buf_printf(&buf,
			"application/x-git-%s-result",
			ctx->s->service);

	if (git_buf_oom(&buf))
		return t->parse_error = PARSE_ERROR_GENERIC;

	if (strcmp(t->content_type, git_buf_cstr(&buf))) {
		git_buf_dispose(&buf);
		git_error_set(GIT_ERROR_NET,
			"invalid Content-Type: %s",
			t->content_type);
		return t->parse_error = PARSE_ERROR_GENERIC;
	}

	git_buf_dispose(&buf);

	return 0;
}

static int on_message_complete(http_parser *parser)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;

	t->parse_finished = 1;

	return 0;
}

static int on_body_fill_buffer(http_parser *parser, const char *str, size_t len)
{
	parser_context *ctx = (parser_context *) parser->data;
	http_subtransport *t = ctx->t;

	/* If our goal is to replay the request (either an auth failure or
	 * a redirect) then don't bother buffering since we're ignoring the
	 * content anyway.
	 */
	if (t->parse_error == PARSE_ERROR_REPLAY)
		return 0;

	/* If there's no buffer set, we're explicitly ignoring the body. */
	if (ctx->buffer) {
		if (ctx->buf_size < len) {
			git_error_set(GIT_ERROR_NET, "can't fit data in the buffer");
			return t->parse_error = PARSE_ERROR_GENERIC;
		}

		memcpy(ctx->buffer, str, len);
		ctx->buffer += len;
		ctx->buf_size -= len;
	}

	*(ctx->bytes_read) += len;

	return 0;
}

static void clear_parser_state(http_subtransport *t)
{
	http_parser_init(&t->parser, HTTP_RESPONSE);
	gitno_buffer_setup_fromstream(t->server.stream,
		&t->parse_buffer,
		t->parse_buffer_data,
		sizeof(t->parse_buffer_data));

	t->last_cb = NONE;
	t->parse_error = 0;
	t->parse_finished = 0;

	git_buf_dispose(&t->parse_header_name);
	git_buf_init(&t->parse_header_name, 0);

	git_buf_dispose(&t->parse_header_value);
	git_buf_init(&t->parse_header_value, 0);

	git__free(t->content_type);
	t->content_type = NULL;

	git__free(t->content_length);
	t->content_length = NULL;

	git__free(t->location);
	t->location = NULL;

	git_vector_free_deep(&t->proxy.auth_challenges);
	git_vector_free_deep(&t->server.auth_challenges);
}

static int write_chunk(git_stream *io, const char *buffer, size_t len)
{	
	/* Chunk body */
	if (len > 0 && git_stream__write_full(io, buffer, len, 0) < 0)
		return -1;


	return 0;
}

static int http_connect(http_subtransport *t)
{
	git_stream *stream = NULL;
	git_net_url *url;
	int error;

	if (t->connected &&
		http_should_keep_alive(&t->parser) &&
		t->parse_finished)
		return 0;

	if (t->server.stream) {
		git_stream_close(t->server.stream);
		git_stream_free(t->server.stream);
		t->server.stream = NULL;
	}

	t->connected = 0;

	url = &t->server.url;	
	error = git_tls_stream_new(&stream,  url->host, url->port);
	
	if (error < 0)
		goto on_error;

	if ((error = git_stream_connect(stream)))
		goto on_error;

	
	t->server.stream = stream;
	t->connected = 1;
	t->replay_count = 0;
	return 0;

on_error:
	if (stream) {
		git_stream_close(stream);
		git_stream_free(stream);
	}

	return error;
}

static int http_stream_read(
	git_smart_subtransport_stream *stream,
	char *buffer,
	size_t buf_size,
	size_t *bytes_read)
{
	http_stream *s = (http_stream *)stream;
	http_subtransport *t = OWNING_SUBTRANSPORT(s);
	
	*bytes_read = 0;

	assert(t->connected);

	if (!s->sent_request) {
		git_buf request = GIT_BUF_INIT;

		clear_parser_state(t);

		if (gen_request(&request, s, 0) < 0)
			return -1;

		if (git_stream__write_full(t->server.stream, request.ptr,
					   request.size, 0) < 0) {
			git_buf_dispose(&request);
			return -1;
		}

		git_buf_dispose(&request);

		s->sent_request = 1;
	}

	if (!s->received_response) {
		if (s->chunked) {
			assert(s->verb == post_verb);

			/* Flush, if necessary */
			if (s->chunk_buffer_len > 0 &&
				write_chunk(t->server.stream,
				    s->chunk_buffer, s->chunk_buffer_len) < 0)
				return -1;

			s->chunk_buffer_len = 0;

			/* Write the final chunk. */
			/*
			
			// Removed. Works with regular git backend, but not with Eclipse JGit which expects
			// EOF here and complains if there are more data
			
			if (git_stream__write_full(t->server.stream,
						   "0\r\n\r\n", 5, 0) < 0)
				return -1;
			*/
		}

		s->received_response = 1;
	}
			
	// When using emscripten we simply bypass the http parser since we use the one built into the browser
	*(bytes_read)  = git_stream_read(t->server.stream, buffer, buf_size);	
	
	return 0;
}

static int http_stream_write_chunked(
	git_smart_subtransport_stream *stream,
	const char *buffer,
	size_t len)
{
	http_stream *s = (http_stream *)stream;
	http_subtransport *t = OWNING_SUBTRANSPORT(s);

	assert(t->connected);

	/* Send the request, if necessary */
	if (!s->sent_request) {
		git_buf request = GIT_BUF_INIT;

		clear_parser_state(t);

		if (gen_request(&request, s, 0) < 0)
			return -1;

		if (git_stream__write_full(t->server.stream, request.ptr,
					   request.size, 0) < 0) {
			git_buf_dispose(&request);
			return -1;
		}

		git_buf_dispose(&request);

		s->sent_request = 1;
	}

	if (len > CHUNK_SIZE) {
		/* Flush, if necessary */
		if (s->chunk_buffer_len > 0) {
			if (write_chunk(t->server.stream,
			    s->chunk_buffer, s->chunk_buffer_len) < 0)
				return -1;

			s->chunk_buffer_len = 0;
		}

		/* Write chunk directly */
		if (write_chunk(t->server.stream, buffer, len) < 0)
			return -1;
	}
	else {
		/* Append as much to the buffer as we can */
		int count = min(CHUNK_SIZE - s->chunk_buffer_len, len);

		if (!s->chunk_buffer)
			s->chunk_buffer = git__malloc(CHUNK_SIZE);

		memcpy(s->chunk_buffer + s->chunk_buffer_len, buffer, count);
		s->chunk_buffer_len += count;
		buffer += count;
		len -= count;

		/* Is the buffer full? If so, then flush */
		if (CHUNK_SIZE == s->chunk_buffer_len) {
			if (write_chunk(t->server.stream,
			    s->chunk_buffer, s->chunk_buffer_len) < 0)
				return -1;

			s->chunk_buffer_len = 0;

			if (len > 0) {
				memcpy(s->chunk_buffer, buffer, len);
				s->chunk_buffer_len = len;
			}
		}
	}
	
	return 0;
}

static int http_stream_write_single(
	git_smart_subtransport_stream *stream,
	const char *buffer,
	size_t len)
{
	http_stream *s = (http_stream *)stream;
	http_subtransport *t = OWNING_SUBTRANSPORT(s);
	git_buf request = GIT_BUF_INIT;

	assert(t->connected);

	if (s->sent_request) {
		git_error_set(GIT_ERROR_NET, "subtransport configured for only one write");
		return -1;
	}

	clear_parser_state(t);

	if (gen_request(&request, s, len) < 0)
		return -1;

	if (git_stream__write_full(t->server.stream, request.ptr, request.size, 0) < 0)
		goto on_error;

	if (len && git_stream__write_full(t->server.stream, buffer, len, 0) < 0)
		goto on_error;

	git_buf_dispose(&request);
	s->sent_request = 1;

	return 0;

on_error:
	git_buf_dispose(&request);
	return -1;
}

static void http_stream_free(git_smart_subtransport_stream *stream)
{
	http_stream *s = (http_stream *)stream;

	if (s->chunk_buffer)
		git__free(s->chunk_buffer);

	if (s->redirect_url)
		git__free(s->redirect_url);

	git__free(s);
}

static int http_stream_alloc(http_subtransport *t,
	git_smart_subtransport_stream **stream)
{
	http_stream *s;

	if (!stream)
		return -1;

	s = git__calloc(sizeof(http_stream), 1);
	GIT_ERROR_CHECK_ALLOC(s);

	s->parent.subtransport = &t->parent;
	s->parent.read = http_stream_read;
	s->parent.write = http_stream_write_single;
	s->parent.free = http_stream_free;

	*stream = (git_smart_subtransport_stream *)s;
	return 0;
}

static int http_uploadpack_ls(
	http_subtransport *t,
	git_smart_subtransport_stream **stream)
{
	http_stream *s;

	if (http_stream_alloc(t, stream) < 0)
		return -1;

	s = (http_stream *)*stream;

	s->service = upload_pack_service;
	s->service_url = upload_pack_ls_service_url;
	s->verb = get_verb;

	return 0;
}

static int http_uploadpack(
	http_subtransport *t,
	git_smart_subtransport_stream **stream)
{
	http_stream *s;

	if (http_stream_alloc(t, stream) < 0)
		return -1;

	s = (http_stream *)*stream;

	s->service = upload_pack_service;
	s->service_url = upload_pack_service_url;
	s->verb = post_verb;

	return 0;
}

static int http_receivepack_ls(
	http_subtransport *t,
	git_smart_subtransport_stream **stream)
{
	http_stream *s;

	if (http_stream_alloc(t, stream) < 0)
		return -1;

	s = (http_stream *)*stream;

	s->service = receive_pack_service;
	s->service_url = receive_pack_ls_service_url;
	s->verb = get_verb;

	return 0;
}

static int http_receivepack(
	http_subtransport *t,
	git_smart_subtransport_stream **stream)
{
	http_stream *s;

	if (http_stream_alloc(t, stream) < 0)
		return -1;

	s = (http_stream *)*stream;

	/* Use Transfer-Encoding: chunked for this request */
	s->chunked = 1;
	s->parent.write = http_stream_write_chunked;

	s->service = receive_pack_service;
	s->service_url = receive_pack_service_url;
	s->verb = post_verb;

	return 0;
}

static int http_action(
	git_smart_subtransport_stream **stream,
	git_smart_subtransport *subtransport,
	const char *url,
	git_smart_service_t action)
{
	http_subtransport *t = (http_subtransport *)subtransport;
	int ret;

	assert(stream);

	/*
	 * If we've seen a redirect then preserve the location that we've
	 * been given.  This is important to continue authorization against
	 * the redirect target, not the user-given source; the endpoint may
	 * have redirected us from HTTP->HTTPS and is using an auth mechanism
	 * that would be insecure in plaintext (eg, HTTP Basic).
	 */
	if ((!t->server.url.host || !t->server.url.port || !t->server.url.path) &&
	    (ret = git_net_url_parse(&t->server.url, url)) < 0)
		return ret;

	assert(t->server.url.host && t->server.url.port && t->server.url.path);

	if ((ret = http_connect(t)) < 0)
		return ret;

	switch (action) {
	case GIT_SERVICE_UPLOADPACK_LS:
		return http_uploadpack_ls(t, stream);

	case GIT_SERVICE_UPLOADPACK:
		return http_uploadpack(t, stream);

	case GIT_SERVICE_RECEIVEPACK_LS:
		return http_receivepack_ls(t, stream);

	case GIT_SERVICE_RECEIVEPACK:
		return http_receivepack(t, stream);
	}

	*stream = NULL;
	return -1;
}

static void free_auth_contexts(git_vector *contexts)
{
	git_http_auth_context *context;
	size_t i;

	git_vector_foreach(contexts, i, context) {
		if (context->free)
			context->free(context);
	}

	git_vector_clear(contexts);
}

static int http_close(git_smart_subtransport *subtransport)
{
	http_subtransport *t = (http_subtransport *) subtransport;

	clear_parser_state(t);

	t->connected = 0;

	if (t->server.stream) {
		git_stream_close(t->server.stream);
		git_stream_free(t->server.stream);
		t->server.stream = NULL;
	}

	if (t->proxy.stream) {
		git_stream_close(t->proxy.stream);
		git_stream_free(t->proxy.stream);
		t->proxy.stream = NULL;
	}

	free_cred(&t->server.cred);
	free_cred(&t->server.url_cred);
	free_cred(&t->proxy.cred);
	free_cred(&t->proxy.url_cred);

	free_auth_contexts(&t->server.auth_contexts);
	free_auth_contexts(&t->proxy.auth_contexts);

	git_net_url_dispose(&t->server.url);
	git_net_url_dispose(&t->proxy.url);

	git__free(t->proxy_url);
	t->proxy_url = NULL;

	return 0;
}

static void http_free(git_smart_subtransport *subtransport)
{
	http_subtransport *t = (http_subtransport *) subtransport;

	http_close(subtransport);

	git_vector_free(&t->server.auth_contexts);
	git_vector_free(&t->proxy.auth_contexts);
	git__free(t);
}

int git_smart_subtransport_http(git_smart_subtransport **out, git_transport *owner, void *param)
{
	http_subtransport *t;

	GIT_UNUSED(param);

	if (!out)
		return -1;

	t = git__calloc(sizeof(http_subtransport), 1);
	GIT_ERROR_CHECK_ALLOC(t);

	t->owner = (transport_smart *)owner;
	t->parent.action = http_action;
	t->parent.close = http_close;
	t->parent.free = http_free;

	t->settings.on_header_field = on_header_field;
	t->settings.on_header_value = on_header_value;
	t->settings.on_headers_complete = on_headers_complete;
	t->settings.on_body = on_body_fill_buffer;
	t->settings.on_message_complete = on_message_complete;

	*out = (git_smart_subtransport *) t;
	return 0;
}

#endif /* !GIT_WINHTTP */
