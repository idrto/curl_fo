#include "curl_fo.h"
#include "internal.h"
#include "test_harness.h"

#include <string.h>

int test_util_run(void)
{
    char host[128];
    uint16_t port;
    int https, ws;

    ASSERT(cf_parse_url("https://example.com/path", host, sizeof(host),
                        &port, &https, &ws) == 0);
    ASSERT(strcmp(host, "example.com") == 0);
    ASSERT(port == 443);
    ASSERT(https == 1);

    ASSERT(cf_parse_url("http://api.example.com:8080/", host, sizeof(host),
                        &port, &https, &ws) == 0);
    ASSERT(strcmp(host, "api.example.com") == 0);
    ASSERT(port == 8080);

    ASSERT(cf_parse_url("wss://ws.example.com/socket", host, sizeof(host),
                        &port, &https, &ws) == 0);
    ASSERT(ws == 1 && https == 1 && port == 443);

    cf_config *cfg = cf_config_create();
    ASSERT(cfg != NULL);
    ASSERT(cf_config_get_lru_capacity(cfg) == 500);
    ASSERT(cf_config_get_top_ips(cfg) == 3);
    cf_config_set_lru_capacity(cfg, 100);
    ASSERT(cf_config_get_lru_capacity(cfg) == 100);
    cf_config_destroy(cfg);

    return 0;
}
