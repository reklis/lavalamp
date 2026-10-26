#pragma once

#include "vec2.h"

// Smooth value noise on an integer lattice, in [-1, 1]. Pure and
// deterministic: no global state, no rand(), so the simulation is
// reproducible from a seed.
double value_noise(double x, double y, double z);

// 2D curl of a noise potential, animated by `t`.
//
// Curl noise is divergence-free, which is the entire reason to use it here: a
// plain noise velocity field has sources and sinks, so blobs pile up in some
// places and drain out of others. A divergence-free field swirls and folds
// instead, which is what separates organic motion from objects drifting on
// rails.
Vec2 curl_noise(double x, double y, double t);
