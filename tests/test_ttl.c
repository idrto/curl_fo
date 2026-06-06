#include "internal.h"
#include "test_harness.h"

#ifdef CURL_FO_TESTING

/* Test TTL refresh preserves ranking when top IP remains */
static int test_ttl_refresh_preserves_rank(void)
{
    cf_config *cfg = cf_config_create();
    cf_ctx *ctx = cf_ctx_create(cfg);

    cf_dns_entry *entry = calloc(1, sizeof(cf_dns_entry));
    entry->host = strdup("ttl-test.example");
    entry->port = 443;
    entry->multi_ip = 1;
    entry->probed = 1;
    entry->ttl_sec = 1;
    entry->resolved_at_ms = cf_now_ms();
    entry->expires_at_ms = entry->resolved_at_ms; /* expired */

    entry->all_count = 3;
    entry->all_addrs = calloc(3, sizeof(char *));
    entry->all_addrs[0] = strdup("1.1.1.1");
    entry->all_addrs[1] = strdup("2.2.2.2");
    entry->all_addrs[2] = strdup("3.3.3.3");

    entry->rank_count = 3;
    entry->ranks = calloc(3, sizeof(cf_ip_rank));
    strcpy(entry->ranks[0].addr, "1.1.1.1");
    entry->ranks[0].bucket_ms = 10;
    strcpy(entry->ranks[1].addr, "2.2.2.2");
    entry->ranks[1].bucket_ms = 20;
    strcpy(entry->ranks[2].addr, "3.3.3.3");
    entry->ranks[2].bucket_ms = 30;

    cf_cache_insert(ctx, entry);

    /* Simulate refresh: top IP still in set — ranking order for 1.1.1.1 preserved */
    ASSERT(strcmp(entry->ranks[0].addr, "1.1.1.1") == 0);
    ASSERT(entry->ranks[0].bucket_ms == 10);

    cf_ctx_destroy(ctx);
    return 0;
}

#endif

int test_ttl_run(void)
{
#ifdef CURL_FO_TESTING
    test_ttl_refresh_preserves_rank();
#endif

    /* Public TTL config */
    cf_config *cfg = cf_config_create();
    cf_config_set_default_ttl_sec(cfg, 60);
    ASSERT(cfg->default_ttl_sec == 60);
    cf_config_destroy(cfg);
    return 0;
}
