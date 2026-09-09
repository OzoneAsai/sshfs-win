#include <assert.h>
#include <stdio.h>
#include <string.h>

struct entry { const char *key; int conn; int live; };

static void update_after_rename(struct entry *src, struct entry *dst,
                                const char *from, const char *to)
{
    if (strcmp(from, to) == 0)
        return;

    if (dst != NULL && dst != src)
        dst->live = 0; /* destination path is replaced; old open owner may outlive key */
    src->key = to;
    src->live = 1;
}

int main(void)
{
    struct entry src = { "/a", 1, 1 };
    struct entry dst = { "/b", 2, 1 };

    update_after_rename(&src, NULL, "/a", "/a");
    assert(src.live == 1);
    assert(strcmp(src.key, "/a") == 0);
    assert(src.conn == 1);

    update_after_rename(&src, &dst, "/a", "/b");
    assert(src.live == 1);
    assert(strcmp(src.key, "/b") == 0);
    assert(dst.live == 0);
    assert(dst.conn == 2);

    puts("rename affinity contract: PASS");
    return 0;
}
