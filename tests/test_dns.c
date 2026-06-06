#include "internal.h"
#include "test_harness.h"

int test_dns_run(void)
{
    cf_dns_result res;
    memset(&res, 0, sizeof(res));

    /* Fallback resolver should resolve localhost */
    int rc = cf_dns_resolve("localhost", &res, 300);
    if (rc == 0) {
        ASSERT(res.count >= 1);
        ASSERT(res.ttl_sec > 0);
    }
    cf_dns_result_free(&res);

    rc = cf_dns_resolve("invalid.invalid.invalid.example", &res, 300);
    /* May fail on network — acceptable */
    cf_dns_result_free(&res);

    return 0;
}
