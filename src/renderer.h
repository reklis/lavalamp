#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "field.h"
#include "glyph.h"
#include "palette.h"

// Output destination. Exists so the renderer can be tested by asserting on
// the exact escape sequences it produces, which is the only practical way to
// verify the diffing and SGR-coalescing below.
struct Sink {
  virtual void write(std::string_view bytes) = 0;
  virtual ~Sink() = default;
};

struct StringSink : Sink {
  std::string data;
  void write(std::string_view bytes) override { data.append(bytes); }
};

class FdSink : public Sink {
 public:
  explicit FdSink(int fd) : fd_(fd) {}
  void write(std::string_view bytes) override;

 private:
  int fd_;
};

class Renderer {
 public:
  Renderer(int cols, int rows);

  void resize(int cols, int rows);

  // Drops the previous-frame cache, forcing the next render to repaint every
  // cell. Needed after a resize or anything else that invalidates what the
  // terminal is actually showing.
  void invalidate();

  // Composes one frame and hands it to the sink in a single write.
  //
  // Three things keep the byte count down, all of them load-bearing at
  // 30 fps on a large terminal: cells identical to the previous frame are
  // skipped entirely, the cursor is only repositioned when the next cell to
  // emit is not adjacent to the last one, and an SGR sequence is emitted only
  // when the colour actually changes. Without the last one a 200x50 terminal
  // costs tens of kilobytes per frame and the lamp becomes bandwidth-bound.
  void render(const Field& field, const Ramp& ramp, Sink& sink);

 private:
  int cols_;
  int rows_;
  bool dirty_ = true;
  std::vector<Cell> previous_;
  std::string buffer_;
  // Kept as a member so the per-frame concatenation reuses its capacity
  // instead of allocating ~100 KB thirty times a second.
  std::string frame_;
};
