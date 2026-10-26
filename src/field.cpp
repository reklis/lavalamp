#include "field.h"

#include <algorithm>

namespace {

int non_negative(int v) { return std::max(v, 0); }

}  // namespace

Field::Field(int w, int h)
    : width_(non_negative(w)),
      height_(non_negative(h)),
      data_(static_cast<std::size_t>(non_negative(w)) * static_cast<std::size_t>(non_negative(h)),
            0.0) {}

double& Field::at(int x, int y) { return data_[static_cast<std::size_t>(y) * width_ + x]; }

double Field::at(int x, int y) const {
  return data_[static_cast<std::size_t>(y) * width_ + x];
}

void Field::resize(int w, int h) {
  width_ = non_negative(w);
  height_ = non_negative(h);
  data_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0.0);
}

void Field::fill(double v) { std::fill(data_.begin(), data_.end(), v); }
