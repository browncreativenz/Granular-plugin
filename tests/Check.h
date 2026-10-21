/**
 * @file Check.h
 * @brief Tiny test harness: CHECK(cond, ...) prints a message on failure and
 *        counts it; main() returns Failures() != 0.
 */

#pragma once

#include <cstdio>

namespace granular::test {

inline int& FailureCount() { static int n = 0; return n; }
inline int  Failures()     { return FailureCount(); }

} // namespace granular::test

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);    \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
            granular::test::FailureCount()++;                              \
        }                                                                  \
    } while (0)
