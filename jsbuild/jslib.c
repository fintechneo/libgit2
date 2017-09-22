#include <stdio.h>
#include "../examples/network/common.h"
#include "../include/git2.h"
#include "../include/git2/clone.h"

static git_repository *repo = NULL;

typedef struct progress_data {
	git_transfer_progress fetch_progress;
	size_t completed_steps;
	size_t total_steps;
	const char *path;
} progress_data;

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



void jsgitinit() {
	git_libgit2_init();
	printf("libgit2 for javascript initialized\n");
}

void jsgitopenrepo() {
	git_repository_open(&repo, ".");
}

void jsgitclone(char * url, char * localdir) {			
	cloneremote(url,localdir);			
}

void jsgitadd(char * path) {	
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

void jsgitpush() {

}
