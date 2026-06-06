#include "curl_fo.h"
#include "test_harness.h"

int test_failover_run(void)
{
    /* Transport failure with no HTTP response → failover */
    ASSERT(cf_should_failover(CURLE_COULDNT_CONNECT, 0) == 1);
    ASSERT(cf_should_failover(CURLE_OPERATION_TIMEDOUT, 0) == 1);
    ASSERT(cf_should_failover(CURLE_SSL_CONNECT_ERROR, 0) == 1);

    /* HTTP response received → never failover */
    ASSERT(cf_should_failover(CURLE_OK, 200) == 0);
    ASSERT(cf_should_failover(CURLE_OK, 401) == 0);
    ASSERT(cf_should_failover(CURLE_OK, 403) == 0);
    ASSERT(cf_should_failover(CURLE_OK, 500) == 0);
    ASSERT(cf_should_failover(CURLE_OK, 503) == 0);

    /* libcurl error but HTTP code received (edge) */
    ASSERT(cf_should_failover(CURLE_RECV_ERROR, 500) == 0);

    cf_config *cfg = cf_config_create();
    ASSERT(cfg != NULL);
    cf_config_set_failover_gateway(cfg, 1);
    ASSERT(cf_config_get_failover_gateway(cfg) == 1);
    cf_config_destroy(cfg);

    return 0;
}
