/**
 * Phase 3: libcurl-compatible wrapper library.
 *
 * Applications link -lcurl_fo instead of -lcurl.  Un-hooked symbols are
 * forwarded to the real libcurl loaded at runtime.
 */
#include "internal.h"

#undef curl_easy_init
#undef curl_easy_cleanup
#undef curl_easy_setopt
#undef curl_easy_perform
#undef curl_easy_getinfo

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define CF_DLOPEN(path) LoadLibraryA(path)
#  define CF_DLSYM(h, s)  GetProcAddress((HMODULE)(h), s)
#  define CF_DLLHANDLE HMODULE
#else
#  include <dlfcn.h>
#  define CF_DLOPEN(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#  define CF_DLSYM(h, s)  dlsym(h, s)
#  define CF_DLLHANDLE void *
#endif

static CF_DLLHANDLE g_libcurl;

static void cf_wrapper_load(void)
{
    if (g_libcurl) return;
    const char *path = getenv("CURL_FO_LIBCURL_PATH");
#ifdef _WIN32
    g_libcurl = LoadLibraryA(path ? path : "libcurl.dll");
#else
    g_libcurl = dlopen(path ? path : "libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!g_libcurl)
        g_libcurl = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

#define CF_FORWARD(ret, name, args, call)          \
    ret name args {                                \
        cf_wrapper_load();                         \
        typedef ret (*fn_t) args;                  \
        static fn_t fn;                            \
        if (!fn) fn = (fn_t)CF_DLSYM(g_libcurl, #name); \
        return fn ? fn call : (ret)0;              \
    }

#define CF_FORWARD_VOID(name, args, call)          \
    void name args {                               \
        cf_wrapper_load();                         \
        typedef void (*fn_t) args;                 \
        static fn_t fn;                            \
        if (!fn) fn = (fn_t)CF_DLSYM(g_libcurl, #name); \
        if (fn) fn call;                           \
    }

/* ── Intercepted symbols ─────────────────────────────────────────────── */

CF_EXPORT CURL *curl_easy_init(void)
{
    cf_wrapper_load();
    typedef CURL *(*fn_t)(void);
    static fn_t real;
    if (!real) real = (fn_t)CF_DLSYM(g_libcurl, "curl_easy_init");
    CURL *c = real ? real() : NULL;
    if (c) cf_easy_attach(c);
    return c;
}

CF_EXPORT void curl_easy_cleanup(CURL *curl)
{
    cf_shadow_free(curl);
    cf_wrapper_load();
    typedef void (*fn_t)(CURL *);
    static fn_t real;
    if (!real) real = (fn_t)CF_DLSYM(g_libcurl, "curl_easy_cleanup");
    if (real) real(curl);
}

CF_EXPORT CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
    va_list ap;
    va_start(ap, option);
    cf_wrapper_load();
    typedef CURLcode (*fn_t)(CURL *, CURLoption, ...);
    static fn_t real;
    if (!real) real = (fn_t)CF_DLSYM(g_libcurl, "curl_easy_setopt");

    if (option == CURLOPT_URL) {
        const char *url = va_arg(ap, const char *);
        va_end(ap);
        cf_shadow_set_url(curl, url);
        return real ? real(curl, option, url) : CURLE_BAD_FUNCTION_ARGUMENT;
    }
    if (option == CURLOPT_HTTPHEADER) {
        struct curl_slist *h = va_arg(ap, struct curl_slist *);
        va_end(ap);
        cf_shadow_set_headers(curl, h);
        return real ? real(curl, option, h) : CURLE_BAD_FUNCTION_ARGUMENT;
    }
    if (option == CURLOPT_HTTPGET) {
        long v = va_arg(ap, long);
        va_end(ap);
        if (v) cf_shadow_set_method(curl, CF_METHOD_GET);
        return real ? real(curl, option, v) : CURLE_BAD_FUNCTION_ARGUMENT;
    }
    if (option == CURLOPT_NOBODY) {
        long v = va_arg(ap, long);
        va_end(ap);
        if (v) cf_shadow_set_method(curl, CF_METHOD_HEAD);
        return real ? real(curl, option, v) : CURLE_BAD_FUNCTION_ARGUMENT;
    }
    void *ptr = va_arg(ap, void *);
    va_end(ap);
    return real ? real(curl, option, ptr) : CURLE_BAD_FUNCTION_ARGUMENT;
}

CF_EXPORT CURLcode curl_easy_perform(CURL *curl)
{
    return cf_easy_perform(cf_shim_ctx(), curl);
}

/* ── Forwarded symbols (core libcurl API) ────────────────────────────── */

CF_FORWARD(CURLcode, curl_global_init, (long flags), (flags))
CF_FORWARD_VOID(curl_global_cleanup, (void), ())
CF_FORWARD(char *, curl_version, (void), ())
CF_EXPORT CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
    va_list ap;
    va_start(ap, info);
    void *param = va_arg(ap, void *);
    va_end(ap);
    cf_wrapper_load();
    typedef CURLcode (*fn_t)(CURL *, CURLINFO, void *);
    static fn_t real;
    if (!real) real = (fn_t)CF_DLSYM(g_libcurl, "curl_easy_getinfo");
    return real ? real(curl, info, param) : CURLE_BAD_FUNCTION_ARGUMENT;
}
CF_FORWARD(CURLcode, curl_easy_pause, (CURL *c, int bits), (c, bits))
CF_FORWARD_VOID(curl_easy_reset, (CURL *c), (c))
CF_FORWARD(CURL *, curl_easy_duphandle, (CURL *c), (c))
CF_FORWARD(CURLM *, curl_multi_init, (void), ())
CF_FORWARD(CURLMcode, curl_multi_add_handle, (CURLM *m, CURL *c), (m, c))
CF_FORWARD(CURLMcode, curl_multi_remove_handle, (CURLM *m, CURL *c), (m, c))
CF_FORWARD(CURLMcode, curl_multi_poll, (CURLM *m, struct curl_waitfd *e, unsigned n, int t, int *rc), (m, e, n, t, rc))
CF_FORWARD(CURLMcode, curl_multi_perform, (CURLM *m, int *rc), (m, rc))
CF_FORWARD(CURLMcode, curl_multi_wait, (CURLM *m, struct curl_waitfd *e, unsigned n, int t, int *rc), (m, e, n, t, rc))
CF_FORWARD(int, curl_multi_fdset, (CURLM *m, fd_set *r, fd_set *w, fd_set *e, int *max), (m, r, w, e, max))
CF_FORWARD(CURLMcode, curl_multi_cleanup, (CURLM *m), (m))
CF_FORWARD(struct curl_slist *, curl_slist_append, (struct curl_slist *l, const char *s), (l, s))
CF_FORWARD_VOID(curl_slist_free_all, (struct curl_slist *l), (l))
CF_FORWARD(const char *, curl_easy_strerror, (CURLcode c), (c))
CF_FORWARD(curl_version_info_data *, curl_version_info, (CURLversion v), (v))
CF_FORWARD(CURLSH *, curl_share_init, (void), ())
CF_FORWARD(CURLSHcode, curl_share_cleanup, (CURLSH *s), (s))
CF_FORWARD(CURLcode, curl_easy_recv, (CURL *c, void *b, size_t l, size_t *n), (c, b, l, n))
CF_FORWARD(CURLcode, curl_easy_send, (CURL *c, const void *b, size_t l, size_t *n), (c, b, l, n))
CF_FORWARD(CURLcode, curl_ws_recv, (CURL *c, void *b, size_t l, size_t *n, const struct curl_ws_frame **f), (c, b, l, n, f))
CF_FORWARD(CURLcode, curl_ws_send, (CURL *c, const void *b, size_t l, size_t *n, curl_off_t fr, unsigned flags), (c, b, l, n, fr, flags))
