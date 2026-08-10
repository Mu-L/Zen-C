// SPDX-License-Identifier: MIT
// Standalone Zen C source formatter. Reads source from a file (or stdin) and
// prints the formatted source to stdout.
#include "tool_common.h"
#include "../lsp/lsp_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    z_compiler_setup();

    const char *input = NULL;
    int check = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--check") == 0)
        {
            check = 1;
        }
        else if (argv[i][0] != '-' && !input)
        {
            input = argv[i];
        }
        else
        {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }

    char *src = NULL;
    if (input)
    {
        FILE *f = fopen(input, "rb");
        if (!f)
        {
            fprintf(stderr, "error: could not open '%s'\n", input);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        src = (char *)malloc((size_t)size + 1);
        if (!src)
        {
            fclose(f);
            return 1;
        }
        if (fread(src, 1, (size_t)size, f) != (size_t)size)
        {
            free(src);
            fclose(f);
            return 1;
        }
        src[size] = 0;
        fclose(f);
    }
    else
    {
        size_t cap = 4096;
        size_t len = 0;
        src = (char *)malloc(cap);
        if (!src)
        {
            return 1;
        }
        int ch;
        while ((ch = fgetc(stdin)) != EOF)
        {
            if (len + 1 >= cap)
            {
                cap *= 2;
                src = (char *)realloc(src, cap);
            }
            src[len++] = (char)ch;
        }
        src[len] = 0;
    }

    char *formatted = lsp_format_source(src);
    if (!formatted)
    {
        fprintf(stderr, "error: formatting failed\n");
        free(src);
        return 1;
    }

    if (check)
    {
        int same = strcmp(formatted, src) == 0;
        free(src);
        free(formatted);
        return same ? 0 : 1;
    }

    fputs(formatted, stdout);
    free(src);
    free(formatted);
    return 0;
}
