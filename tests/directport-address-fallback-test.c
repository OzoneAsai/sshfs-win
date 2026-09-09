#include <assert.h>
#include <stdio.h>

struct candidate {
    int socket_ok;
    int connect_ok;
};

static int connect_candidates(const struct candidate *c, int n, int *attempts)
{
    int i;
    *attempts = 0;
    for (i = 0; i < n; i++) {
        (*attempts)++;
        if (!c[i].socket_ok)
            continue;
        if (c[i].connect_ok)
            return i;
    }
    return -1;
}

int main(void)
{
    int attempts;
    const struct candidate first_dead_second_live[] = {
        {1, 0}, {1, 1}, {1, 1}
    };
    const struct candidate socket_failure_then_v6_live[] = {
        {0, 0}, {1, 1}
    };
    const struct candidate all_dead[] = {
        {1, 0}, {0, 0}, {1, 0}
    };

    assert(connect_candidates(first_dead_second_live, 3, &attempts) == 1);
    assert(attempts == 2);

    assert(connect_candidates(socket_failure_then_v6_live, 2, &attempts) == 1);
    assert(attempts == 2);

    assert(connect_candidates(all_dead, 3, &attempts) == -1);
    assert(attempts == 3);

    puts("directport-address-fallback-test: PASS");
    return 0;
}
