/*
 * TriniOS Test Framework - Implementation
 * ========================================
 * Provides global state for test registry and counters.
 * Include this as the first .c file in every test binary.
 */

#include "test_framework.h"

int test_total = 0;
int test_passed = 0;
int test_failed = 0;
int test_current = 0;

test_case_t test_registry[MAX_TESTS];
int test_registry_count = 0;
setup_func_t test_setup_fn = NULL;
setup_func_t test_teardown_fn = NULL;