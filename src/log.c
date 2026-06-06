#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void cf_vlog(cf_config *cfg, const char *fmt, ...)
{
    if (!cfg || !cfg->verbose || !fmt)
        return;
    fprintf(stderr, "[curl_fo] ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void cf_log_hex(cf_config *cfg, const uint8_t *data, size_t len)
{
    if (!cfg || !cfg->verbose || !data)
        return;
    size_t n = len < 64 ? len : 64;
    fputs("[curl_fo]   wire=", stderr);
    for (size_t i = 0; i < n; i++)
        fprintf(stderr, "%02x", data[i]);
    if (len > n)
        fputs("...", stderr);
    fputc('\n', stderr);
}

void cf_log_dns_query(cf_config *cfg, const char *host, const char *rtype,
                      const char *server, const uint8_t *wire, size_t wire_len,
                      uint16_t query_id)
{
    if (!cfg || !cfg->verbose)
        return;
    cf_vlog(cfg, "DNS UDP query id=0x%04x type=%s host=%s nameserver=%s:53\n",
            query_id, rtype ? rtype : "?", host ? host : "?", server ? server : "?");
    cf_log_hex(cfg, wire, wire_len);
}

void cf_log_dns_answer(cf_config *cfg, const char *addr, uint32_t ttl)
{
    if (!cfg || !cfg->verbose || !addr)
        return;
    cf_vlog(cfg, "DNS answer: %s ttl=%u\n", addr, ttl);
}

void cf_log_dns_fallback(cf_config *cfg, const char *host, const char *why)
{
    cf_vlog(cfg, "DNS fallback getaddrinfo for %s (%s)\n", host, why ? why : "UDP failed");
}

void cf_log_dns_summary(cf_config *cfg, const char *host, cf_dns_result *res,
                        const char *source)
{
    if (!cfg || !cfg->verbose || !res)
        return;
    cf_vlog(cfg, "DNS resolved %s via %s: %zu address(es) ttl=%u\n",
            host, source ? source : "?", res->count, res->ttl_sec);
    for (size_t i = 0; i < res->count; i++)
        cf_vlog(cfg, "  %zu: %s\n", i + 1, res->addrs[i]);
}

void cf_log_probe_start(cf_config *cfg, const char *host, uint16_t port,
                        size_t count, unsigned bucket_ms)
{
    cf_vlog(cfg, "latency probe %s:%u — %zu candidate IP(s), bucket=%ums\n",
            host, port, count, bucket_ms);
}

void cf_log_probe_ip(cf_config *cfg, const char *addr, int ok,
                      unsigned raw_ms, unsigned bucket_ms)
{
    if (!ok)
        cf_vlog(cfg, "  probe %s: unreachable\n", addr);
    else
        cf_vlog(cfg, "  probe %s: raw=%ums bucket=%ums\n", addr, raw_ms, bucket_ms);
}

void cf_log_probe_ranking(cf_config *cfg, cf_ip_rank *ranks, size_t count)
{
    if (!cfg || !cfg->verbose || !ranks || count == 0)
        return;
    cf_vlog(cfg, "ranked IPs (lowest latency first):\n");
    for (size_t i = 0; i < count; i++)
        cf_vlog(cfg, "  #%zu %s raw=%ums bucket=%ums\n",
                i + 1, ranks[i].addr, ranks[i].raw_ms, ranks[i].bucket_ms);
}

static void cf_log_shell_quote(const char *s, FILE *out)
{
    if (!s) {
        fputs("''", out);
        return;
    }
    fputc('\'', out);
    while (*s) {
        if (*s == '\'')
            fputs("'\\''", out);
        else
            fputc(*s, out);
        s++;
    }
    fputc('\'', out);
}

static const char *cf_method_string(cf_method method)
{
    switch (method) {
    case CF_METHOD_GET:  return "GET";
    case CF_METHOD_HEAD: return "HEAD";
    default:             return NULL;
    }
}

void cf_log_curl_replay(cf_config *cfg, CURL *curl, const char *host,
                        uint16_t port, const char *ip, const char *req_id,
                        cf_method method, long timeout_ms, long connect_ms,
                        int connect_only)
{
    if (!cfg || !cfg->verbose)
        return;

    const char *url = cf_shadow_get_url(curl);
    const char *hdr_name = cf_config_get_idempotency_header(cfg);
    struct curl_slist *headers = cf_shadow_get_headers(curl);
    const char *post = cf_shadow_get_postfields(curl);
    const char *method_name = cf_method_string(method);

    cf_vlog(cfg, "idempotency header: %s: %s\n", hdr_name, req_id);

    fputs("[curl_fo] replayable curl command:\n[curl_fo]   curl -v", stderr);

    char resolve[384];
    snprintf(resolve, sizeof(resolve), "%s:%u:%s", host, port, ip);
    fputs(" --resolve ", stderr);
    cf_log_shell_quote(resolve, stderr);

    fputs(" -H ", stderr);
    char id_line[160];
    snprintf(id_line, sizeof(id_line), "%s: %s", hdr_name, req_id);
    cf_log_shell_quote(id_line, stderr);

    for (struct curl_slist *h = headers; h; h = h->next) {
        fputs(" -H ", stderr);
        cf_log_shell_quote(h->data, stderr);
    }

    if (method == CF_METHOD_HEAD)
        fputs(" -I", stderr);
    else if (method_name)
        fprintf(stderr, " -X %s", method_name);
    else if (method == CF_METHOD_OTHER) {
        const char *custom = NULL;
        if (cf_curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_METHOD, &custom) == CURLE_OK
            && custom && *custom)
            fprintf(stderr, " -X %s", custom);
    }

    if (post && *post) {
        fputs(" -d ", stderr);
        cf_log_shell_quote(post, stderr);
    }

    if (connect_ms > 0)
        fprintf(stderr, " --connect-timeout %ld", connect_ms / 1000L);
    if (timeout_ms > 0)
        fprintf(stderr, " -m %ld", (timeout_ms + 999L) / 1000L);
    if (connect_only)
        fputs(" --connect-only", stderr);

    if (url) {
        fputs(" ", stderr);
        cf_log_shell_quote(url, stderr);
    }
    fputc('\n', stderr);
}
