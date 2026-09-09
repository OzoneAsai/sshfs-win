#include <assert.h>
#include <stdio.h>

struct chunk { int refs; int freed; };

static void chunk_put(struct chunk *c)
{
    if (!c) return;
    assert(c->refs > 0);
    c->refs--;
    if (c->refs == 0) c->freed = 1;
}

static struct chunk *new_chunk(void)
{
    static struct chunk slots[8];
    static int next;
    struct chunk *c = &slots[next++];
    c->refs = 1; /* sshfs_send_read ownership */
    c->freed = 0;
    return c;
}

static void submit_transfer(struct chunk **slot)
{
    struct chunk *c = new_chunk();
    chunk_put(*slot);
    *slot = c; /* transfer the sole creation reference */
}

static struct chunk *borrow_cached(struct chunk *cached)
{
    if (cached) cached->refs++;
    return cached;
}

int main(void)
{
    struct chunk *local = NULL;
    struct chunk *cached = NULL;
    struct chunk *borrowed;
    struct chunk *first;

    submit_transfer(&local);
    assert(local->refs == 1);
    first = local;
    chunk_put(local); /* wait_chunk consumes temporary owner */
    assert(first->freed);

    submit_transfer(&cached);
    assert(cached->refs == 1);
    borrowed = borrow_cached(cached);
    assert(cached->refs == 2);
    chunk_put(borrowed); /* caller done; cache still owns it */
    assert(cached->refs == 1 && !cached->freed);

    first = cached;
    submit_transfer(&cached); /* replacement drops previous cache owner */
    assert(first->freed);
    assert(cached->refs == 1);
    chunk_put(cached); /* file release */
    assert(cached->freed);

    puts("read chunk ownership: PASS");
    return 0;
}
