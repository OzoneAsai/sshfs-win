#include <assert.h>
#include <errno.h>
#include <stdio.h>

struct conn { int connver; };

static int request_gen(struct conn *c, int expected)
{
    return c->connver == expected ? 0 : -EIO;
}

static int open_transaction(struct conn *c, int reconnect_after_open)
{
    int open_connver = c->connver;
    int err = request_gen(c, open_connver); /* OPEN */
    if (err)
        return err;
    if (reconnect_after_open)
        c->connver++;
    /* Metadata belongs to the same session transaction as the handle. */
    return request_gen(c, open_connver); /* STAT/LSTAT */
}

int main(void)
{
    struct conn c = { 7 };
    assert(open_transaction(&c, 0) == 0);
    c.connver = 7;
    assert(open_transaction(&c, 1) == -EIO);
    puts("open generation transaction: PASS");
    return 0;
}
