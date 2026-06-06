/**
 * Phase 2: LD_PRELOAD / DLL shim intercepting libcurl easy interface.
 */
#include "internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

static cf_ctx *g_shim_ctx;
static void   *g_shim_mutex;

static void *cf_load_real(const char *sym)
{
#ifdef _WIN32
    static HMODULE h;
    if (!h) {
        const char *path = getenv("CURL_FO_LIBCURL_PATH");
        h = LoadLibraryA(path ? path : "libcurl.dll");
    }
    return h ? GetProcAddress(h, sym) : NULL;
#else
    static void *h;
    if (!h) {
        const char *path = getenv("CURL_FO_LIBCURL_PATH");
        h = dlopen(path ? path : "libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!h) h = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
    }
    return h ? dlsym(h, sym) : NULL;
#endif
}

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

/* ── Intercepted libcurl easy API ─────────────────────────────────────── */

typedef CURL *(*cf_real_init_t)(void);
typedef void (*cf_real_cleanup_t)(CURL *);
typedef CURLcode (*cf_real_setopt_t)(CURL *, CURLoption, ...);
typedef CURLcode (*cf_real_perform_t)(CURL *);
typedef CURLcode (*cf_real_getinfo_t)(CURL *, CURLINFO, ...);

#ifdef _WIN32
#  define CF_SHIM_EXPORT __declspec(dllexport)
#else
#  define CF_SHIM_EXPORT __attribute__((visibility("default")))
#endif

CF_SHIM_EXPORT CURL *curl_easy_init(void)
{
    cf_real_init_t real = (cf_real_init_t)cf_load_real("curl_easy_init");
    if (!real) return NULL;
    CURL *curl = real();
    if (curl)
        cf_easy_attach(curl);
    return curl;
}

CF_SHIM_EXPORT void curl_easy_cleanup(CURL *curl)
{
    cf_shadow_free(curl);
    cf_real_cleanup_t real = (cf_real_cleanup_t)cf_load_real("curl_easy_cleanup");
    if (real) real(curl);
}

CF_SHIM_EXPORT CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
    va_list ap;
    va_start(ap, option);

    cf_real_setopt_t real = (cf_real_setopt_t)cf_load_real("curl_easy_setopt");
    if (!real) {
        va_end(ap);
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }

    /* Track URL and headers for failover layer */
    if (option == CURLOPT_URL) {
        const char *url = va_arg(ap, const char *);
        va_end(ap);
        cf_shadow_set_url(curl, url);
        return real(curl, option, url);
    }
    if (option == CURLOPT_HTTPHEADER) {
        struct curl_slist *hdrs = va_arg(ap, struct curl_slist *);
        va_end(ap);
        cf_shadow_set_headers(curl, hdrs);
        return real(curl, option, hdrs);
    }
    if (option == CURLOPT_HTTPGET) {
        long v = va_arg(ap, long);
        va_end(ap);
        if (v) cf_shadow_set_method(curl, CF_METHOD_GET);
        return real(curl, option, v);
    }
    if (option == CURLOPT_NOBODY) {
        long v = va_arg(ap, long);
        va_end(ap);
        if (v) cf_shadow_set_method(curl, CF_METHOD_HEAD);
        return real(curl, option, v);
    }
    if (option == CURLOPT_CUSTOMREQUEST) {
        const char *m = va_arg(ap, const char *);
        va_end(ap);
        if (m) {
            if (strcmp(m, "GET") == 0) cf_shadow_set_method(curl, CF_METHOD_GET);
            else if (strcmp(m, "HEAD") == 0) cf_shadow_set_method(curl, CF_METHOD_HEAD);
            else cf_shadow_set_method(curl, CF_METHOD_OTHER);
        }
        return real(curl, option, m);
    }

    /* Generic forward — pointer-sized argument (common shim pattern) */
    void *ptr = va_arg(ap, void *);
    va_end(ap);
    return real(curl, option, ptr);
}

CF_SHIM_EXPORT CURLcode curl_easy_perform(CURL *curl)
{
    return cf_easy_perform(cf_shim_ctx(), curl);
}

CF_SHIM_EXPORT CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
    va_list ap;
    va_start(ap, info);
    void *param = va_arg(ap, void *);
    va_end(ap);

    cf_real_getinfo_t real = (cf_real_getinfo_t)cf_load_real("curl_easy_getinfo");
    if (!real) return CURLE_BAD_FUNCTION_ARGUMENT;
    return real(curl, info, param);
}
