/* Windows-only deterministic parent-death regression; test seams never ship. */
#include "foundation/subprocess.h"
#include "foundation/compat_fs.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *marker;
enum { FIXTURE_ARG_COUNT = 3, FIXTURE_POLL_MS = 1000 };
static void created(unsigned long process_id, bool in_owned_job) {
    FILE *file = cbm_fopen(marker, "wbx");
    if (!file) {
        ExitProcess(ERROR_WRITE_FAULT);
    }
    (void)fprintf(file, "%lu %u\n", process_id, (unsigned)in_owned_job);
    if (fclose(file) != 0) {
        ExitProcess(ERROR_WRITE_FAULT);
    }
    for (;;) {
        Sleep(FIXTURE_POLL_MS);
    }
}
int main(int argc, char **argv) {
    if (argc != FIXTURE_ARG_COUNT || strcmp(argv[1], "parent") != 0) {
        return ERROR_INVALID_PARAMETER;
    }
    marker = argv[2];
    cbm_subprocess_set_created_hook_for_testing(created);
    const char *worker_argv[] = {argv[0], "worker", NULL};
    cbm_proc_opts_t options = {.bin = argv[0], .argv = worker_argv};
    cbm_subprocess_t *process = NULL;
    int result = cbm_subprocess_spawn(&options, &process);
    /* The hook must block before spawn returns and before worker execution. */
    return result == 0 ? ERROR_INVALID_FUNCTION : ERROR_PROCESS_ABORTED;
}
