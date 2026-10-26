#include "renderer.h"

#include <algorithm>
#include <unistd.h>

namespace {

int non_negative(int v) { return std::max(v, 0); }

void append_uint(std::string& out, unsigned value) {
  char digits[12];
  int length = 0;
  do {
    digits[length++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (length > 0) out.push_back(digits[--length]);
}

void append_cursor_move(std::string& out, int row, int col) {
  out += "\x1b[";
  append_uint(out, static_cast<unsigned>(row + 1));
  out.push_back(';');
  append_uint(out, static_cast<unsigned>(col + 1));
  out.push_back('H');
}

void append_sgr(std::string& out, bool foreground, ColorCode color) {
  if ((color & kRgbColorFlag) != 0) {
    const Rgb rgb = color_code_to_rgb(color);
    out += foreground ? "\x1b[38;2;" : "\x1b[48;2;";
    append_uint(out, rgb.r);
    out.push_back(';');
    append_uint(out, rgb.g);
    out.push_back(';');
    append_uint(out, rgb.b);
  } else {
    out += foreground ? "\x1b[38;5;" : "\x1b[48;5;";
    append_uint(out, color);
  }
  out.push_back('m');
}

}  // namespace

void FdSink::write(std::string_view bytes) {
  while (!bytes.empty()) {
    const ssize_t written = ::write(fd_, bytes.data(), bytes.size());
    if (written <= 0) return;  // terminal went away; the main loop will exit
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

Renderer::Renderer(int cols, int rows) : cols_(non_negative(cols)), rows_(non_negative(rows)) {
  previous_.assign(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_), Cell{});
}

void Renderer::resize(int cols, int rows) {
  cols_ = non_negative(cols);
  rows_ = non_negative(rows);
  previous_.assign(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_), Cell{});
  dirty_ = true;
}

void Renderer::invalidate() { dirty_ = true; }

void Renderer::render(const Field& field, const Ramp& ramp, Sink& sink) {
  if (ramp.size() == 0) return;

  // Clamping to the field rather than trusting cols_/rows_: a terminal resize
  // between the field being sized and this call would otherwise index out of
  // bounds. A frame drawn slightly small is recoverable; a heap overrun is
  // not.
  const int cols = std::min(cols_, field.width());
  const int rows = std::min(rows_, field.height() / 2);

  buffer_.clear();

  // Colour state is reset per frame, not carried across frames: between two
  // renders the terminal may have been written to by anything else.
  int current_fg = -1;
  int current_bg = -1;
  int last_row = -1;
  int last_col = -1;

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      const Cell cell = select_cell(field.at(col, row * 2), field.at(col, row * 2 + 1), ramp);
      const auto index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols_) +
                         static_cast<std::size_t>(col);

      if (!dirty_ && previous_[index] == cell) continue;

      if (row != last_row || col != last_col) append_cursor_move(buffer_, row, col);
      if (static_cast<int>(cell.fg) != current_fg) {
        append_sgr(buffer_, true, cell.fg);
        current_fg = cell.fg;
      }
      if (static_cast<int>(cell.bg) != current_bg) {
        append_sgr(buffer_, false, cell.bg);
        current_bg = cell.bg;
      }
      buffer_ += cell.glyph;

      previous_[index] = cell;
      last_row = row;
      last_col = col + 1;
    }
  }

  dirty_ = false;
  if (buffer_.empty()) return;

  // Synchronized output (DEC private mode 2026). A frame can run to ~100 KB
  // on a large terminal while a pty buffer is around 64 KB, so write() blocks
  // partway through and the terminal -- which composites on its own repaint
  // timer, not ours -- presents part of the new frame over part of the old
  // one. These markers tell it to buffer everything between them and present
  // it in one go. Terminals that do not implement the mode ignore both.
  //
  // Emitted only when there is something to draw: a terminal that is handed a
  // synchronized update every frame may schedule a present for each one even
  // when the payload is empty.
  frame_.assign("\x1b[?2026h");
  frame_ += buffer_;
  frame_ += "\x1b[?2026l";
  sink.write(frame_);
}
