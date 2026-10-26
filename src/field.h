#pragma once

#include <vector>

// A scalar field sampled at one value per *pixel*, where a pixel is half a
// terminal cell: the field is `cols` wide and `rows * 2` tall. Terminal cells
// are roughly twice as tall as they are wide, so stacking two samples per
// cell gives square sampling and doubles the vertical resolution available
// to blob silhouettes.
class Field {
 public:
  Field(int w, int h);

  double& at(int x, int y);
  double at(int x, int y) const;

  int width() const { return width_; }
  int height() const { return height_; }

  // Reallocates. Contents are not preserved -- the only caller is a terminal
  // resize, after which the simulation is resampled from scratch anyway.
  void resize(int w, int h);

  void fill(double v);

 private:
  int width_;
  int height_;
  std::vector<double> data_;
};
