// SPDX-License-Identifier: MIT
#include "tool_common.h"
#include "../compiler.h"
#include "../codegen/codegen.h"
#include "../diagnostics/diagnostics.h"
#include "../platform/os.h"
#include "../utils/utils.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void z_tool_crash(int sig)
{
    fprintf(stderr, "\n------------------------------------------------\n");
    fprintf(stderr, "CRITICAL: Compiler crashed with signal %d\n", sig);
    fprintf(stderr, "This is likely a bug in the Zen compiler.\n");
    fprintf(stderr, "Flushing all output files before exit...\n");
    fprintf(stderr, "------------------------------------------------\n");
    fflush(NULL);
    _exit(139);
}

// Shared initialization: signal handlers, terminal/config defaults, diagnostics
// and std/ root-path discovery. Mirrors what `zc`'s main() used to do inline so
// every binary (compiler and tools) boots the same way.
void z_compiler_setup(void)
{
    signal(SIGSEGV, z_tool_crash);
    signal(SIGABRT, z_tool_crash);
    signal(SIGFPE, z_tool_crash);

    z_setup_terminal();
    memset(&g_config, 0, sizeof(g_config));
    g_config.mode_debug = 1;
    if (z_is_windows())
    {
        strncpy(g_config.cc, "gcc.exe", sizeof(g_config.cc) - 1);
        g_config.cc[sizeof(g_config.cc) - 1] = '\0';
    }
    else
    {
        strncpy(g_config.cc, "gcc", sizeof(g_config.cc) - 1);
        g_config.cc[sizeof(g_config.cc) - 1] = '\0';
    }

    // Default diagnostics: Enable standard Zen C diagnostics
    set_diag_by_name("unused", 1);
    set_diag_by_name("safety", 1);
    set_diag_by_name("logic", 1);
    set_diag_by_name("conversion", 1);
    set_diag_by_name("style", 1);

    codegen_init_backends();

    char self_path[MAX_PATH_SIZE];
    z_get_executable_path(self_path, sizeof(self_path));
    if (self_path[0])
    {
        g_config.root_path = xstrdup(self_path);

        // Improve root_path discovery: look for std.zc in root_path or its parents
        char current_root[MAX_PATH_SIZE];
        strncpy(current_root, self_path, sizeof(current_root) - 1);
        current_root[sizeof(current_root) - 1] = '\0';

        while (current_root[0])
        {
            char check_path[MAX_PATH_SIZE + 32];
            snprintf(check_path, sizeof(check_path), "%s/std.zc", current_root);
            if (access(check_path, F_OK) == 0)
            {
                // Found it!
                zfree(g_config.root_path);
                g_config.root_path = xstrdup(current_root);
                break;
            }

            // Try parent
            char *last_slash = (char *)strrchr(current_root, '/');
            if (last_slash && last_slash != current_root)
            {
                *last_slash = '\0';
            }
            else
            {
                break; // Reached root or no more slashes
            }
        }
    }
    else
    {
        // Fallback to std_root in config if self path fails
        if (g_config.std_root[0])
        {
            zfree(g_config.root_path);
            g_config.root_path = xstrdup(g_config.std_root);
        }
    }
}
