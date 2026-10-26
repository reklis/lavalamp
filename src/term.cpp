#include "term.h"

#include <cstring>
#include <poll.h>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

volatile std::sig_atomic_t g_should_quit = 0;
volatile std::sig_atomic_t g_resized = 0;

namespace {

constexpr int kMinCols = 20;
constexpr int kMinRows = 10;

constexpr std::string_view kEnterAltScreen = "\x1b[?1049h";
constexpr std::string_view kLeaveAltScreen = "\x1b[?1049l";
constexpr std::string_view kHideCursor = "\x1b[?25l";
constexpr std::string_view kShowCursor = "\x1b[?25h";
// Autowrap off. Without it, painting the bottom-right cell makes the cursor
// wrap and scrolls the alternate screen, which creeps the whole image upward.
constexpr std::string_view kAutowrapOff = "\x1b[?7l";
constexpr std::string_view kAutowrapOn = "\x1b[?7h";
constexpr std::string_view kResetAttributes = "\x1b[0m";

void write_all(int fd, std::string_view bytes) {
  while (!bytes.empty()) {
    const ssize_t written = ::write(fd, bytes.data(), bytes.size());
    if (written <= 0) return;
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

void on_quit_signal(int) { g_should_quit = 1; }
void on_winch(int) { g_resized = 1; }

}  // namespace

std::optional<TermSize> query_term_size(int fd) {
  winsize ws{};
  if (::ioctl(fd, TIOCGWINSZ, &ws) != 0) return std::nullopt;
  if (ws.ws_col == 0 || ws.ws_row == 0) return std::nullopt;
  return TermSize{static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
}

TermSize query_term_size_or(int fd, TermSize fallback) {
  return query_term_size(fd).value_or(fallback);
}

bool is_size_usable(TermSize size) {
  return size.cols >= kMinCols && size.rows >= kMinRows;
}

bool supports_truecolor(std::string_view colorterm, std::string_view term) {
  return colorterm == "truecolor" || colorterm == "24bit" || term.ends_with("-direct");
}

TerminalSession::TerminalSession(int fd, bool use_alt_screen)
    : fd_(fd), use_alt_screen_(use_alt_screen) {
  if (::tcgetattr(fd_, &saved_termios_) == 0) {
    termios raw = saved_termios_;
    // Echo off so stray keys do not litter the art; non-canonical so a
    // keypress is readable without waiting for Enter.
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(fd_, TCSANOW, &raw) == 0) termios_saved_ = true;
  }

  if (use_alt_screen_) write_all(fd_, kEnterAltScreen);
  write_all(fd_, kHideCursor);
  write_all(fd_, kAutowrapOff);
}

TerminalSession::~TerminalSession() {
  write_all(fd_, kResetAttributes);
  write_all(fd_, kAutowrapOn);
  write_all(fd_, kShowCursor);
  if (use_alt_screen_) write_all(fd_, kLeaveAltScreen);
  if (termios_saved_) ::tcsetattr(fd_, TCSANOW, &saved_termios_);
}

void install_signal_handlers() {
  struct sigaction quit_action{};
  quit_action.sa_handler = &on_quit_signal;
  sigemptyset(&quit_action.sa_mask);
  quit_action.sa_flags = 0;
  ::sigaction(SIGINT, &quit_action, nullptr);
  ::sigaction(SIGTERM, &quit_action, nullptr);
  ::sigaction(SIGHUP, &quit_action, nullptr);

  struct sigaction winch_action{};
  winch_action.sa_handler = &on_winch;
  sigemptyset(&winch_action.sa_mask);
  winch_action.sa_flags = SA_RESTART;
  ::sigaction(SIGWINCH, &winch_action, nullptr);
}

bool poll_quit_key(int fd) {
  bool quit = false;

  for (;;) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;

    const int ready = ::poll(&entry, 1, 0);
    if (ready <= 0) break;
    if ((entry.revents & POLLIN) == 0) break;

    char buffer[64];
    const ssize_t count = ::read(fd, buffer, sizeof buffer);
    if (count <= 0) break;

    for (ssize_t i = 0; i < count; ++i) {
      if (buffer[i] == 'q' || buffer[i] == 'Q' || buffer[i] == 0x1b) quit = true;
    }

    if (static_cast<std::size_t>(count) < sizeof buffer) break;
  }

  return quit;
}
