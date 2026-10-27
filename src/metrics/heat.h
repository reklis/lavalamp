#pragma once

// One immutable reading of what the machine is doing, published by the
// metrics thread and consumed by the render loop. Every channel carries its
// own availability flag: a machine with no readable GPU counter, no thermal
// zone or no network still produces a beautiful lamp, it just has fewer
// moods, so an unavailable channel must be distinguishable from a channel
// that is genuinely reading zero.
struct Heat {
  double cpu = 0.0;
  double gpu = 0.0;
  double net = 0.0;
  double temp = 0.0;
  double memory = 0.0;
  double audio = 0.0;

  bool cpu_ok = false;
  bool gpu_ok = false;
  bool net_ok = false;
  bool temp_ok = false;
  bool memory_ok = false;
  bool audio_ok = false;
};

// Exponential smoothing with separate rise and fall time constants.
//
// The asymmetry exists because the inputs mean physically different
// things. Temperature is slow and pretending otherwise just renders noise;
// network traffic is bursty, so it wants a fast attack and a slow decay so a
// transfer registers immediately and then settles like a splash.
class Smoother {
 public:
  Smoother(double tau_attack, double tau_decay);

  // `dt` is passed in rather than read from a clock so this is testable and
  // deterministic. Steps of any size are safe: the exponential form cannot
  // overshoot the target, which matters because a descheduled process hands
  // the render loop arbitrarily large steps.
  double update(double target, double dt);

  double value() const { return value_; }

 private:
  double tau_attack_;
  double tau_decay_;
  double value_ = 0.0;
};
