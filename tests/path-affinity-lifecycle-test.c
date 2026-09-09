#include <assert.h>
#include <stdio.h>

struct owner { int conn; unsigned refs; };
struct slot { struct owner *value; };

static void unlink_success(struct slot *path)
{
    path->value = NULL;
}

static void rename_success(struct slot *from, struct slot *to, int posix)
{
    struct owner *src = from->value;
    struct owner *dst = to->value;

    if (src != NULL) {
        if (posix && dst == src)
            return; /* known hard-link aliases: POSIX rename is a no-op */
        to->value = src;
        from->value = NULL;
    } else {
        to->value = NULL; /* replacement invalidates stale destination affinity */
    }
}

int main(void)
{
    struct owner old = { 1, 1 };
    struct owner src = { 2, 2 };
    struct owner alias = { 3, 3 };
    struct slot path = { &old };
    struct slot from = { NULL };
    struct slot to = { &old };
    struct slot a = { &src };
    struct slot b = { &old };
    struct slot h1 = { &alias };
    struct slot h2 = { &alias };

    unlink_success(&path);
    assert(path.value == NULL);
    assert(old.refs == 1); /* open file owns lifetime, not the pathname key */

    rename_success(&from, &to, 1);
    assert(to.value == NULL);
    assert(old.refs == 1);

    rename_success(&a, &b, 1);
    assert(a.value == NULL);
    assert(b.value == &src);
    assert(src.refs == 2);
    assert(old.refs == 1);

    rename_success(&h1, &h2, 1);
    assert(h1.value == &alias);
    assert(h2.value == &alias);
    assert(alias.refs == 3);

    puts("path affinity lifecycle: PASS");
    return 0;
}
