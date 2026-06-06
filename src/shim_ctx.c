#include "internal.h"

static cf_ctx *g_shim_ctx;
static void   *g_shim_mutex;

cf_ctx *cf_shim_ctx(void)
{
    if (!g_shim_mutex)
        g_shim_mutex = cf_mutex_create();
    cf_mutex_lock(g_shim_mutex);
    if (!g_shim_ctx) {
        cf_config *cfg = cf_config_create();
        cf_config_load_env(cfg);
        g_shim_ctx = cf_ctx_create(cfg);
    }
    cf_mutex_unlock(g_shim_mutex);
    return g_shim_ctx;
}
