#pragma once
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
namespace naval {
using Real = long double;
constexpr Real pi = 3.141592653589793238462643383279502884L;
struct Vec {
  Real x = 0, y = 0;
  Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
  Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
  Vec operator*(Real s) const { return {x * s, y * s}; }
};
inline Real dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
inline Real norm(Vec a) { return std::hypotl(a.x, a.y); }
Real heading(Real degrees);
struct Pose {
  Vec p;
  Real heading = 0, speed = 0;
};
struct Motion {
  Pose initial;
  Real acceleration = 0,
       angular = 0; // actual speed delta and radians delta per T
  Real stoppedAt = 2;
  Pose at(Real t) const;
  Real distance(Real t) const;
  Real bound(Real radius, Real lo, Real hi) const;
  void stop(Real t) { stoppedAt = t; }
};
Motion maneuver(Pose p, Real targetHeading, Real targetSpeed,
                Real accelerationLimit, Real turnLimit);
struct Body {
  Motion motion;
  Real length = 0, width = 0;
  Real radius() const { return std::hypotl(length, width) / 2; }
};
std::array<Vec, 4> corners(const Body &, Real t);
Real gap(const Body &, const Body &, Real t);
Real margin(const Body &, Real t, Real size);
bool contains(const Body &, Vec point, Real t);
struct Coast {
  Vec a, b;
  Real radius;
};
std::optional<Real> coastFraction(Vec start, Vec end, const Coast &coast);
struct Separation {
  Real distance;
  Vec normal;
  Vec point;
};
Separation coastGap(const Body &, const Coast &, Real);
struct Solver {
  Real distanceTolerance = 1e-6L, timeTolerance = 1e-9L;
  size_t evaluations = 0, limit = 250000;
};
std::optional<Real> coastContact(const Body &, const Coast &, Real lo, Real hi,
                                 Solver &, bool projectile = false);
// Adaptive continuous interval search. A midpoint separation plane plus
// Hausdorff velocity bound certifies empty intervals; never assumes samples
// cover a step.
std::optional<Real> contact(const Body &, const Body &, Real lo, Real hi,
                            Solver &);
std::optional<Real> boundary(const Body &, Real lo, Real hi, Real size,
                             Solver &);
std::optional<Real> exit_body(const Body &point, const Body &ship, Real lo,
                              Real hi, Solver &);
} // namespace naval
