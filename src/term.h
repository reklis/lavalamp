#pragma once

#include <csignal>
#include <optional>
#include <termios.h>

struct TermSize {
  int cols = 0;
  int rows = 0;
};

std::optional<TermSize> query_term_size(int fd);

// Returns `fallback` when the real size cannot be determined, so startup has
// one code path rather than two.
TermSize query_term_size_or(TermSize fallback);

// Below 20x10 there is not enough room for the lamp to read as anything, so
// the program refuses rather than painting a corrupt handful of cells.
bool is_size_usable(TermSize size);

// RAII ownership of the terminal's state. The destructor runs on every exit
// path -- including signals, because the main loop exits by observing a flag
// rather than by _exit -- so the terminal is never left without a cursor or
// without echo. That is the one failure mode that outlives the process.
class TerminalSession {
 public:
  TerminalSession(int fd, bool use_alt_screen);
  ~TerminalSession();

  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;

 private:
  int fd_;
  bool use_alt_screen_;
  bool termios_saved_ = false;
  termios saved_termios_{};
};

// Handlers do nothing but set these flags: no I/O, no allocation, nothing
// that is unsafe to call from a signal context.
extern volatile std::sig_atomic_t g_should_quit;
extern volatile std::sig_atomic_t g_resized;

void install_signal_handlers();

// Non-blocking. Drains whatever is pending on `fd` -- which also stops stray
// keystrokes queueing up for the shell after exit -- and reports whether any
// of it was a quit request.
bool poll_quit_key(int fd);
