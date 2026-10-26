#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

// A deliberately tiny test harness. The project ships with no third-party
// dependencies (see the spec's constraints), and that extends to the tests:
// pulling in a framework would mean either vendoring it or fetching it at
// configure time, and assert-style unit tests do not need either.

void register_test(const char* name, const char* file, void (*fn)());
void record_failure(const char* file, int line, const char* expr);

// `filter` is matched as a substring against both the test name and the name
// of the file it lives in, so `lavalamp_tests color` runs test_color.cpp.
int run_all_tests(const char* filter);

std::size_t count_occurrences(std::string_view haystack, std::string_view needle);

struct TestRegistrar {
  TestRegistrar(const char* name, const char* file, void (*fn)());
};

#define TEST(name)                                                   \
  static void name();                                                \
  static TestRegistrar registrar_##name(#name, __FILE__, &name);     \
  static void name()

// Failures record the stringified expression rather than the operand values.
// That keeps the macros usable on any type with the relevant operator --
// std::vector<std::uint8_t> comparisons in the palette tests, for instance --
// without the harness needing to know how to print it.
#define CHECK(expr)                                                  \
  do {                                                               \
    if (!(expr)) record_failure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
  } while (0)

#define CHECK_EQ(a, b)                                               \
  do {                                                               \
    if (!((a) == (b)))                                               \
      record_failure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                        \
  do {                                                               \
    if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps))) \
      record_failure(__FILE__, __LINE__,                             \
                     "CHECK_NEAR(" #a ", " #b ", " #eps ")");        \
  } while (0)
