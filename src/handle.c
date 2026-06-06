#include "internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#else
#  include <unistd.h>
#endif

typedef struct cf_curl_shadow {
    char              url[4096];
    int               has_url;
    cf_method         method;
    int               has_method;
    struct curl_slist  *headers;
    char              *postfields;
    char              proxy[4096];
    int               has_proxy;
    int               preconnected_fd;
    char              race_ip[64];
} cf_curl_shadow;

cf_curl_shadow *cf_shadow_get(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) != CURLE_OK || !s) {
        s = calloc(1, sizeof(cf_curl_shadow));
        if (!s) return NULL;
        s->preconnected_fd = -1;
        cf_curl_easy_setopt(curl, CURLOPT_PRIVATE, s);
    }
    return s;
}

void cf_shadow_set_url(CURL *curl, const char *url)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s || !url) return;
    strncpy(s->url, url, sizeof(s->url) - 1);
    s->has_url = 1;
}

const char *cf_shadow_get_url(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s && s->has_url)
        return s->url;
    char *eff = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK && eff)
        return eff;
    return NULL;
}

void cf_shadow_set_method(CURL *curl, cf_method m)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s) return;
    s->method = m;
    s->has_method = 1;
}

cf_method cf_shadow_get_method(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s && s->has_method)
        return s->method;
    return cf_detect_method(curl);
}

void cf_shadow_set_headers(CURL *curl, struct curl_slist *headers)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s) return;
    s->headers = headers;
}

struct curl_slist *cf_shadow_get_headers(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s)
        return s->headers;
    return NULL;
}

void cf_shadow_set_postfields(CURL *curl, const char *data)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s) return;
    free(s->postfields);
    s->postfields = data ? strdup(data) : NULL;
}

const char *cf_shadow_get_postfields(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s)
        return s->postfields;
    return NULL;
}

void cf_shadow_set_proxy(CURL *curl, const char *proxy)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s) return;
    if (proxy && proxy[0]) {
        strncpy(s->proxy, proxy, sizeof(s->proxy) - 1);
        s->has_proxy = 1;
    } else {
        s->proxy[0] = '\0';
        s->has_proxy = 0;
    }
}

const char *cf_shadow_get_proxy(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s && s->has_proxy)
        return s->proxy;
    return NULL;
}

void cf_shadow_free(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) == CURLE_OK && s) {
        cf_curl_easy_setopt(curl, CURLOPT_PRIVATE, NULL);
        free(s->postfields);
        free(s);
    }
}

void cf_easy_attach(CURL *curl)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (s)
        s->preconnected_fd = -1;
}

void cf_shadow_set_preconnected(CURL *curl, int fd, const char *ip)
{
    cf_curl_shadow *s = cf_shadow_get(curl);
    if (!s) return;
    s->preconnected_fd = fd;
    if (ip)
        strncpy(s->race_ip, ip, sizeof(s->race_ip) - 1);
    else
        s->race_ip[0] = '\0';
}

int cf_shadow_take_preconnected(CURL *curl, char *ip, size_t iplen)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) != CURLE_OK || !s)
        return -1;
    if (s->preconnected_fd < 0)
        return -1;
    int fd = s->preconnected_fd;
    s->preconnected_fd = -1;
    if (ip && iplen > 0 && s->race_ip[0]) {
        strncpy(ip, s->race_ip, iplen - 1);
        ip[iplen - 1] = '\0';
    }
    return fd;
}

curl_socket_t cf_shadow_opensocket_cb(void *clientp, curlsocktype purpose,
                                      struct curl_sockaddr *address)
{
    (void)purpose;
    (void)address;
    cf_curl_shadow *s = clientp;
    if (s && s->preconnected_fd >= 0) {
        curl_socket_t fd = (curl_socket_t)s->preconnected_fd;
        s->preconnected_fd = -1;
        return fd;
    }
    return CURL_SOCKET_BAD;
}

int cf_shadow_closesocket_cb(void *clientp, curl_socket_t item)
{
    (void)clientp;
#ifdef _WIN32
    closesocket((SOCKET)item);
#else
    close((int)item);
#endif
    return 0;
}
