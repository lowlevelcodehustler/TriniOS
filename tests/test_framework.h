/*
 * TriniOS Test Framework
 * ======================
 * Lightweight unit test framework for TriniOS.
 *
 * Design:
 *  - Each test is a void test_func(void) function
 *  - Tests are registered via TEST() macro, invoked via TEST_REGISTER() in main
 *  - Assertions: TEST_ASSERT, TEST_ASSERT_EQ, TEST_ASSERT_NULL, TEST_ASSERT_NOT_NULL,
 *                TEST_ASSERT_STR_EQ, TEST_ASSERT_TRUE, TEST_ASSERT_FALSE
 *  - Setup/teardown via TEST_SETUP/TEST_TEARDOWN
 *  - Results: pass/fail count, per-test output
 *  - Exit code: 0 if all pass, 1 otherwise
 *
 * Usage:
 *   TEST(my_test) {
 *       TEST_ASSERT_EQ(1, 1);
 *   }
 *
 *   int main(void) {
 *       TEST_REGISTER(my_test);
 *       test_run_all();
 *       return test_summary();
 *   }
 */

#ifndef TRINIOS_TEST_FRAMEWORK_H
#define TRINIOS_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_TESTS 256
#define MAX_NAME_LEN 128

/* ---------- internal state ---------- */

extern int test_total;
extern int test_passed;
extern int test_failed;
extern int test_current;

typedef void (*test_func_t)(void);
typedef void (*setup_func_t)(void);

typedef struct {
    char name[MAX_NAME_LEN];
    test_func_t func;
} test_case_t;

extern test_case_t test_registry[MAX_TESTS];
extern int test_registry_count;
extern setup_func_t test_setup_fn;
extern setup_func_t test_teardown_fn;

/* ---------- registration macros ---------- */

#define TEST(test_name) \
    static void test_##test_name(void); \
    static void test_register_##test_name(void) { \
        if (test_registry_count < MAX_TESTS) { \
            strncpy(test_registry[test_registry_count].name, #test_name, MAX_NAME_LEN - 1); \
            test_registry[test_registry_count].func = test_##test_name; \
            test_registry_count++; \
        } \
    } \
    static void test_##test_name(void)

#define TEST_REGISTER(name) test_register_##name()

#define TEST_SETUP(setup_name) \
    static void test_setup_##setup_name(void); \
    static void test_setup_##setup_name(void)

#define TEST_TEARDOWN(teardown_name) \
    static void test_teardown_##teardown_name(void); \
    static void test_teardown_##teardown_name(void)

/* ---------- assertion macros ---------- */

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s\n", __FILE__, __LINE__, #expr); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s != %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s == %s (%lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a)); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "  FAIL: %s (line %d): expected NULL for %s\n", \
                __FILE__, __LINE__, #ptr); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s is NULL\n", \
                __FILE__, __LINE__, #ptr); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s (line %d): \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_STR_NE(a, b) do { \
    if (strcmp((a), (b)) == 0) { \
        fprintf(stderr, "  FAIL: %s (line %d): \"%s\" == \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_TRUE(expr) TEST_ASSERT(expr)
#define TEST_ASSERT_FALSE(expr) TEST_ASSERT(!(expr))

#define TEST_ASSERT_MEM_EQ(a, b, size) do { \
    if (memcmp((a), (b), (size)) != 0) { \
        fprintf(stderr, "  FAIL: %s (line %d): memory mismatch (%d bytes)\n", \
                __FILE__, __LINE__, (int)(size)); \
        test_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_IN_RANGE(val, lo, hi) do { \
    if ((val) < (lo) || (val) > (hi)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %lld not in range [%lld, %lld]\n", \
                __FILE__, __LINE__, (long long)(val), (long long)(lo), (long long)(hi)); \
        test_failed++; \
        return; \
    } \
} while (0)

/* ---------- runner ---------- */

static inline void test_run_all(void)
{
    for (int i = 0; i < test_registry_count; i++) {
        test_current = i;
        test_total++;

        int failed_before = test_failed;

        if (test_setup_fn)
            test_setup_fn();

        printf("  %-60s", test_registry[i].name);

        test_registry[i].func();

        if (test_teardown_fn)
            test_teardown_fn();

        if (test_failed == failed_before) {
            test_passed++;
            printf(" OK\n");
        } else {
            printf(" FAIL\n");
        }
    }
}

static inline int test_summary(void)
{
    printf("\n====================\n");
    printf("Results: %d test%s, %d passed, %d failed\n",
           test_total, test_total == 1 ? "" : "s", test_passed, test_failed);
    printf("====================\n");
    return test_failed > 0 ? 1 : 0;
}

#endif /* TRINIOS_TEST_FRAMEWORK_H */