#ifndef UNIT_H
#define UNIT_H
/* Minimal test harness: no dependencies, runs anywhere a C compiler does. */
#include <stdio.h>
#include <stdlib.h>

static int unit_failures, unit_checks;

#define CHECK(cond) do { unit_checks++; if (!(cond)) { unit_failures++; \
    fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); unit_checks++; \
    if (_a != _b) { unit_failures++; fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); unit_checks++; \
    if (_a - _b > (tol) || _b - _a > (tol)) { unit_failures++; \
    fprintf(stderr, "  FAIL %s:%d: %s ~ %s (%.3f vs %.3f)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define RUN(test) do { int _f = unit_failures; test(); \
    printf("  %-44s %s\n", #test, unit_failures == _f ? "ok" : "FAILED"); } while (0)

#define UNIT_DONE(name) do { printf("%s: %d checks, %d failed\n", name, unit_checks, unit_failures); \
    return unit_failures ? 1 : 0; } while (0)

#endif
