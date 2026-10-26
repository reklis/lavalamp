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

// The comparisons live in function templates rather than inside the macros,
// and the macros are variadic, because braced initialisers are extremely
// common arguments here -- CHECK_EQ(xterm256_to_rgb(16), Rgb{0, 0, 0}) looks
// like four macro arguments to the preprocessor. Pasting __VA_ARGS__ into a
// function call instead lets the compiler's grammar group the braces.
template <typename A, typename B>
bool harness_equal(const A& a, const B& b) {
  return a == b;
}

template <typename A, typename B>
bool harness_near(const A& a, const B& b, double epsilon) {
  return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= epsilon;
}

// Failures record the stringified expression rather than the operand values.
// That keeps the macros usable on any type with the relevant operator --
// std::vector<std::uint8_t> comparisons in the palette tests, for instance --
// without the harness needing to know how to print it.
#define CHECK(...)                                                   \
  do {                                                               \
    if (!(__VA_ARGS__))                                              \
      record_failure(__FILE__, __LINE__, "CHECK(" #__VA_ARGS__ ")"); \
  } while (0)

#define CHECK_EQ(...)                                                \
  do {                                                               \
    if (!harness_equal(__VA_ARGS__))                                 \
      record_failure(__FILE__, __LINE__, "CHECK_EQ(" #__VA_ARGS__ ")"); \
  } while (0)

#define CHECK_NEAR(...)                                              \
  do {                                                               \
    if (!harness_near(__VA_ARGS__))                                  \
      record_failure(__FILE__, __LINE__, "CHECK_NEAR(" #__VA_ARGS__ ")"); \
  } while (0)
