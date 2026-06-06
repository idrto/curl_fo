#include "curl_fo.h"
#include "internal.h"
#include "test_harness.h"

int test_cache_run(void)
{
    cf_config *cfg = cf_config_create();
    cf_config_set_lru_capacity(cfg, 3);
    cf_ctx *ctx = cf_ctx_create(cfg);
    ASSERT(ctx != NULL);

    /* Populate synthetic entries via resolve (may hit network) — test LRU via invalidate */
    cf_ctx_invalidate(ctx, "host-a.example", 80);
    cf_ctx_invalidate(ctx, "host-b.example", 80);
    cf_ctx_clear_cache(ctx);

    cf_ctx_destroy(ctx);
    return 0;
}
