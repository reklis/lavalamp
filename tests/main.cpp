#include "test_harness.h"

int main(int argc, char** argv) {
  return run_all_tests(argc > 1 ? argv[1] : nullptr);
}
