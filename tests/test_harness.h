#ifndef CURL_FO_TEST_HARNESS_H
#define CURL_FO_TEST_HARNESS_H

#include <stdlib.h>
#include <string.h>

void cf_test_assert(const char *file, int line, const char *expr, int ok);
#define ASSERT(expr) cf_test_assert(__FILE__, __LINE__, #expr, (expr))

#endif
