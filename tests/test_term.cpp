#include "term.h"

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#include "test_harness.h"

TEST(minimum_size_is_enforced) {
  CHECK(is_size_usable(TermSize{20, 10}));
  CHECK(is_size_usable(TermSize{200, 60}));
  CHECK(!is_size_usable(TermSize{19, 10}));
  CHECK(!is_size_usable(TermSize{20, 9}));
  CHECK(!is_size_usable(TermSize{0, 0}));
}

TEST(signal_flags_start_clear) {
  CHECK_EQ(g_should_quit, 0);
  CHECK_EQ(g_resized, 0);
}

TEST(sigwinch_sets_resize_flag) {
  install_signal_handlers();
  g_resized = 0;
  raise(SIGWINCH);
  CHECK_EQ(g_resized, 1);  // Review Focus 3: resize must be observable
}

TEST(sigint_sets_quit_flag) {
  install_signal_handlers();
  g_should_quit = 0;
  raise(SIGINT);
  CHECK_EQ(g_should_quit, 1);
}

TEST(tiny_terminal_is_rejected_before_any_rendering) {
  // Review Focus 3, lower bound: must refuse rather than corrupt the screen.
  // Uses an explicit non-tty fd so the result does not depend on whether the
  // test binary happens to be running under a terminal.
  const int fd = ::open("/dev/null", O_RDONLY);
  CHECK(fd >= 0);
  CHECK(!query_term_size(fd).has_value());
  CHECK(!is_size_usable(query_term_size_or(fd, TermSize{10, 4})));
  CHECK(is_size_usable(query_term_size_or(fd, TermSize{80, 24})));
  ::close(fd);
}
