#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct cf_curl_shadow {
    char              url[4096];
    int               has_url;
    cf_method         method;
    int               has_method;
    struct curl_slist  *headers;
    char              *postfields;
    char              proxy[4096];
    int               has_proxy;
} cf_curl_shadow;

cf_curl_shadow *cf_shadow_get(CURL *curl)
{
    cf_curl_shadow *s = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &s) != CURLE_OK || !s) {
        s = calloc(1, sizeof(cf_curl_shadow));
        if (!s) return NULL;
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
    cf_shadow_get(curl);
}
