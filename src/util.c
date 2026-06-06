#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#endif

uint64_t cf_now_ms(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (uli.QuadPart / 10000ULL) - 11644473600000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

void cf_generate_uuid(char *buf, size_t buflen)
{
    static int seeded;
    if (!seeded) {
        srand((unsigned)cf_now_ms() ^ (unsigned)(uintptr_t)buf);
        seeded = 1;
    }
    snprintf(buf, buflen,
             "%08x-%04x-%4x-%04x-%012llx",
             (unsigned)rand(), (unsigned)(rand() & 0xFFFF),
             (unsigned)((rand() & 0x0FFF) | 0x4000),
             (unsigned)((rand() & 0x3FFF) | 0x8000),
             (unsigned long long)((rand() & 0xFFFFFFFFFFFFULL)));
}

int cf_parse_url(const char *url, char *host, size_t hostlen,
                 uint16_t *port, int *is_https, int *is_ws)
{
    const char *p = url;
    *is_https = 0;
    *is_ws = 0;
    *port = 80;

    if (strncmp(p, "https://", 8) == 0) {
        *is_https = 1;
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "wss://", 6) == 0) {
        *is_https = 1;
        *is_ws = 1;
        *port = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        *is_ws = 1;
        *port = 80;
        p += 5;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *at = strchr(p, '@');
    const char *end = slash ? slash : p + strlen(p);

    if (at && at < end)
        p = at + 1;

    const char *colon = memchr(p, ':', (size_t)(end - p));
    size_t hlen;
    if (colon && colon < end) {
        hlen = (size_t)(colon - p);
        int prt = atoi(colon + 1);
        if (prt > 0 && prt <= 65535)
            *port = (uint16_t)prt;
    } else {
        hlen = (size_t)(end - p);
    }

    if (hlen == 0 || hlen >= hostlen)
        return -1;

    memcpy(host, p, hlen);
    host[hlen] = '\0';

    if (host[0] == '[') {
        char *br = strchr(host, ']');
        if (br) {
            *br = '\0';
            memmove(host, host + 1, strlen(host));
        }
    }
    return 0;
}

uint16_t cf_default_port(int is_https, int is_ws)
{
    (void)is_ws;
    return is_https ? 443 : 80;
}

int cf_addr_in_list(const char *addr, char **list, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(addr, list[i]) == 0)
            return 1;
    }
    return 0;
}

void cf_shuffle_tied(cf_ip_rank *ranks, size_t count, unsigned bucket_ms)
{
    for (size_t i = 0; i < count; ) {
        size_t j = i + 1;
        while (j < count && ranks[j].bucket_ms == ranks[i].bucket_ms)
            j++;
        if (ranks[i].bucket_ms == bucket_ms && j - i > 1) {
            for (size_t a = i; a < j - 1; a++) {
                size_t b = a + (size_t)(rand() % (int)(j - a));
                cf_ip_rank tmp = ranks[a];
                ranks[a] = ranks[b];
                ranks[b] = tmp;
            }
        }
        i = j;
    }
}

void *cf_mutex_create(void)
{
#ifdef _WIN32
    CRITICAL_SECTION *cs = malloc(sizeof(CRITICAL_SECTION));
    if (cs)
        InitializeCriticalSection(cs);
    return cs;
#else
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (m)
        pthread_mutex_init(m, NULL);
    return m;
#endif
}

void cf_mutex_lock(void *m)
{
    if (!m) return;
#ifdef _WIN32
    EnterCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_lock((pthread_mutex_t *)m);
#endif
}

void cf_mutex_unlock(void *m)
{
    if (!m) return;
#ifdef _WIN32
    LeaveCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_unlock((pthread_mutex_t *)m);
#endif
}

void cf_mutex_destroy(void *m)
{
    if (!m) return;
#ifdef _WIN32
    DeleteCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_destroy((pthread_mutex_t *)m);
#endif
    free(m);
}

int cf_should_failover(CURLcode code, long http_code)
{
    if (http_code > 0)
        return 0;
    return code != CURLE_OK;
}

cf_method cf_detect_method(CURL *curl)
{
    const char *method = "GET";
    if (cf_curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_METHOD, &method) != CURLE_OK
        || !method)
        return CF_METHOD_UNKNOWN;
#ifdef _WIN32
    if (_stricmp(method, "GET") == 0) return CF_METHOD_GET;
    if (_stricmp(method, "HEAD") == 0) return CF_METHOD_HEAD;
#else
    if (strcasecmp(method, "GET") == 0) return CF_METHOD_GET;
    if (strcasecmp(method, "HEAD") == 0) return CF_METHOD_HEAD;
#endif
    return CF_METHOD_OTHER;
}
