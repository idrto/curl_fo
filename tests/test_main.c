#include <stdio.h>
#include <stdlib.h>

extern int test_util_run(void);
extern int test_failover_run(void);
extern int test_cache_run(void);
extern int test_probe_run(void);
extern int test_dns_run(void);
extern int test_ttl_run(void);

static int g_failures;

void cf_test_assert(const char *file, int line, const char *expr, int ok)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        g_failures++;
    }
}

#define ASSERT(expr) cf_test_assert(__FILE__, __LINE__, #expr, (expr))

int main(void)
{
    test_util_run();
    test_failover_run();
    test_probe_run();
    test_dns_run();
    test_cache_run();
    test_ttl_run();

    if (g_failures) {
        fprintf(stderr, "\n%d test(s) failed.\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
