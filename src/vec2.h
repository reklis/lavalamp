#pragma once

#include <cmath>

struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  double length() const { return std::sqrt(x * x + y * y); }
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return Vec2{a.x * s, a.y * s}; }
inline Vec2 operator*(double s, Vec2 a) { return a * s; }

inline Vec2& operator+=(Vec2& a, Vec2 b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}
