#include "renderer.h"

#include <string>

#include "field.h"
#include "palette.h"
#include "test_harness.h"

TEST(field_resize_preserves_dimensions) {
  Field f(10, 20);
  f.resize(5, 8);
  CHECK_EQ(f.width(), 5);
  CHECK_EQ(f.height(), 8);
}

TEST(first_render_emits_every_cell) {
  Field f(2, 2);
  f.fill(0.0);  // 2 cols x 2 rows-of-pixels = 1 text row
  Renderer r(2, 1);
  StringSink s;
  r.render(f, build_ramp(0.5, 32), s);
  CHECK(s.data.find("\x1b[38;5;") != std::string::npos);
  CHECK(s.data.find("\x1b[48;5;") != std::string::npos);
}

TEST(unchanged_second_render_emits_no_cells) {
  Field f(2, 2);
  f.fill(0.0);
  Renderer r(2, 1);
  Ramp p = build_ramp(0.5, 32);
  StringSink first, second;
  r.render(f, p, first);
  r.render(f, p, second);
  CHECK(second.data.find("\x1b[38;5;") == std::string::npos);
}

TEST(identical_adjacent_cells_emit_sgr_once) {
  Field f(8, 2);
  f.fill(0.0);
  Renderer r(8, 1);
  StringSink s;
  r.render(f, build_ramp(0.5, 32), s);
  // 8 identical cells: exactly one fg and one bg sequence for the run
  CHECK_EQ(count_occurrences(s.data, "\x1b[38;5;"), 1u);
}

TEST(invalidate_forces_full_repaint) {
  Field f(2, 2);
  f.fill(0.0);
  Renderer r(2, 1);
  Ramp p = build_ramp(0.5, 32);
  StringSink first, second;
  r.render(f, p, first);
  r.invalidate();
  r.render(f, p, second);
  CHECK(second.data.find("\x1b[38;5;") != std::string::npos);
}

TEST(ramp_change_repaints_even_when_field_is_static) {
  Field f(2, 2);
  f.fill(0.3);
  Renderer r(2, 1);
  StringSink first, second;
  r.render(f, build_ramp(0.0, 32), first);
  r.render(f, build_ramp(1.0, 32), second);
  CHECK(second.data.find("\x1b[48;5;") != std::string::npos);
}

TEST(frames_are_wrapped_in_synchronized_output_markers) {
  // Without DEC mode 2026 the terminal composites whenever its own repaint
  // timer fires, which lands mid-write: a frame is ~100 KB at 200x50 but a
  // pty buffer is ~64 KB, so write() blocks partway and the terminal shows
  // part of the new frame over part of the old one. The markers tell it to
  // buffer and present the whole frame at once.
  Field f(4, 2);
  f.fill(0.0);
  Renderer r(4, 1);
  StringSink s;
  r.render(f, build_ramp(0.5, 32), s);
  CHECK(s.data.rfind("\x1b[?2026h", 0) == 0);  // begins the frame
  CHECK(s.data.size() >= 8 && s.data.compare(s.data.size() - 8, 8, "\x1b[?2026l") == 0);
}

TEST(an_unchanged_frame_emits_nothing_at_all) {
  // Not even an empty begin/end pair: a terminal that sees a synchronized
  // update every frame may still schedule a present for each one.
  Field f(4, 2);
  f.fill(0.0);
  Renderer r(4, 1);
  Ramp p = build_ramp(0.5, 32);
  StringSink first, second;
  r.render(f, p, first);
  r.render(f, p, second);
  CHECK(second.data.empty());
}
