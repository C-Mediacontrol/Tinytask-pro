#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "ttp_vision.h"

static void test_isqrt_cases(void) {
    printf("[1/2] Running test_isqrt_cases...\n");

    assert(ttp_isqrt(0ULL) == 0UL);
    assert(ttp_isqrt(1ULL) == 1UL);
    assert(ttp_isqrt(4ULL) == 2UL);
    assert(ttp_isqrt(99ULL) == 9UL);
    assert(ttp_isqrt(100ULL) == 10UL);
    assert(ttp_isqrt(2560000ULL) == 1600UL);
    assert(ttp_isqrt(1ULL << 32) == 65536UL);
    assert(ttp_isqrt(1ULL << 40) == 1048576UL);

    printf("  ttp_isqrt(0) = %lu (expected 0)\n", ttp_isqrt(0ULL));
    printf("  ttp_isqrt(1) = %lu (expected 1)\n", ttp_isqrt(1ULL));
    printf("  ttp_isqrt(4) = %lu (expected 2)\n", ttp_isqrt(4ULL));
    printf("  ttp_isqrt(99) = %lu (expected 9)\n", ttp_isqrt(99ULL));
    printf("  ttp_isqrt(100) = %lu (expected 10)\n", ttp_isqrt(100ULL));
    printf("  ttp_isqrt(2560000) = %lu (expected 1600)\n", ttp_isqrt(2560000ULL));
    printf("  ttp_isqrt(1ULL << 32) = %lu (expected 65536)\n", ttp_isqrt(1ULL << 32));
    printf("  ttp_isqrt(1ULL << 40) = %lu (expected 1048576)\n", ttp_isqrt(1ULL << 40));
    printf("  [PASS] All ttp_isqrt tests passed!\n");
}

static void test_sqrt_cases(void) {
    printf("[2/2] Running test_sqrt_cases...\n");

    double r0 = ttp_sqrt(0.0);
    assert(r0 == 0.0);
    printf("  ttp_sqrt(0.0) = %.8f (expected 0.0)\n", r0);

    double r2 = ttp_sqrt(2.0);
    double diff2 = r2 - 1.414213562373095;
    if (diff2 < 0) diff2 = -diff2;
    assert(diff2 < 1e-7);
    printf("  ttp_sqrt(2.0) = %.8f (expected ~1.41421356, diff=%.2e)\n", r2, diff2);

    double r100 = ttp_sqrt(100.0);
    double diff100 = r100 - 10.0;
    if (diff100 < 0) diff100 = -diff100;
    assert(diff100 < 1e-9);
    printf("  ttp_sqrt(100.0) = %.8f (expected 10.0, diff=%.2e)\n", r100, diff100);

    printf("  [PASS] All ttp_sqrt tests passed!\n");
}

int main(void) {
    printf("=== Test Suite: ttp_isqrt & ttp_sqrt ===\n");
    test_isqrt_cases();
    test_sqrt_cases();
    printf("=== ALL TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}
