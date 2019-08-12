/******************************************************/
/**
 * API for adding custom merge driver 
 *   
 *  PJL  Aug 2019
 */

#include <emscripten.h>

#include <stdio.h>
#include "streams/stransport.h"
#include "streams/tls.h"

#ifdef EMSCRIPTEN_NODEJS
#include "streams/emscripten_nodejs.h"
#else
#include "streams/emscripten_browser.h"
#endif

#include "git2.h"
#include "git2/clone.h"
#include "git2/merge.h"
#include "filter.h"
#include "git2/sys/filter.h"

#include "git2/repository.h"
#include "buffer.h"
#include "merge.h"

#include "merge_driver.h"

// static void (*custom_driver_callback)(const char *ancestor, const char *ours, const char *theirs) = NULL;

extern git_repository *repo;  // lives in jslib.c

static int custom_merge_file__from_inputs(
    git_merge_file_result *out,
    const git_merge_file_input *ancestor,
    const git_merge_file_input *ours,
    const git_merge_file_input *theirs,
    const git_merge_file_options *given_opts)
{

   // printf(" XXX OURS = %p %s\n",ours->ptr,ours->ptr);


    EM_ASM_( 
       { custom_driver_callback($0,$1,$2)
       },ancestor->ptr, ours->ptr,
         theirs->ptr);
}

int custom_merge_file__input_from_index(
    git_merge_file_input *input_out,
    git_odb_object **odb_object_out,
    git_odb *odb,
    const git_index_entry *entry)
{
    int error = 0;

    assert(input_out && odb_object_out && odb && entry);

    if ((error = git_odb_read(odb_object_out, odb, &entry->id)) < 0)
        goto done;

    input_out->path = entry->path;
    input_out->mode = entry->mode;
    input_out->ptr = (char *)git_odb_object_data(*odb_object_out);
    input_out->size = git_odb_object_size(*odb_object_out);

done:
    return error;
}

int custom_merge_file_from_index(
    git_merge_file_result *out,
    git_repository *repo,
    const git_index_entry *ancestor,
    const git_index_entry *ours,
    const git_index_entry *theirs,
    const git_merge_file_options *options)
{
    git_merge_file_input *ancestor_ptr = NULL,
                         ancestor_input = {0}, our_input = {0}, their_input = {0};
    git_odb *odb = NULL;
    git_odb_object *odb_object[3] = {0};
    int error = 0;

    assert(out && repo && ours && theirs);

    memset(out, 0x0, sizeof(git_merge_file_result));

    if ((error = git_repository_odb(&odb, repo)) < 0)
        goto done;

    if (ancestor)
    {
        if ((error = custom_merge_file__input_from_index(
                 &ancestor_input, &odb_object[0], odb, ancestor)) < 0)
            goto done;

        ancestor_ptr = &ancestor_input;
    }

    if ((error = custom_merge_file__input_from_index(
             &our_input, &odb_object[1], odb, ours)) < 0 ||
        (error = custom_merge_file__input_from_index(
             &their_input, &odb_object[2], odb, theirs)) < 0)
        goto done;

    error = custom_merge_file__from_inputs(out,
                                           ancestor_ptr, &our_input, &their_input, options);

done:
    git_odb_object_free(odb_object[0]);
    git_odb_object_free(odb_object[1]);
    git_odb_object_free(odb_object[2]);
    git_odb_free(odb);

    return error;
}

static int test_driver_apply(
    git_merge_driver *s,
    const char **path_out,
    uint32_t *mode_out,
    git_buf *merged_out,
    const char *filter_name,
    const git_merge_driver_source *src)
{
    int error;
    printf("XXXXXXXXX      custom merge    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX  \n");

    git_merge_file_result result = {0};
    git_merge_file_options file_opts = GIT_MERGE_FILE_OPTIONS_INIT;

    //
    if ((error = custom_merge_file_from_index(&result, src->repo,
                                              src->ancestor, src->ours, src->theirs, &file_opts)) < 0)
        goto done;

    printf(" Result will be written to %s\n", result.path);

    if (!result.automergeable &&
        !(file_opts.flags & GIT_MERGE_FILE_FAVOR__CONFLICTED))
    {
        error = GIT_EMERGECONFLICT;
        goto done;
    }

    *path_out = git_merge_file__best_path(
        src->ancestor ? src->ancestor->path : NULL,
        src->ours ? src->ours->path : NULL,
        src->theirs ? src->theirs->path : NULL);

    *mode_out = git_merge_file__best_mode(
        src->ancestor ? src->ancestor->mode : 0,
        src->ours ? src->ours->mode : 0,
        src->theirs ? src->theirs->mode : 0);

    merged_out->ptr = (char *)result.ptr;
    merged_out->size = result.len;
    merged_out->asize = result.len;
    result.ptr = NULL;

done:
    git_merge_file_result_free(&result);
    return error;

    //  TODO put something real here
    // GIT_UNUSED(s);
    // GIT_UNUSED(src);
    // EM_ASM_({ driver_callback() } );
    // *path_out = "applied.txt";
    // *mode_out = GIT_FILEMODE_BLOB;

    // return git_buf_printf(merged_out, "This is the `%s` driver.\n",
    // 	filter_name);
}

static int test_driver_init(git_merge_driver *s)
{
    printf(" test_driver_init \n");
    return 0;
}

static void test_driver_shutdown(git_merge_driver *s)
{
    printf(" test_driver_shutdown \n");
}

static struct git_merge_driver test_driver_custom =
    {
        GIT_MERGE_DRIVER_VERSION,
        test_driver_init,
        test_driver_shutdown,
        test_driver_apply,
};

/* TODO when OR do we need do to call this ? */

static void test_drivers_unregister(void)
{
    git_merge_driver_unregister("custom");
    git_merge_driver_unregister("*");
}

void EMSCRIPTEN_KEEPALIVE jsregisterdriver()
{

    git_config *cfg;

    //  (*custom_driver_callback)();
    // printf(" Hello from register driver \n ");

    /* Ensure that the user's merge.conflictstyle doesn't interfere */

    // PJL fetch config and set stuff
    git_repository_config(&cfg, repo);

    git_config_set_string(cfg, "merge.conflictstyle", "merge");
    git_config_set_bool(cfg, "core.autocrlf", false);

    git_merge_driver_register("custom", &test_driver_custom);
    git_merge_driver_register("*", &test_driver_custom);

    git_config_free(cfg);
}
