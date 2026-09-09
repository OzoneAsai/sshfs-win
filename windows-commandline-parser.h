#ifndef SSHFS_WIN_WINDOWS_COMMANDLINE_PARSER_H
#define SSHFS_WIN_WINDOWS_COMMANDLINE_PARSER_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Parse a Microsoft CRT-style command line that is already UTF-8. Quote and
 * backslash are ASCII, so parsing after strict UTF-16 -> UTF-8 conversion
 * preserves the Windows quoting grammar without depending on Win32 APIs.
 */
static inline int win32_parse_command_line_utf8(const char *command_line,
    int *argc_out, char ***argv_out)
{
    size_t n;
    char *storage, *src, *dst;
    char **argv;
    int argc = 0;

    if (0 == command_line || 0 == argc_out || 0 == argv_out)
        return -1;

    n = strlen(command_line);
    storage = malloc(n + 1);
    argv = calloc(n + 2, sizeof *argv);
    if (0 == storage || 0 == argv)
    {
        free(argv);
        free(storage);
        return -1;
    }
    memcpy(storage, command_line, n + 1);

    src = storage;
    dst = storage;
    while (*src)
    {
        int quoted = 0;

        while (' ' == *src || '\t' == *src)
            src++;
        if ('\0' == *src)
            break;

        argv[argc++] = dst;
        while (*src)
        {
            size_t backslashes = 0, i;

            if (!quoted && (' ' == *src || '\t' == *src))
                break;

            while ('\\' == *src)
            {
                backslashes++;
                src++;
            }

            if ('"' == *src)
            {
                for (i = 0; i < backslashes / 2; i++)
                    *dst++ = '\\';
                if (0 == backslashes && quoted && '"' == src[1])
                {
                    *dst++ = '"';
                    src += 2;
                }
                else if (backslashes & 1)
                {
                    *dst++ = '"';
                    src++;
                }
                else
                {
                    quoted = !quoted;
                    src++;
                }
                continue;
            }

            for (i = 0; i < backslashes; i++)
                *dst++ = '\\';

            if ('\0' == *src)
                break;
            if (!quoted && (' ' == *src || '\t' == *src))
                break;
            *dst++ = *src++;
        }
        while (' ' == *src || '\t' == *src)
            src++;
        *dst++ = '\0';
    }

    argv[argc] = 0;
    if (0 == argc)
    {
        free(argv);
        free(storage);
        return -1;
    }

    *argc_out = argc;
    *argv_out = argv;
    return 0;
}

static inline void win32_free_parsed_argv(char **argv)
{
    if (0 != argv)
    {
        if (0 != argv[0])
            free(argv[0]);
        free(argv);
    }
}

#endif
