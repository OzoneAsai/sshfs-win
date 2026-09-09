#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

struct buffer { const unsigned char *p; size_t len, size; };

static int get_u32(struct buffer *b, uint32_t *v)
{
    const unsigned char *p;
    if (b->len > b->size || b->size - b->len < 4) return -1;
    p = b->p + b->len;
    *v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    b->len += 4;
    return 0;
}

static int valid_handle(const struct buffer *reply)
{
    struct buffer b = *reply;
    uint32_t n;
    if (b.len > b.size || get_u32(&b, &n) == -1) return 0;
    return n == b.size - b.len;
}

static int wait_handle_result(const struct buffer *reply)
{
    return valid_handle(reply) ? 0 : -EIO;
}

int main(void)
{
    const unsigned char good[] = {0,0,0,3,'a','b','c'};
    const unsigned char short_len[] = {0,0,0};
    const unsigned char truncated[] = {0,0,0,3,'a'};
    const unsigned char trailing[] = {0,0,0,1,'a','x'};
    const unsigned char empty[] = {0,0,0,0};
    struct buffer b;

    b = (struct buffer){ good, 0, sizeof good }; assert(wait_handle_result(&b) == 0);
    b = (struct buffer){ empty, 0, sizeof empty }; assert(wait_handle_result(&b) == 0);
    b = (struct buffer){ short_len, 0, sizeof short_len }; assert(wait_handle_result(&b) == -EIO);
    b = (struct buffer){ truncated, 0, sizeof truncated }; assert(wait_handle_result(&b) == -EIO);
    b = (struct buffer){ trailing, 0, sizeof trailing }; assert(wait_handle_result(&b) == -EIO);

    puts("handle reply structural contract: PASS");
    return 0;
}
