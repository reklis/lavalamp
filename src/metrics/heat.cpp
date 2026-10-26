#include "metrics/heat.h"

#include <algorithm>
#include <cmath>

Smoother::Smoother(double tau_attack, double tau_decay)
    : tau_attack_(std::max(tau_attack, 0.0)), tau_decay_(std::max(tau_decay, 0.0)) {}

double Smoother::update(double target, double dt) {
  if (!(dt > 0.0)) return value_;

  const double tau = target > value_ ? tau_attack_ : tau_decay_;
  if (tau <= 0.0) {
    value_ = target;
    return value_;
  }

  // alpha = 1 - exp(-dt/tau) rather than dt/tau. The naive form overshoots
  // and then oscillates once dt approaches tau, and the render loop will hand
  // this large steps whenever the process is descheduled.
  const double alpha = -std::expm1(-dt / tau);
  value_ += alpha * (target - value_);
  return value_;
}
