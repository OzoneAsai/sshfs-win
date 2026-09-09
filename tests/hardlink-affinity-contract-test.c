#include <assert.h>
#include <stdio.h>
#include <string.h>

struct entry {
    int conn;
    unsigned refs;
};

struct path_slot {
    const char *path;
    struct entry *owner;
};

static void update_after_link(struct path_slot *from, struct path_slot *to)
{
    /* A successful hard link changes only the destination pathname mapping. */
    to->owner = from->owner;
}

static void update_after_link_without_source_affinity(struct path_slot *to)
{
    /* A stale destination key must not survive creation of a new inode alias. */
    to->owner = NULL;
}

int main(void)
{
    struct entry src_owner = { 1, 2 };
    struct entry stale_owner = { 2, 1 };
    struct path_slot from = { "/from", &src_owner };
    struct path_slot to = { "/to", &stale_owner };
    unsigned src_refs = src_owner.refs;
    unsigned stale_refs = stale_owner.refs;

    update_after_link(&from, &to);
    assert(strcmp(from.path, "/from") == 0);
    assert(from.owner == &src_owner);
    assert(to.owner == &src_owner);
    assert(from.owner->conn == 1);
    assert(src_owner.refs == src_refs);       /* alias keys do not add refs */
    assert(stale_owner.refs == stale_refs);   /* displaced open owner survives */

    to.owner = &stale_owner;
    update_after_link_without_source_affinity(&to);
    assert(to.owner == NULL);
    assert(stale_owner.refs == stale_refs);

    puts("hardlink affinity contract: PASS");
    return 0;
}
