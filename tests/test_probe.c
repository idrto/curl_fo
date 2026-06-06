#include "internal.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

static unsigned test_round_bucket(unsigned ms, unsigned bucket)
{
    if (bucket == 0) bucket = 10;
    unsigned rounded = ((ms + bucket - 1) / bucket) * bucket;
    return rounded == 0 ? bucket : rounded;
}

int test_probe_run(void)
{
    ASSERT(test_round_bucket(1, 10) == 10);
    ASSERT(test_round_bucket(10, 10) == 10);
    ASSERT(test_round_bucket(11, 10) == 20);
    ASSERT(test_round_bucket(20, 10) == 20);
    ASSERT(test_round_bucket(21, 10) == 30);

    char *addrs[] = {"127.0.0.1"};
    cf_ip_rank *out = NULL;
    size_t count = 0;
    if (cf_probe_rank("localhost", 80, addrs, 1, 10, 3, &out, &count, NULL) == 0) {
        ASSERT(count == 1);
        ASSERT(out[0].bucket_ms >= 10);
        free(out);
    }

    return 0;
}
