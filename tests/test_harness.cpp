#include "test_harness.h"

#include <cstdio>
#include <vector>

namespace {

struct TestCase {
  const char* name;
  const char* file;
  void (*fn)();
};

// Function-local statics: the registry must already exist when the first
// TestRegistrar in any translation unit runs, and static initialisation order
// across translation units is unspecified.
std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

struct Failure {
  const char* file;
  int line;
  const char* expr;
};

std::vector<Failure>& current_failures() {
  static std::vector<Failure> failures;
  return failures;
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

void register_test(const char* name, const char* file, void (*fn)()) {
  registry().push_back(TestCase{name, file, fn});
}

TestRegistrar::TestRegistrar(const char* name, const char* file, void (*fn)()) {
  register_test(name, file, fn);
}

void record_failure(const char* file, int line, const char* expr) {
  current_failures().push_back(Failure{file, line, expr});
}

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return 0;
  std::size_t count = 0;
  for (std::size_t pos = haystack.find(needle); pos != std::string_view::npos;
       pos = haystack.find(needle, pos + needle.size())) {
    ++count;
  }
  return count;
}

int run_all_tests(const char* filter) {
  std::size_t ran = 0;
  std::size_t passed = 0;

  for (const TestCase& test : registry()) {
    if (filter != nullptr && *filter != '\0' &&
        !contains(test.name, filter) && !contains(test.file, filter)) {
      continue;
    }

    ++ran;
    current_failures().clear();
    test.fn();

    if (current_failures().empty()) {
      ++passed;
      std::printf("  ok   %s\n", test.name);
    } else {
      std::printf("  FAIL %s\n", test.name);
      for (const Failure& failure : current_failures()) {
        std::printf("         %s:%d: %s\n", failure.file, failure.line, failure.expr);
      }
    }
  }

  if (ran == 0) {
    std::printf("no tests matched filter '%s'\n", filter != nullptr ? filter : "");
    return 1;
  }

  std::printf("%zu/%zu passed\n", passed, ran);
  return passed == ran ? 0 : 1;
}
