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
		gitxhrdata = null;
	);
	return 1;
}

ssize_t emscripten_read(git_stream *stream, void *data, size_t len) {
	size_t ret = 0;
	
	unsigned int readyState = 0;
	EM_ASM_({		
		if(gitxhrdata!==null) {
			console.log("sending post data",gitxhrdata.length);
			gitxhr.send(gitxhrdata.buffer);			
			gitxhrdata = null;
		} 
		setValue($0,gitxhr.readyState,"i32");
	},&readyState);
	
	/*
	 * We skip this since we are now using a synchronous request
	while(readyState!=4) {
		EM_ASM_({
			console.log("Waiting for data");
			setValue($0,gitxhr.readyState,"i32");
		},&readyState);
		
		emscripten_sleep(10);
	}*/
	
	EM_ASM_({
		if(gitxhr) {
			var arrayBuffer = gitxhr.response; // Note: not oReq.responseText
					
			if (gitxhr.readyState===4 && arrayBuffer) {		
				var availlen = (arrayBuffer.byteLength-gitxhrreadoffset);						
				var len = availlen > $2 ? $2 : availlen;
								
				var byteArray = new Uint8Array(arrayBuffer,gitxhrreadoffset,len);		
				//console.log("read from ",arrayBuffer.byteLength,gitxhrreadoffset,len,byteArray[0]);
				writeArrayToMemory(byteArray,$0);
				setValue($1,len,"i32");
				
				gitxhrreadoffset+=len;				
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
			gitxhr=new XMLHttpRequest();
			gitxhrreadoffset = 0;
			gitxhr.responseType = "arraybuffer";			
			gitxhr.open("GET",data.split("\n")[0].split(" ")[1], false);		
			gitxhr.send();
		} else if(data.indexOf("POST ")===0) {
			gitxhr=new XMLHttpRequest();
			gitxhrreadoffset = 0;
			gitxhr.responseType = "arraybuffer";			
			var requestlines = data.split("\n");			
			gitxhr.open("POST",requestlines[0].split(" ")[1], false);
			
			console.log(data);
			gitxhrdata = null;								
			for(var n=1;n<requestlines.length;n++) {
				if(requestlines[n].indexOf("Content-Type")===0) {
					gitxhr.setRequestHeader("Content-Type",requestlines[n].split(": ")[1].trim());
				}	
			}			
		} else {
			if(gitxhrdata===null) {				
				console.log("New post data",$1,data);
				gitxhrdata = new Uint8Array($1);
				gitxhrdata.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),0);				
			} else {
				var appended = new Uint8Array(gitxhrdata.length+$1);
				appended.set(gitxhrdata,0);
				appended.set(new Uint8Array(Module.HEAPU8.buffer,$0,$1),gitxhrdata.length);
				gitxhrdata = appended;										
				console.log("Appended post data",$1,gitxhrdata.length,data);
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

/**
 * Initialize repository in current directory
 */
void jsgitinitrepo(unsigned int bare) {
	git_repository_init(&repo, ".", bare);
}

/**
 * Open repository in current directory
 */
void jsgitopenrepo() {
	git_repository_open(&repo, ".");
}

void jsgitclone(char * url, char * localdir) {			
	cloneremote(url,localdir);		
}

void jsgitadd(const char * path) {	
	git_index *index;	
	git_repository_index(&index, repo);	
	git_index_add_bypath(index, path);
	git_index_write(index);
	git_index_free(index);
}

void jsgitremove(const char * path) {	
	git_index *index;	
	git_repository_index(&index, repo);	
	git_index_remove_bypath(index, path);
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
	git_repository_free(repo);
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
			1,
			&merge_opts,
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

				git_index_conflict_iterator *conflicts;
				const git_index_entry *ancestor;
				const git_index_entry *our;
				const git_index_entry *their;
				int err = 0;

				git_index_conflict_iterator_new(&conflicts, index);

				while ((err = git_index_conflict_next(&ancestor, &our, &their, conflicts)) == 0) {
					fprintf(stderr, "conflict: a:%s o:%s t:%s\n",
							ancestor ? ancestor->path : "NULL",
							our->path ? our->path : "NULL",
							their->path ? their->path : "NULL");
				}

				if (err != GIT_ITEROVER) {
					fprintf(stderr, "error iterating conflicts\n");
				}

				git_index_conflict_iterator_free(conflicts);
				
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

void jsgitresolvemergecommit() {	
	git_index *index;
	git_repository_index(&index, repo);	
	
	git_oid commit_oid, tree_oid, oid_parent_commit, oid_fetchhead_commit;
	git_tree *tree;
	
	git_signature * signature;
	git_signature_default(&signature,repo);
			
	git_commit * parent_commit;	
	git_commit * fetchhead_commit;
	
	printf("%d %d\n", parent_commit, fetchhead_commit);
	git_reference_name_to_id( &oid_parent_commit, repo, "HEAD" );
	git_commit_lookup( &parent_commit, repo, &oid_parent_commit );

	git_reference_name_to_id( &oid_fetchhead_commit, repo, "FETCH_HEAD" );			
	git_commit_lookup( &fetchhead_commit, repo, &oid_fetchhead_commit );	
		
	
	git_index_write_tree(&tree_oid, index);
	git_tree_lookup(&tree, repo, &tree_oid);

	git_commit_create_v(
		&commit_oid,
		repo,
		"HEAD",
		signature,
		signature,
		NULL,
		"Resolved conflicts and merge with remote",
		tree,
		2, 
		parent_commit,
		fetchhead_commit
	);	

	git_repository_state_cleanup(repo);
	git_signature_free(signature);
	git_index_free(index);		
	git_tree_free(tree);			
	git_commit_free(parent_commit);
	git_commit_free(fetchhead_commit);			
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

	printf("Pull done\n");
		
	return;

on_error:
	printLastError();

	git_remote_free(remote);	
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

int jsgitstatus() {
	git_status_list *status;
	git_status_options statusopt = GIT_STATUS_OPTIONS_INIT;
	EM_ASM(
		jsgitstatusresult = [];
	);
	statusopt.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	statusopt.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
		GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
		GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
	git_status_list_new(&status, repo, &statusopt);

	size_t i, maxi = git_status_list_entrycount(status);
	const git_status_entry *s;
	int header = 0, changes_in_index = 0;
	int changed_in_workdir = 0, rm_in_workdir = 0;
	const char *old_path, *new_path;

	/** Print index changes. */

	for (i = 0; i < maxi; ++i) {
		char *istatus = NULL;

		s = git_status_byindex(status, i);

		if (s->status == GIT_STATUS_CURRENT)
			continue;

		if (s->status & GIT_STATUS_WT_DELETED)
			rm_in_workdir = 1;

		if (s->status & GIT_STATUS_INDEX_NEW)
			istatus = "new file: ";
		if (s->status & GIT_STATUS_INDEX_MODIFIED)
			istatus = "modified: ";
		if (s->status & GIT_STATUS_INDEX_DELETED)
			istatus = "deleted:  ";
		if (s->status & GIT_STATUS_INDEX_RENAMED)
			istatus = "renamed:  ";
		if (s->status & GIT_STATUS_INDEX_TYPECHANGE)
			istatus = "typechange:";

		if (istatus == NULL)
			continue;

		if (!header) {
			printf("# Changes to be committed:\n");
			printf("#   (use \"git reset HEAD <file>...\" to unstage)\n");
			printf("#\n");
			header = 1;
		}

		old_path = s->head_to_index->old_file.path;
		new_path = s->head_to_index->new_file.path;

		if (old_path && new_path && strcmp(old_path, new_path)) {
			printf("#\t%s  %s -> %s\n", istatus, old_path, new_path);
			EM_ASM_({
				jsgitstatusresult.push({
					old_path: Pointer_stringify($0),
					new_path: Pointer_stringify($1),
					status: Pointer_stringify($2).trim().replace(':', '')
				});
			}, old_path, new_path, istatus);
		}
		else
		{
			printf("#\t%s  %s\n", istatus, old_path ? old_path : new_path);
			EM_ASM_({
				jsgitstatusresult.push({
					path: Pointer_stringify($0),
					status: Pointer_stringify($1).trim().replace(':', '')
				});
			}, old_path ? old_path : new_path, istatus);
		}
	}

	if (header) {
		changes_in_index = 1;
		printf("#\n");
	}
	header = 0;

	/** Print workdir changes to tracked files. */

	for (i = 0; i < maxi; ++i) {
		char *wstatus = NULL;

		s = git_status_byindex(status, i);

		/**
		 * With `GIT_STATUS_OPT_INCLUDE_UNMODIFIED` (not used in this example)
		 * `index_to_workdir` may not be `NULL` even if there are
		 * no differences, in which case it will be a `GIT_DELTA_UNMODIFIED`.
		 */
		if (s->status == GIT_STATUS_CURRENT || s->index_to_workdir == NULL)
			continue;

		/** Print out the output since we know the file has some changes */
		if (s->status & GIT_STATUS_WT_MODIFIED)
			wstatus = "modified: ";
		if (s->status & GIT_STATUS_WT_DELETED)
			wstatus = "deleted:  ";
		if (s->status & GIT_STATUS_WT_RENAMED)
			wstatus = "renamed:  ";
		if (s->status & GIT_STATUS_WT_TYPECHANGE)
			wstatus = "typechange:";

		if (wstatus == NULL)
			continue;

		if (!header) {
			printf("# Changes not staged for commit:\n");
			printf("#   (use \"git add%s <file>...\" to update what will be committed)\n", rm_in_workdir ? "/rm" : "");
			printf("#   (use \"git checkout -- <file>...\" to discard changes in working directory)\n");
			printf("#\n");
			header = 1;
		}

		old_path = s->index_to_workdir->old_file.path;
		new_path = s->index_to_workdir->new_file.path;

		if (old_path && new_path && strcmp(old_path, new_path)) {
			printf("#\t%s  %s -> %s\n", wstatus, old_path, new_path);
			EM_ASM_({
				jsgitstatusresult.push({
					old_path: Pointer_stringify($0),
					new_path: Pointer_stringify($1),
					status: Pointer_stringify($2).trim().replace(':', '')
				});
			}, old_path, new_path, wstatus);
		} else {
			printf("#\t%s  %s\n", wstatus, old_path ? old_path : new_path);
			EM_ASM_({
				jsgitstatusresult.push({
					path: Pointer_stringify($0),
					status: Pointer_stringify($1).trim().replace(':', '')
				});
			}, old_path ? old_path : new_path, wstatus);
		}
	}

	if (header) {
		changed_in_workdir = 1;
		printf("#\n");
	}

	/** Print untracked files. */

	header = 0;

	for (i = 0; i < maxi; ++i) {
		s = git_status_byindex(status, i);

		if (s->status == GIT_STATUS_WT_NEW) {

			if (!header) {
				printf("# Untracked files:\n");
				printf("#   (use \"git add <file>...\" to include in what will be committed)\n");
				printf("#\n");
				header = 1;
			}

			printf("#\t%s\n", s->index_to_workdir->old_file.path);

			EM_ASM_({
				jsgitstatusresult.push({
					path: Pointer_stringify($0),
					status: Pointer_stringify($1).trim().replace(':', '')
				});
			}, s->index_to_workdir->old_file.path, "untracked");
		}
	}

	header = 0;

	/** Print ignored files. */

	for (i = 0; i < maxi; ++i) {
		s = git_status_byindex(status, i);

		if (s->status == GIT_STATUS_IGNORED) {

			if (!header) {
				printf("# Ignored files:\n");
				printf("#   (use \"git add -f <file>...\" to include in what will be committed)\n");
				printf("#\n");
				header = 1;
			}

			printf("#\t%s\n", s->index_to_workdir->old_file.path);
		}
	}

	if (!changes_in_index && changed_in_workdir) {
		printf("no changes added to commit (use \"git add\" and/or \"git commit -a\")\n");
		return 0;
	} else {
		return 1;
	}
}

void jsgitpush() {
	// get the remote.
	int error;

	git_remote* remote = NULL;
	git_remote_lookup( &remote, repo, "origin" );

	char *refspec = "refs/heads/master";
	const git_strarray refspecs = {
		&refspec,
		1,
	};

	// configure options
	git_push_options options;
	git_push_init_options( &options, GIT_PUSH_OPTIONS_VERSION );

	// do the push
	printf("Do the push\n");
	error = git_remote_push( remote, &refspecs, &options);
	if (error != 0) {
		const git_error *err = giterr_last();
		if (err) printf("ERROR %d: %s\n", err->klass, err->message);
		else printf("ERROR %d: no detailed info\n", error);
	}
	printf("Push done\n");

}
