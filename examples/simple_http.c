#include <curl/curl.h>
#include <curl_fo.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "https://example.com";

    curl_global_init(CURL_GLOBAL_DEFAULT);
    cf_ctx *ctx = cf_ctx_create_default();

    CURL *curl = curl_easy_init();
    cf_easy_attach(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = cf_easy_perform(ctx, curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    printf("result=%s http=%ld\n", curl_easy_strerror(rc), code);

    curl_easy_cleanup(curl);
    cf_ctx_destroy(ctx);
    curl_global_cleanup();
    return rc == CURLE_OK ? 0 : 1;
}
