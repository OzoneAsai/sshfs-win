#ifndef SSHFS_WIN_PORTABLE_FORMAT_H
#define SSHFS_WIN_PORTABLE_FORMAT_H

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int sshfs_win_alloc_printf(char **out, const char *format, ...)
{
    va_list ap;
    char *buffer;
    int needed;
    int written;

    if (0 == out || 0 == format)
    {
        errno = EINVAL;
        return -1;
    }
    *out = 0;

    va_start(ap, format);
    needed = vsnprintf(0, 0, format, ap);
    va_end(ap);
    if (0 > needed)
    {
        errno = EINVAL;
        return -1;
    }

    buffer = malloc((size_t)needed + 1);
    if (0 == buffer)
    {
        errno = ENOMEM;
        return -1;
    }

    va_start(ap, format);
    written = vsnprintf(buffer, (size_t)needed + 1, format, ap);
    va_end(ap);
    if (written != needed)
    {
        free(buffer);
        errno = EIO;
        return -1;
    }

    *out = buffer;
    return 0;
}

#endif
