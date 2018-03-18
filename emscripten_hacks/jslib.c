#include "emscripten.h"
#include <stdio.h>
#include "streams/stransport.h"
#include "streams/tls.h"
#include "../examples/network/common.h"
#include "git2.h"
#include "git2/clone.h"
#include "git2/merge.h"

static git_repository *repo = NULL;
git_stream xhrstream;

typedef struct progress_data {
	git_transfer_progress fetch_progress;
	size_t completed_steps;
	size_t total_steps;
	const char *path;
} progress_data;


int emscripten_connect(git_stream *stream) {
	printf("Connecting\n");	
	EM_ASM(
		self.gitxhrdata = null;
	);
	return 1;
}

ssize_t emscripten_read(git_stream *stream, void *data, size_t len) {
	size_t ret = 0;
	
	unsigned int readyState = 0;
	EM_ASM_({		
		if(self.gitxhrdata!==null) {
			console.log("sending post data",self.gitxhrdata.length);
			self.gitxhr.send(self.gitxhrdata.buffer);			
			self.gitxhrdata = null;
		} 
		setValue($0,self.gitxhr.readyState,"i32");
	},&readyState);
	
	/*
	 * We skip this since we are now using a synchronous request
	while(readyState!=4) {
		EM_ASM_({
			console.log("Waiting for data");
			setValue($0,self.gitxhr.readyState,"i32");
		},&readyState);
		
		emscripten_sleep(10);
	}*/
	
	EM_ASM_({
		if(self.gitxhr) {
			var arrayBuffer = self.gitxhr.response; // Note: not oReq.responseText
					
			if (self.gitxhr.readyState===4 && arrayBuffer) {		
				var availlen = (arrayBuffer.byteLength-self.gitxhrreadoffset);						
				var len = availlen > $2 ? $2 : availlen;
								
				var byteArray = new Uint8Array(arrayBuffer,self.gitxhrreadoffset,len);		
				//console.log("read from ",arrayBuffer.byteLength,self.gitxhrreadoffset,len,byteArray[0]);
				writeArrayToMemory(byteArray,$0);
				setValue($1,len,"i32");
				
				self.gitxhrreadoffset+=len;				
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
			self.gitxhr=new XMLHttpRequest();
			self.gitxhrreadoffset = 0;
			self.gitxhr.responseType = "arraybuffer";			
			self.gitxhr.open("GET",data.split("\n")[0].split(" ")[1], false);		
			self.gitxhr.send();
		} else if(data.indexOf("POST ")===0) {
			self.gitxhr=new XMLHttpRequest();
			self.gitxhrreadoffset = 0;
			self.gitxhr.responseType = "arraybuffer";			
			var requestlines = data.split("\n");			
			self.gitxhr.open("POST",requestlines[0].split(" ")[1], false);
			
			console.log(data);
			self.gitxhrdata = null;								
			for(var n=1;n<requestlines.length;n++) {
				if(requestlines[n].indexOf("Content-Type")===0) {
					self.gitxhr.setRequestHeader("Content-Type",requestlines[n].split(": ")[1].trim());
				}	
			}			
		} else {
			if(self.gitxhrdata===null) {				
				console.log("New post data",$1,data);
				self.gitxhrdata = new Uint8Array($1);
				self.gitxhrdata.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),0);				
			} else {
				var appended = new Uint8Array(self.gitxhrdata.length+$1);
				appended.set(self.gitxhrdata,0);
				appended.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),self.gitxhrdata.length);
				self.gitxhrdata = appended;										
				console.log("Appended post data",$1,self.gitxhrdata.length,data);
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

static void print_progress(const progress_data *pd)
{
	int network_percent = pd->fetch_progress.total_objects > 0 ?
		(100*pd->fetch_progress.received_objects) / pd->fetch_progress.total_objects :
		0;
	int index_percent = pd->fetch_progress.total_objects > 0 ?
		(100*pd->fetch_progress.indexed_objects) / pd->fetch_progress.total_objects :
		0;

	int checkout_percent = pd->total_steps > 0
		? (100 * pd->completed_steps) / pd->total_steps
		: 0;
	int kbytes = pd->fetch_progress.received_bytes / 1024;

	if (pd->fetch_progress.total_objects &&
		pd->fetch_progress.received_objects == pd->fetch_progress.total_objects) {
		printf("Resolving deltas %d/%d\n",
		       pd->fetch_progress.indexed_deltas,
		       pd->fetch_progress.total_deltas);
	} else {
		printf("net %3d%% (%4d kb, %5d/%5d)  /  idx %3d%% (%5d/%5d)  /  chk %3d%% (%4" PRIuZ "/%4" PRIuZ ") %s\n",
		   network_percent, kbytes,
		   pd->fetch_progress.received_objects, pd->fetch_progress.total_objects,
		   index_percent, pd->fetch_progress.indexed_objects, pd->fetch_progress.total_objects,
		   checkout_percent,
		   pd->completed_steps, pd->total_steps,
		   pd->path);
	}
}

static int sideband_progress(const char *str, int len, void *payload)
{
	(void)payload; // unused

	printf("remote: %.*s\n", len, str);
	fflush(stdout);
	return 0;
}

static int fetch_progress(const git_transfer_progress *stats, void *payload)
{
	progress_data *pd = (progress_data*)payload;
	pd->fetch_progress = *stats;
	print_progress(pd);
	return 0;
}
static void checkout_progress(const char *path, size_t cur, size_t tot, void *payload)
{
	progress_data *pd = (progress_data*)payload;
	pd->completed_steps = cur;
	pd->total_steps = tot;
	pd->path = path;
	print_progress(pd);
}

int cred_acquire_cb(git_cred **out,
	const char * url,
	const char * username_from_url,
	unsigned int allowed_types,
	void * payload)
{	
	int error;

	error = git_cred_userpass_plaintext_new(out, "username", "password");

	return error;
}

int cloneremote(const char * url,const char * path)
{
	progress_data pd = {{0}};

	git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
		
	int error;
	
	// Set up options
	checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
	checkout_opts.progress_cb = checkout_progress;
	checkout_opts.progress_payload = &pd;
	clone_opts.checkout_opts = checkout_opts;
	clone_opts.fetch_opts.callbacks.sideband_progress = sideband_progress;
	clone_opts.fetch_opts.callbacks.transfer_progress = &fetch_progress;
	clone_opts.fetch_opts.callbacks.credentials = cred_acquire_cb;
	clone_opts.fetch_opts.callbacks.payload = &pd;

	// Do the clone
	error = git_clone(&repo, url, path, &clone_opts);
	printf("\n");
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}	
	return error;
}

/**
 * This function gets called for each remote-tracking branch that gets
 * updated. The message we output depends on whether it's a new one or
 * an update.
 */
static int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data)
{
	printf("Update cb %s\n",refname);
	char a_str[GIT_OID_HEXSZ+1], b_str[GIT_OID_HEXSZ+1];
	 (void)data;
 
	 git_oid_fmt(b_str, b);
	 b_str[GIT_OID_HEXSZ] = '\0';
 
	 if (git_oid_iszero(a)) {
		 printf("[new]     %.20s %s\n", b_str, refname);
	 } else {
		 git_oid_fmt(a_str, a);
		 a_str[GIT_OID_HEXSZ] = '\0';
		 printf("[updated] %.10s..%.10s %s\n", a_str, b_str, refname);
	 }
 
	 return 0;
}
 
static int progress_cb(const char *str, int len, void *data)
{
	(void)data;
	printf("remote: %.*s", len, str);
	fflush(stdout); /* We don't have the \n to force the flush */
	return 0;
}

/**
 * This gets called during the download and indexing. Here we show
 * processed and total objects in the pack and the amount of received
 * data. Most frontends will probably want to show a percentage and
 * the download rate.
 */
static int transfer_progress_cb(const git_transfer_progress *stats, void *payload)
{
	(void)payload;

	if (stats->received_objects == stats->total_objects) {
		printf("Resolving deltas %d/%d\n",
			stats->indexed_deltas, stats->total_deltas);
	} else if (stats->total_objects > 0) {
		printf("Received %d/%d objects (%d) in %" PRIuZ " bytes\n",
			stats->received_objects, stats->total_objects,
			stats->indexed_objects, stats->received_bytes);
	}
	return 0;
}

void printLastError() {
	const git_error *err = giterr_last();
	if (err) printf("ERROR %d: %s\n", err->klass, err->message);	
}

void jsgitinit() {
	git_stream_register_tls(git_open_emscripten_stream);
	git_libgit2_init();	
	printf("libgit2 for javascript initialized\n");
}

void jsgitopenrepo() {
	git_repository_open(&repo, ".");
}

void jsgitclone(char * url, char * localdir) {			
	cloneremote(url,localdir);	
	EM_ASM(
		if(self.jsgitonmethodfinish) {
			self.jsgitonmethodfinish();
		}
	);		
}

void jsgitadd(const char * path) {	
	git_index *index;	
	git_repository_index(&index, repo);	
	git_index_add_bypath(index, path);
	git_index_write(index);
	git_index_free(index);
}

void jsgitcommit(char * comment,char * name, char * email, long time, int offset) {
	git_oid commit_oid,tree_oid,oid_parent_commit;
	git_commit *parent_commit;
	git_tree *tree;
	git_index *index;	
	git_object *parent = NULL;
	git_reference *ref = NULL;	
	
	git_revparse_ext(&parent, &ref, repo, "HEAD");
	git_repository_index(&index, repo);	
	git_index_write_tree(&tree_oid, index);
	git_index_write(index);
	git_index_free(index);

	int error = git_tree_lookup(&tree, repo, &tree_oid);
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}

	
	git_signature *signature;	
	git_signature_new(&signature, name, email, time, offset);
	
	error = git_commit_create_v(
		&commit_oid,
		repo,
		"HEAD",
		signature,
		signature,
		NULL,
		comment,
		tree,
		parent ? 1 : 0, parent);
		
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}
	git_signature_free(signature);
	git_tree_free(tree);	
}

int jsgitrepositorystate() {
	return git_repository_state(repo);
}

void jsgitprintlatestcommit()
{
	int rc;
	git_commit * commit = NULL; /* the result */
	git_oid oid_parent_commit;  /* the SHA1 for last commit */

	/* resolve HEAD into a SHA1 */
	rc = git_reference_name_to_id( &oid_parent_commit, repo, "HEAD" );
	if ( rc == 0 )
	{
		/* get the actual commit structure */
		rc = git_commit_lookup( &commit, repo, &oid_parent_commit );
		if ( rc == 0 )
		{
			printf("%s\n",git_commit_message(commit));
			git_commit_free(commit);      
		}
	}	
}

void jsgitshutdown() {
	git_libgit2_shutdown();
}

int fetchead_foreach_cb(const char *ref_name,
	const char *remote_url,
	const git_oid *oid,
	unsigned int is_merge,
	void *payload)
{	  
	if(is_merge) {
		git_annotated_commit * fetchhead_annotated_commit;					

		git_annotated_commit_lookup(&fetchhead_annotated_commit,
			repo,
			oid
		);			
					
		git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;		
		merge_opts.file_flags = 
			(GIT_MERGE_FILE_STYLE_DIFF3 | GIT_MERGE_FILE_DIFF_MINIMAL);

		git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
		checkout_opts.checkout_strategy = (
			GIT_CHECKOUT_SAFE |
			GIT_CHECKOUT_ALLOW_CONFLICTS |
			GIT_CHECKOUT_CONFLICT_STYLE_DIFF3
		);
		
		const git_annotated_commit *mergeheads[] = 
			{fetchhead_annotated_commit};
		git_merge(repo,mergeheads,
			1,&merge_opts,
			&checkout_opts);		
				
		git_merge_analysis_t analysis;
		git_merge_preference_t preference = GIT_MERGE_PREFERENCE_NONE;
		git_merge_analysis(&analysis,
				&preference,
				repo,
				mergeheads
				,1);
		
		git_annotated_commit_free(fetchhead_annotated_commit);		

		if(analysis==GIT_MERGE_ANALYSIS_NORMAL) {		
			printf("Normal merge\n");
			git_signature * signature;
			git_signature_default(&signature,repo);
						
			git_oid commit_oid,oid_parent_commit,tree_oid;
			git_commit * parent_commit;
			git_commit * commit = NULL; /* the result */
			git_commit * fetchhead_commit;

			git_commit_lookup(&fetchhead_commit,
				repo,
				oid
			);						
									
			git_reference_name_to_id( &oid_parent_commit, repo, "HEAD" );			
			git_commit_lookup( &parent_commit, repo, &oid_parent_commit );
			
			git_tree *tree;
			git_index *index;	
			
			git_repository_index(&index, repo);
			if(git_index_has_conflicts(index)) {
				printf("Index has conflicts\n");				
			} else {
				git_index_write_tree(&tree_oid, index);
				git_tree_lookup(&tree, repo, &tree_oid);
				
				git_commit_create_v(
					&commit_oid,
					repo,
					"HEAD",
					signature,
					signature,
					NULL,
					"Merge with remote",
					tree,
					2, 
					parent_commit, 
					fetchhead_commit
				);
				git_repository_state_cleanup(repo);
			}
			
			git_index_free(index);					
			git_commit_free(parent_commit);
			git_commit_free(fetchhead_commit);			
		} else if(analysis==(GIT_MERGE_ANALYSIS_NORMAL | GIT_MERGE_ANALYSIS_FASTFORWARD)) {
			printf("Fast forward\n");
			git_reference * ref = NULL;		

			git_reference_lookup(&ref, repo, "refs/heads/master");
			git_reference *newref;
			git_reference_set_target(&newref,ref,oid,"pull");

			git_reference_free(newref);
			git_reference_free(ref);

			git_repository_state_cleanup(repo);
		} else if(analysis==GIT_MERGE_ANALYSIS_UP_TO_DATE) {
			printf("All up to date\n");
			git_repository_state_cleanup(repo);
		} else {
			printf("Don't know how to merge\n");
		}
								
		
		
		printf("Merged %s\n",remote_url);
	}	
	return 0;
}	

void jsgitpull() {
	git_remote *remote = NULL;
	const git_transfer_progress *stats;
	git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
	
	if (git_remote_lookup(&remote, repo, "origin") < 0)
		goto on_error;

	/* Set up the callbacks (only update_tips for now) */
	fetch_opts.callbacks.update_tips = &update_cb;
	fetch_opts.callbacks.sideband_progress = &progress_cb;
	fetch_opts.callbacks.transfer_progress = transfer_progress_cb;
	fetch_opts.callbacks.credentials = cred_acquire_cb;

	/**
	 * Perform the fetch with the configured refspecs from the
	 * config. Update the reflog for the updated references with
	 * "fetch".
	 */	 
	if (git_remote_fetch(remote, NULL, &fetch_opts, "fetch") < 0)
		goto on_error;

	/**
	 * If there are local objects (we got a thin pack), then tell
	 * the user how many objects we saved from having to cross the
	 * network.
	 */
	stats = git_remote_stats(remote);
	if (stats->local_objects > 0) {
		printf("\nReceived %d/%d objects in %" PRIuZ " bytes (used %d local objects)\n",
		       stats->indexed_objects, stats->total_objects, stats->received_bytes, stats->local_objects);
	} else{
		printf("\nReceived %d/%d objects in %" PRIuZ "bytes\n",
			stats->indexed_objects, stats->total_objects, stats->received_bytes);
	}

	printf("Fetch done\n");
	
	
	git_repository_fetchhead_foreach(repo,&fetchead_foreach_cb,NULL);
	
	//git_repository_state_cleanup(repo);

	//printf("Pull done\n");
	EM_ASM(
		if(self.jsgitonmethodfinish) {
			self.jsgitonmethodfinish();
		}
	);	
	return;

on_error:
	printLastError();

	git_remote_free(remote);
	EM_ASM(
		if(self.jsgitonmethodfinish) {
			self.jsgitonmethodfinish();
		}
	);
	return;
}

int diff_file_cb(const git_diff_delta *delta, float progress, void *payload) {
	printf("Adding %s\n",delta->old_file.path);
	jsgitadd(delta->old_file.path);
	return 0;
}

void jsgitaddfileswithchanges() {
	git_diff *diff;
	
	git_diff_index_to_workdir(&diff, repo, NULL, NULL);
	git_diff_foreach(diff,&diff_file_cb,NULL,NULL,NULL,NULL);

	git_diff_free(diff);
}

int jsgitworkdirnumberofdeltas() {
	git_diff *diff;
	
	git_diff_index_to_workdir(&diff, repo, NULL, NULL);
	int ret = git_diff_num_deltas(diff);
	git_diff_free(diff);
	return ret;
}

void jsgitpush() {
	// get the remote.
	int error;

	git_remote* remote = NULL;
	git_remote_lookup( &remote, repo, "origin" );

	// connect to remote
	//git_remote_connect( remote, GIT_DIRECTION_PUSH , NULL, NULL, NULL);

	// add a push refspec
	git_remote_add_push(repo,"origin", "refs/heads/master:refs/heads/master" );
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}

	// configure options
	git_push_options options;
	git_push_init_options( &options, GIT_PUSH_OPTIONS_VERSION );

	// do the push
	printf("Do the push\n");
	error = git_remote_upload( remote, NULL, &options );
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}
	printf("Push done\n");
	EM_ASM(
		if(self.jsgitonmethodfinish) {
			self.jsgitonmethodfinish();
		}
	);	
}
