#include "internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

typedef CURLcode (*cf_curl_perform_fn)(CURL *);
typedef CURLcode (*cf_curl_setopt_fn)(CURL *, CURLoption, ...);
typedef CURLcode (*cf_curl_getinfo_fn)(CURL *, CURLINFO, ...);
typedef CURL *(*cf_curl_init_fn)(void);
typedef void (*cf_curl_cleanup_fn)(CURL *);
typedef struct curl_slist *(*cf_curl_slist_append_fn)(struct curl_slist *, const char *);
typedef void (*cf_curl_slist_free_fn)(struct curl_slist *);
typedef CURLcode (*cf_curl_ws_send_fn)(CURL *, const void *, size_t, size_t *,
                                       curl_off_t, unsigned);
typedef CURLcode (*cf_curl_ws_recv_fn)(CURL *, void *, size_t, size_t *,
                                       const struct curl_ws_frame **);
typedef curl_version_info_data *(*cf_curl_version_info_fn)(CURLversion);
typedef const char *(*cf_curl_strerror_fn)(CURLcode);

static cf_curl_perform_fn      p_perform;
static cf_curl_setopt_fn       p_setopt;
static cf_curl_getinfo_fn      p_getinfo;
static cf_curl_init_fn         p_init;
static cf_curl_cleanup_fn      p_cleanup;
static cf_curl_slist_append_fn p_slist_append;
static cf_curl_slist_free_fn   p_slist_free;
static cf_curl_ws_send_fn      p_ws_send;
static cf_curl_ws_recv_fn      p_ws_recv;
static cf_curl_version_info_fn p_version_info;
static cf_curl_strerror_fn     p_strerror;
static int                     dispatch_ready;

#if defined(CURL_FO_SHIM_BUILD) || defined(CURL_FO_WRAPPER_BUILD)
static void *g_curl_lib;

static void *cf_dispatch_sym(const char *name)
{
    if (!g_curl_lib) {
        const char *path = getenv("CURL_FO_LIBCURL_PATH");
#  ifdef _WIN32
        g_curl_lib = LoadLibraryA(path ? path : "libcurl-4.dll");
#  else
        g_curl_lib = dlopen(path ? path : "libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!g_curl_lib)
            g_curl_lib = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
#  endif
    }
    if (!g_curl_lib)
        return NULL;
#  ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)g_curl_lib, name);
#  else
    return dlsym(g_curl_lib, name);
#  endif
}

static void cf_dispatch_init(void)
{
    if (dispatch_ready)
        return;
    p_perform      = (cf_curl_perform_fn)cf_dispatch_sym("curl_easy_perform");
    p_setopt       = (cf_curl_setopt_fn)cf_dispatch_sym("curl_easy_setopt");
    p_getinfo      = (cf_curl_getinfo_fn)cf_dispatch_sym("curl_easy_getinfo");
    p_init         = (cf_curl_init_fn)cf_dispatch_sym("curl_easy_init");
    p_cleanup      = (cf_curl_cleanup_fn)cf_dispatch_sym("curl_easy_cleanup");
    p_slist_append = (cf_curl_slist_append_fn)cf_dispatch_sym("curl_slist_append");
    p_slist_free   = (cf_curl_slist_free_fn)cf_dispatch_sym("curl_slist_free_all");
    p_ws_send      = (cf_curl_ws_send_fn)cf_dispatch_sym("curl_ws_send");
    p_ws_recv      = (cf_curl_ws_recv_fn)cf_dispatch_sym("curl_ws_recv");
    p_version_info = (cf_curl_version_info_fn)cf_dispatch_sym("curl_version_info");
    p_strerror     = (cf_curl_strerror_fn)cf_dispatch_sym("curl_easy_strerror");
    dispatch_ready = 1;
}
#else
static void cf_dispatch_init(void)
{
    if (dispatch_ready)
        return;
    p_perform      = curl_easy_perform;
    p_setopt       = curl_easy_setopt;
    p_getinfo      = curl_easy_getinfo;
    p_init         = curl_easy_init;
    p_cleanup      = curl_easy_cleanup;
    p_slist_append = curl_slist_append;
    p_slist_free   = curl_slist_free_all;
    p_ws_send      = curl_ws_send;
    p_ws_recv      = curl_ws_recv;
    p_version_info = curl_version_info;
    p_strerror     = curl_easy_strerror;
    dispatch_ready = 1;
}
#endif

static const char *cf_curl_code_str(CURLcode code)
{
    switch (code) {
    case CURLE_OK: return "No error";
    case CURLE_COULDNT_CONNECT: return "Couldn't connect to server";
    case CURLE_COULDNT_RESOLVE_HOST: return "Couldn't resolve host name";
    case CURLE_OPERATION_TIMEDOUT: return "Timeout was reached";
    case CURLE_UNSUPPORTED_PROTOCOL: return "Unsupported protocol";
    case CURLE_FAILED_INIT: return "Failed initialization";
    default: return "Unknown error";
    }
}

curl_version_info_data *cf_curl_version_info(CURLversion ver)
{
    cf_dispatch_init();
    return p_version_info ? p_version_info(ver) : NULL;
}

CURLcode cf_curl_easy_perform(CURL *curl)
{
    cf_dispatch_init();
    return p_perform ? p_perform(curl) : CURLE_FAILED_INIT;
}

CURLcode cf_curl_easy_setopt(CURL *curl, CURLoption opt, ...)
{
    va_list ap;
    va_start(ap, opt);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    cf_dispatch_init();
    return p_setopt ? p_setopt(curl, opt, arg) : CURLE_FAILED_INIT;
}

CURLcode cf_curl_easy_getinfo(CURL *curl, CURLINFO info, void *param)
{
    cf_dispatch_init();
    return p_getinfo ? p_getinfo(curl, info, param) : CURLE_FAILED_INIT;
}

CURL *cf_curl_easy_init(void)
{
    cf_dispatch_init();
    return p_init ? p_init() : NULL;
}

void cf_curl_easy_cleanup(CURL *curl)
{
    cf_dispatch_init();
    if (p_cleanup)
        p_cleanup(curl);
}

struct curl_slist *cf_curl_slist_append(struct curl_slist *list, const char *s)
{
    cf_dispatch_init();
    return p_slist_append ? p_slist_append(list, s) : NULL;
}

void cf_curl_slist_free_all(struct curl_slist *list)
{
    cf_dispatch_init();
    if (p_slist_free)
        p_slist_free(list);
}

CURLcode cf_curl_ws_send(CURL *curl, const void *buf, size_t len, size_t *sent,
                         curl_off_t fragsize, unsigned flags)
{
    cf_dispatch_init();
    return p_ws_send ? p_ws_send(curl, buf, len, sent, fragsize, flags)
                     : CURLE_UNSUPPORTED_PROTOCOL;
}

CURLcode cf_curl_ws_recv(CURL *curl, void *buf, size_t len, size_t *recvd,
                         const struct curl_ws_frame **meta)
{
    cf_dispatch_init();
    return p_ws_recv ? p_ws_recv(curl, buf, len, recvd, meta)
                     : CURLE_UNSUPPORTED_PROTOCOL;
}

const char *cf_curl_easy_strerror(CURLcode code)
{
    cf_dispatch_init();
    return p_strerror ? p_strerror(code) : cf_curl_code_str(code);
}
