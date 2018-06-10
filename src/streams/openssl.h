/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */
#ifndef INCLUDE_streams_openssl_h__
#define INCLUDE_streams_openssl_h__

#include "common.h"

#include "git2/sys/stream.h"

extern int git_openssl_stream_global_init(void);

extern int git_openssl_stream_new(git_stream **out, const char *host, const char *port);

extern int git_openssl__set_cert_location(const char *file, const char *path);

#endif
