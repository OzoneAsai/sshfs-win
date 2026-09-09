#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct conn {
    int connver;
    uint32_t server_version;
    int ext_posix_rename;
    int ext_statvfs;
    int ext_hardlink;
    int ext_fsync;
};

struct features {
    struct conn *conn;
    int connver;
    uint32_t server_version;
    int ext_posix_rename;
    int ext_statvfs;
    int ext_hardlink;
};

static void reset_session(struct conn *c, int newver)
{
    c->connver = newver;
    c->server_version = 0;
    c->ext_posix_rename = 0;
    c->ext_statvfs = 0;
    c->ext_hardlink = 0;
    c->ext_fsync = 0;
}

static struct features snapshot(struct conn *c)
{
    return (struct features){
        .conn = c,
        .connver = c->connver,
        .server_version = c->server_version,
        .ext_posix_rename = c->ext_posix_rename,
        .ext_statvfs = c->ext_statvfs,
        .ext_hardlink = c->ext_hardlink,
    };
}

static int generation_matches(const struct features *f)
{
    return f->connver == f->conn->connver;
}

int main(void)
{
    struct conn a = { .connver = 10, .server_version = 3,
                      .ext_posix_rename = 1, .ext_statvfs = 1,
                      .ext_hardlink = 1, .ext_fsync = 1 };
    struct conn b = { .connver = 20, .server_version = 3 };
    struct features fa = snapshot(&a);
    struct features fb = snapshot(&b);

    /* Capabilities are isolated per transport, not a process-wide union. */
    assert(fa.ext_statvfs == 1);
    assert(fb.ext_statvfs == 0);
    assert(a.ext_fsync == 1);
    assert(b.ext_fsync == 0);

    /* A reconnect must not inherit extension advertisements from generation 10. */
    reset_session(&a, 11);
    assert(a.server_version == 0);
    assert(a.ext_posix_rename == 0);
    assert(a.ext_statvfs == 0);
    assert(a.ext_hardlink == 0);
    assert(a.ext_fsync == 0);

    /* Feature decisions are pinned to the generation they were observed on. */
    assert(!generation_matches(&fa));
    fa = snapshot(&a);
    assert(generation_matches(&fa));

    /* A second connection may advertise a different feature set safely. */
    b.ext_hardlink = 1;
    fb = snapshot(&b);
    assert(fb.ext_hardlink == 1);
    assert(fa.ext_hardlink == 0);

    puts("connection capability generation: PASS");
    return 0;
}
