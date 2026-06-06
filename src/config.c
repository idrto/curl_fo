#include "internal.h"

#include <stdlib.h>
#include <string.h>


#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#define CF_DEFAULT_LRU          500
#define CF_DEFAULT_TOP_IPS      3
#define CF_DEFAULT_GET_MS       15000L
#define CF_DEFAULT_OTHER_MS     60000L
#define CF_DEFAULT_CONNECT_MS   3000L
#define CF_DEFAULT_HEADER       "X-Curl-FO-Id"
#define CF_DEFAULT_BUCKET_MS    10
#define CF_DEFAULT_TTL          300

cf_config *cf_config_create(void)
{
    cf_config *cfg = calloc(1, sizeof(cf_config));
    if (!cfg)
        return NULL;
    cfg->lru_capacity = CF_DEFAULT_LRU;
    cfg->top_ips = CF_DEFAULT_TOP_IPS;
    cfg->get_timeout_ms = CF_DEFAULT_GET_MS;
    cfg->other_timeout_ms = CF_DEFAULT_OTHER_MS;
    cfg->connect_timeout_ms = CF_DEFAULT_CONNECT_MS;
    cfg->idempotency_header = strdup(CF_DEFAULT_HEADER);
    cfg->latency_bucket_ms = CF_DEFAULT_BUCKET_MS;
    cfg->default_ttl_sec = CF_DEFAULT_TTL;
    return cfg;
}

void cf_config_destroy(cf_config *cfg)
{
    if (!cfg) return;
    free(cfg->idempotency_header);
    free(cfg);
}

void cf_config_set_lru_capacity(cf_config *cfg, size_t capacity)
{
    if (cfg && capacity > 0)
        cfg->lru_capacity = capacity;
}

void cf_config_set_top_ips(cf_config *cfg, size_t top_ips)
{
    if (cfg && top_ips > 0)
        cfg->top_ips = top_ips;
}

void cf_config_set_get_timeout_ms(cf_config *cfg, long ms)
{
    if (cfg && ms > 0)
        cfg->get_timeout_ms = ms;
}

void cf_config_set_other_timeout_ms(cf_config *cfg, long ms)
{
    if (cfg && ms > 0)
        cfg->other_timeout_ms = ms;
}

void cf_config_set_connect_timeout_ms(cf_config *cfg, long ms)
{
    if (cfg && ms > 0)
        cfg->connect_timeout_ms = ms;
}

void cf_config_set_idempotency_header(cf_config *cfg, const char *name)
{
    if (!cfg || !name || !*name) return;
    free(cfg->idempotency_header);
    cfg->idempotency_header = strdup(name);
}

void cf_config_set_latency_bucket_ms(cf_config *cfg, unsigned bucket_ms)
{
    if (cfg && bucket_ms > 0)
        cfg->latency_bucket_ms = bucket_ms;
}

void cf_config_set_default_ttl_sec(cf_config *cfg, unsigned ttl_sec)
{
    if (cfg && ttl_sec > 0)
        cfg->default_ttl_sec = ttl_sec;
}

void cf_config_set_verbose(cf_config *cfg, int on)
{
    if (cfg)
        cfg->verbose = on ? 1 : 0;
}

int cf_config_get_verbose(const cf_config *cfg)
{
    return cfg && cfg->verbose;
}

void cf_config_set_failover_gateway(cf_config *cfg, int on)
{
    if (cfg)
        cfg->failover_gateway = on ? 1 : 0;
}

int cf_config_get_failover_gateway(const cf_config *cfg)
{
    return cfg && cfg->failover_gateway;
}

size_t cf_config_get_lru_capacity(const cf_config *cfg)
{
    return cfg ? cfg->lru_capacity : CF_DEFAULT_LRU;
}

size_t cf_config_get_top_ips(const cf_config *cfg)
{
    return cfg ? cfg->top_ips : CF_DEFAULT_TOP_IPS;
}

long cf_config_get_get_timeout_ms(const cf_config *cfg)
{
    return cfg ? cfg->get_timeout_ms : CF_DEFAULT_GET_MS;
}

long cf_config_get_other_timeout_ms(const cf_config *cfg)
{
    return cfg ? cfg->other_timeout_ms : CF_DEFAULT_OTHER_MS;
}

long cf_config_get_connect_timeout_ms(const cf_config *cfg)
{
    return cfg ? cfg->connect_timeout_ms : CF_DEFAULT_CONNECT_MS;
}

const char *cf_config_get_idempotency_header(const cf_config *cfg)
{
    return cfg && cfg->idempotency_header ? cfg->idempotency_header
                                          : CF_DEFAULT_HEADER;
}

static long cf_env_long(const char *name, long def)
{
    const char *v = getenv(name);
    if (!v || !*v) return def;
    return atol(v);
}

void cf_config_load_env(cf_config *cfg)
{
    if (!cfg) return;
    long v;
    v = cf_env_long("CURL_FO_LRU_SIZE", (long)cfg->lru_capacity);
    if (v > 0) cfg->lru_capacity = (size_t)v;
    v = cf_env_long("CURL_FO_TOP_IPS", (long)cfg->top_ips);
    if (v > 0) cfg->top_ips = (size_t)v;
    v = cf_env_long("CURL_FO_GET_TIMEOUT_MS", cfg->get_timeout_ms);
    if (v > 0) cfg->get_timeout_ms = v;
    v = cf_env_long("CURL_FO_OTHER_TIMEOUT_MS", cfg->other_timeout_ms);
    if (v > 0) cfg->other_timeout_ms = v;
    v = cf_env_long("CURL_FO_CONNECT_TIMEOUT_MS", cfg->connect_timeout_ms);
    if (v > 0) cfg->connect_timeout_ms = v;
    v = cf_env_long("CURL_FO_LATENCY_BUCKET_MS", (long)cfg->latency_bucket_ms);
    if (v > 0) cfg->latency_bucket_ms = (unsigned)v;
    v = cf_env_long("CURL_FO_DEFAULT_TTL_SEC", (long)cfg->default_ttl_sec);
    if (v > 0) cfg->default_ttl_sec = (unsigned)v;
    const char *hdr = getenv("CURL_FO_IDEMPOTENCY_HEADER");
    if (hdr && *hdr)
        cf_config_set_idempotency_header(cfg, hdr);
    const char *verb = getenv("CURL_FO_VERBOSE");
    if (verb && *verb && verb[0] != '0')
        cfg->verbose = 1;
    const char *gw = getenv("CURL_FO_FAILOVER_GATEWAY");
    if (gw && *gw && gw[0] != '0')
        cfg->failover_gateway = 1;
}
