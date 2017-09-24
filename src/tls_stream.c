/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "tls_stream.h"
#include <emscripten.h>

#include "git2/errors.h"

#include "openssl_stream.h"
#include "stransport_stream.h"

static git_stream_cb tls_ctor;

int git_stream_register_tls(git_stream_cb ctor)
{
	tls_ctor = ctor;

	return 0;
}

// --------- This could be separated in its own file, but for now just replacing the TLS stuff with a browser XMLHttpRequest
git_stream xhrstream;

int emscripten_connect(git_stream *stream) {
	printf("Connecting\n");	
	EM_ASM(
		window.gitxhrdata = null;
	);
	return 1;
}

ssize_t emscripten_read(git_stream *stream, void *data, size_t len) {
	size_t ret = 0;
	
	unsigned int readyState = 0;
	EM_ASM_({		
		if(window.gitxhrdata!==null) {
			console.log("sending post data",window.gitxhrdata.length);
			window.gitxhr.send(window.gitxhrdata.buffer);			
			window.gitxhrdata = null;
		} 
		setValue($0,window.gitxhr.readyState,"i32");
	},&readyState);
	
	while(readyState!=4) {
		EM_ASM_({
			console.log("Waiting for data");
			setValue($0,window.gitxhr.readyState,"i32");
		},&readyState);
		
		emscripten_sleep(10);
	}
	
	EM_ASM_({
		if(window.gitxhr) {
			var arrayBuffer = window.gitxhr.response; // Note: not oReq.responseText
					
			if (window.gitxhr.readyState===4 && arrayBuffer) {		
				var availlen = (arrayBuffer.byteLength-window.gitxhrreadoffset);						
				var len = availlen > $2 ? $2 : availlen;
								
				var byteArray = new Uint8Array(arrayBuffer,window.gitxhrreadoffset,len);		
				//console.log("read from ",arrayBuffer.byteLength,window.gitxhrreadoffset,len,byteArray[0]);
				writeArrayToMemory(byteArray,$0);
				setValue($1,len,"i32");
				
				window.gitxhrreadoffset+=len;				
			}
		} else {
			setValue($1,-1,"i32");
		}
	},data,&ret,len);	
	//printf("Returning %d bytes, requested %d\n",ret,len);
	return ret;
}

int emscripten_certificate(git_cert **out, git_stream *stream) {
	printf("Checking certificate\n");
	return 0;
}

ssize_t emscripten_write(git_stream *stream, const char *data, size_t len, int flags) {
	EM_ASM_({
		var data = Pointer_stringify($0);
		
		if(data.indexOf("GET ")===0) {				
			window.gitxhr=new XMLHttpRequest();
			window.gitxhrreadoffset = 0;
			window.gitxhr.responseType = "arraybuffer";			
			window.gitxhr.open("GET",data.split("\n")[0].split(" ")[1]);		
			window.gitxhr.send();
		} else if(data.indexOf("POST ")===0) {
			window.gitxhr=new XMLHttpRequest();
			window.gitxhrreadoffset = 0;
			window.gitxhr.responseType = "arraybuffer";			
			var requestlines = data.split("\n");			
			window.gitxhr.open("POST",requestlines[0].split(" ")[1]);
			
			console.log(data);
			window.gitxhrdata = null;								
			for(var n=1;n<requestlines.length;n++) {
				if(requestlines[n].indexOf("Content-Type")===0) {
					window.gitxhr.setRequestHeader("Content-Type",requestlines[n].split(": ")[1].trim());
				}	
			}			
		} else {
			if(window.gitxhrdata===null) {				
				console.log("New post data",$1,data);
				window.gitxhrdata = new Uint8Array($1);
				window.gitxhrdata.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),0);				
			} else {
				var appended = new Uint8Array(window.gitxhrdata.length+$1);
				appended.set(window.gitxhrdata,0);
				appended.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),window.gitxhrdata.length);
				window.gitxhrdata = appended;										
				console.log("Appended post data",$1,window.gitxhrdata.length,data);
			}
		}
	},data,len);
	
	return len;
}

int emscripten_close(git_stream *stream) {
	printf("Close\n");
	return 0;
}

void emscripten_free(git_stream *stream) {
	printf("Free\n");
	//git__free(stream);
}

int git_open_emscripten_stream(git_stream **out, const char *host, const char *port) {		
	xhrstream.version = GIT_STREAM_VERSION;
	xhrstream.connect = emscripten_connect;
	xhrstream.read = emscripten_read;
	xhrstream.write = emscripten_write;
	xhrstream.close = emscripten_close;
	xhrstream.free = emscripten_free;
	xhrstream.certificate = emscripten_certificate;
	xhrstream.encrypted = 1;
	xhrstream.proxy_support = 0;
		
	*out = &xhrstream;
	printf("Stream setup \n");
	return 0;
}

// --------- END Emscripten specifics

int git_tls_stream_new(git_stream **out, const char *host, const char *port)
{

	if (tls_ctor)
		return tls_ctor(out, host, port);

#ifdef GIT_SECURE_TRANSPORT
	return git_stransport_stream_new(out, host, port);
#elif defined(GIT_OPENSSL)
	return git_openssl_stream_new(out, host, port);
#else
	// And here we just add the emscripten stream (could be done outside instead)
	return git_open_emscripten_stream(out, host, port);
	/*GIT_UNUSED(out);
	GIT_UNUSED(host);
	GIT_UNUSED(port);

	giterr_set(GITERR_SSL, "there is no TLS stream available");
	return -1;*/
#endif
}
