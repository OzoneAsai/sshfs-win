#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct conn {
    int rfd;
    int req_count;
    int dir_count;
    int file_count;
    unsigned remote_handles;
    unsigned remote_handle_reservations;
};

static int choose_conn(struct conn *conns, int max_conns, unsigned handle_limit)
{
    int i;
    int best_index = 0;
    int capacity_index = -1;
    uint64_t best_score = ~0ULL;
    uint64_t capacity_score = ~0ULL;
    uint64_t capacity_pressure = ~0ULL;

    for (i = 0; i < max_conns; i++) {
        uint64_t score = ((uint64_t) conns[i].req_count << 43) +
                         ((uint64_t) conns[i].dir_count << 22) +
                         ((uint64_t) conns[i].file_count << 1) +
                         (uint64_t) (conns[i].rfd >= 0 ? 0 : 1);
        uint64_t pressure = conns[i].remote_handles +
                            conns[i].remote_handle_reservations;
        if (score < best_score) {
            best_index = i;
            best_score = score;
        }
        if (handle_limit != 0 && pressure < handle_limit &&
            (capacity_index == -1 || pressure < capacity_pressure ||
             (pressure == capacity_pressure && score < capacity_score))) {
            capacity_index = i;
            capacity_pressure = pressure;
            capacity_score = score;
        }
    }
    return capacity_index != -1 ? capacity_index : best_index;
}

int main(void)
{
    struct conn c[3] = {
        {.rfd=1, .file_count=0, .remote_handles=8},
        {.rfd=1, .file_count=3, .remote_handles=2},
        {.rfd=1, .file_count=1, .remote_handles=5},
    };

    /* With a cap, spare server-handle capacity outranks ordinary load score. */
    assert(choose_conn(c, 3, 8) == 1);

    /* Reservations participate in pressure so bursts spread before replies arrive. */
    c[1].remote_handle_reservations = 5; /* pressure 7 */
    assert(choose_conn(c, 3, 8) == 2);   /* pressure 5 */

    /* A full connection is skipped when another has capacity. */
    c[2].remote_handle_reservations = 3; /* pressure 8 */
    assert(choose_conn(c, 3, 8) == 1);

    /* If every connection is full, preserve the original score fallback;
       authoritative admission control will return EMFILE. */
    c[0].remote_handles = 8;
    c[1].remote_handles = 8;
    c[1].remote_handle_reservations = 0;
    c[2].remote_handles = 8;
    c[2].remote_handle_reservations = 0;
    assert(choose_conn(c, 3, 8) == 0);

    /* Disabled handle limit preserves upstream load balancing exactly. */
    c[0].remote_handles = 1000;
    c[1].remote_handles = 0;
    c[2].remote_handles = 0;
    assert(choose_conn(c, 3, 0) == 0);

    puts("connection-routing-test: PASS");
    return 0;
}
