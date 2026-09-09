#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static unsigned average_rtt(uint64_t total_rtt, uint64_t received)
{
    return received ? (unsigned)(total_rtt / received) : 0;
}

int main(void)
{
    /* Five requests may be sent while only four replies contributed to total_rtt. */
    assert(average_rtt(40, 4) == 10);
    assert(average_rtt(0, 0) == 0);
    puts("diagnostic RTT average denominator: PASS");
    return 0;
}
