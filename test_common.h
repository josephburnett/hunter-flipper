#pragma once

// Common test definitions and utilities
#define _GNU_SOURCE
#define TEST_BUILD

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Missing macros that terrain.c needs - use inline functions to avoid linker issues
#ifndef MAX
static inline int MAX(int a, int b) { return (a > b) ? a : b; }
#endif

#ifndef MIN  
static inline int MIN(int a, int b) { return (a < b) ? a : b; }
#endif

// Mock M*LIB
#define M_LET(name, type, value) type name = value

// Test assertion macro
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("ASSERTION FAILED: %s at %s:%d\n", message, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

// Test status tracking
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} TestResults;