#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../portable-format.h"


int main(void)
{
    char *text = 0;
    char large[4097];
    size_t i;

    if (0 != sshfs_win_alloc_printf(&text, "%s:%u:%s", "host", 22u, "path"))
        return 1;
    if (0 != strcmp(text, "host:22:path"))
    {
        free(text);
        return 2;
    }
    free(text);

    for (i = 0; i < sizeof large - 1; i++)
        large[i] = (char)('a' + (i % 26));
    large[sizeof large - 1] = '\0';

    text = 0;
    if (0 != sshfs_win_alloc_printf(&text, "[%s]", large))
        return 3;
    if (strlen(text) != strlen(large) + 2 ||
        '[' != text[0] || ']' != text[strlen(text) - 1])
    {
        free(text);
        return 4;
    }
    free(text);

    errno = 0;
    if (0 == sshfs_win_alloc_printf(0, "%s", "x") || EINVAL != errno)
        return 5;

    puts("portable format: PASS");
    return 0;
}
