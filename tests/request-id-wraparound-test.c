#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct ids {
    uint8_t next;
    unsigned char occupied[256];
};

static int alloc_id(struct ids *s, uint8_t *out)
{
    uint8_t first = s->next++;
    uint8_t id = first;

    while (s->occupied[id]) {
        id = s->next++;
        if (id == first)
            return -1;
    }
    *out = id;
    return 0;
}

int main(void)
{
    struct ids s = {0};
    uint8_t id;
    unsigned i;

    /* Force wrap while IDs 254, 255 and 0 are still in flight. */
    s.next = 254;
    s.occupied[254] = 1;
    s.occupied[255] = 1;
    s.occupied[0] = 1;
    assert(alloc_id(&s, &id) == 0);
    assert(id == 1);

    /* Releasing an old ID makes it reusable after a later wrap. */
    s.occupied[254] = 0;
    s.next = 254;
    assert(alloc_id(&s, &id) == 0);
    assert(id == 254);

    /* Exhaustion must fail instead of replacing an existing owner. */
    for (i = 0; i < 256; i++)
        s.occupied[i] = 1;
    s.next = 17;
    assert(alloc_id(&s, &id) == -1);

    puts("request ID wraparound: PASS");
    return 0;
}
