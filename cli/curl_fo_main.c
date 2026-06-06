/**
 * curl-fo — CLI with curl_fo failover behaviour.
 */
#include "curl_fo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <io.h>
#  define CF_FILENO _fileno
#  define CF_ISATTY _isatty
#else
#  include <unistd.h>
#  define CF_FILENO fileno
#  define CF_ISATTY isatty
#endif

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <URL>\n"
        "\n"
        "Options:\n"
        "  -X, --request METHOD   HTTP method (default GET)\n"
        "  -H, --header LINE      Add request header\n"
        "  -d, --data DATA        POST body\n"
        "  -o, --output FILE      Write response body to FILE\n"
        "  -i, --include          Include response headers in output\n"
        "  -s, --silent           Silent mode\n"
        "  -v, --verbose          Verbose libcurl output\n"
        "  -L, --location         Follow redirects\n"
        "  --connect-timeout SEC  Connect timeout\n"
        "  -m, --max-time SEC     Total timeout override\n"
        "  -h, --help             Show help\n"
        "\n"
        "curl_fo adds DNS latency-ranked IP failover transparently.\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *url = NULL;
    const char *method = "GET";
    const char *data = NULL;
    const char *output = NULL;
    struct curl_slist *headers = NULL;
    int include_headers = 0;
    int verbose = 0;
    int silent = 0;
    int follow = 0;
    long connect_timeout = 0;
    long max_time = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-X") == 0 || strcmp(argv[i], "--request") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            method = argv[i];
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--header") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            headers = curl_slist_append(headers, argv[i]);
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            data = argv[i];
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            output = argv[i];
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include") == 0) {
            include_headers = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0) {
            silent = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--location") == 0) {
            follow = 1;
        } else if (strcmp(argv[i], "--connect-timeout") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            connect_timeout = atol(argv[i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-time") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            max_time = atol(argv[i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        usage(argv[0]);
        return 2;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    cf_config *cfg = cf_config_create();
    cf_config_load_env(cfg);
    cf_ctx *ctx = cf_ctx_create(cfg);

    CURL *curl = curl_easy_init();
    cf_easy_attach(curl);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (strcmp(method, "GET") == 0)
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    else if (strcmp(method, "HEAD") == 0)
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    else
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    if (data) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    }

    if (output) {
        FILE *fp = fopen(output, "wb");
        if (!fp) {
            perror(output);
            return 1;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    } else if (!silent) {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    }

    if (include_headers)
        curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    if (verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    if (follow)
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (connect_timeout > 0)
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    if (max_time > 0)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, max_time);

    CURLcode rc = cf_easy_perform(ctx, curl);

    if (!silent && output) {
        /* body written to file */
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (!silent && rc != CURLE_OK)
        fprintf(stderr, "curl-fo: %s\n", curl_easy_strerror(rc));

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    cf_ctx_destroy(ctx);
    curl_global_cleanup();

    if (rc != CURLE_OK)
        return 1;
    if (http_code >= 400)
        return (int)http_code;
    return 0;
}
