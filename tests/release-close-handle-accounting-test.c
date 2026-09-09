#include <assert.h>
#include <errno.h>
#include <stdio.h>

struct conn_state {
    unsigned remote_handles;
    unsigned handle_limit;
};

static int can_open(const struct conn_state *c)
{
    return c->handle_limit == 0 || c->remote_handles < c->handle_limit;
}

static int close_reply(struct conn_state *c, int status)
{
    if (status == 0 && c->remote_handles != 0)
        c->remote_handles--;
    return status;
}

int main(void)
{
    struct conn_state c = { .remote_handles = 1, .handle_limit = 1 };

    /* Old release semantics: returning before CLOSE reply leaves admission full. */
    assert(!can_open(&c));

    /* CP31-X: release consumes the STATUS barrier before returning. */
    assert(close_reply(&c, 0) == 0);
    assert(c.remote_handles == 0);
    assert(can_open(&c));

    /* A failed CLOSE remains conservatively accounted and propagates the error. */
    c.remote_handles = 1;
    assert(close_reply(&c, -EIO) == -EIO);
    assert(c.remote_handles == 1);
    assert(!can_open(&c));

    /* Unlimited mode is never spuriously blocked by accounting pressure. */
    c.handle_limit = 0;
    assert(can_open(&c));

    puts("release-close-handle-accounting-test: PASS");
    return 0;
}
