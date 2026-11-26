#include "naval/geometry.hpp"
#include <algorithm>
#include <limits>
#include <vector>
namespace naval {
Real heading(Real h) {
  h = std::fmod(h, 360.L);
  if (h < 0)
    h += 360;
  // A tiny negative remainder can round to 360 when adding a full turn,
  // or when serializing Real to JSON's double. Keep the persisted range
  // half-open without snapping representable headings below 360 to zero.
  return h == 0 || double(h) >= 360 ? 0.L : h;
}
static Vec direction(Real degrees) {
  Real h = heading(degrees);
  if (h == 0)
    return {0, 1};
  if (h == 90)
    return {1, 0};
  if (h == 180)
    return {0, -1};
  if (h == 270)
    return {-1, 0};
  return {std::sin(h * pi / 180), std::cos(h * pi / 180)};
}
Pose Motion::at(Real time) const {
  Real t = std::min(time, stoppedAt), z = angular * t / 2;
  Real sinc, f;
  if (std::abs(z) < 1e-3L) {
    Real z2 = z * z;
    sinc = 1 - z2 / 6 + z2 * z2 / 120 - z2 * z2 * z2 / 5040;
    f = z / 3 - z * z2 / 30 + z * z2 * z2 / 840 - z * z2 * z2 * z2 / 45360;
  } else {
    sinc = std::sin(z) / z;
    f = (std::sin(z) - z * std::cos(z)) / (z * z);
  }
  Real a = t * (initial.speed + acceleration * t / 2) * sinc,
       b = acceleration * t * t / 2 * f;
  auto dir = direction(initial.heading + z * 180 / pi);
  Vec displacement{a * dir.x + b * dir.y, a * dir.y - b * dir.x};
  return {initial.p + displacement,
          heading(initial.heading + angular * t * 180 / pi),
          time >= stoppedAt ? 0 : initial.speed + acceleration * t};
}
Real Motion::distance(Real time) const {
  Real t = std::min(time, stoppedAt);
  return initial.speed * t + acceleration * t * t / 2;
}
Real Motion::bound(Real r, Real lo, Real hi) const {
  if (lo >= stoppedAt)
    return 0;
  hi = std::min(hi, stoppedAt);
  return std::max(initial.speed + acceleration * lo,
                  initial.speed + acceleration * hi) +
         std::abs(angular) * r;
}
Motion maneuver(Pose p, Real targetHeading, Real targetSpeed,
                Real accelerationLimit, Real turnLimit) {
  Real d = heading(targetHeading - p.heading);
  if (d > 180)
    d -= 360;
  return {
      p,
      std::clamp(targetSpeed - p.speed, -accelerationLimit, accelerationLimit),
      std::clamp(d, -turnLimit, turnLimit) * pi / 180, 2};
}
static std::array<Vec, 2> axes(Real h) {
  auto d = direction(h);
  return {Vec{d.y, -d.x}, d};
}
std::array<Vec, 4> corners(const Body &b, Real t) {
  auto p = b.motion.at(t);
  auto ax = axes(p.heading);
  Vec u = ax[0] * (b.width / 2), v = ax[1] * (b.length / 2);
  return {p.p + u + v, p.p + u - v, p.p - u - v, p.p - u + v};
}
Real gap(const Body &a, const Body &b, Real t) {
  auto pa = a.motion.at(t), pb = b.motion.at(t);
  auto aa = axes(pa.heading), ab = axes(pb.heading);
  Vec d = pa.p - pb.p;
  Real result = -std::numeric_limits<Real>::infinity();
  for (Vec axis : {aa[0], aa[1], ab[0], ab[1]}) {
    Real ra = std::abs(dot(axis, aa[0])) * a.width / 2 +
              std::abs(dot(axis, aa[1])) * a.length / 2;
    Real rb = std::abs(dot(axis, ab[0])) * b.width / 2 +
              std::abs(dot(axis, ab[1])) * b.length / 2;
    result = std::max(result, std::abs(dot(d, axis)) - ra - rb);
  }
  return result;
}
Real margin(const Body &b, Real t, Real size) {
  Real g = size;
  for (Vec p : corners(b, t))
    g = std::min({g, p.x, p.y, size - p.x, size - p.y});
  return g;
}
bool contains(const Body &b, Vec point, Real t) {
  auto p = b.motion.at(t);
  auto a = axes(p.heading);
  Vec d = point - p.p;
  return std::abs(dot(d, a[0])) <= b.width / 2 &&
         std::abs(dot(d, a[1])) <= b.length / 2;
}
static std::optional<Real>
search(const std::function<Real(Real)> &f,
       const std::function<Real(Real, Real)> &speed, Real lo, Real hi,
       Solver &s, int depth = 0,
       const std::function<bool(Real, Real)> &disjoint = {}) {
  if (++s.evaluations > s.limit)
    throw std::runtime_error("geometry_iteration_limit");
  const Real roundoff = 1e-10L;
  Real m = (lo + hi) / 2, g = f(m), v = speed(lo, hi), h = (hi - lo) / 2;
  if (g > v * h + roundoff)
    return {};
  Real a = f(lo);
  if (a <= 0)
    return lo;
  if (v == 0)
    return {};
  // Isolate to below BOTH geometric and temporal targets; rounding floor only
  // resolves numerically indistinguishable tangencies, not configured weapon
  // ranges.
  if ((hi - lo) <=
      std::min(s.timeTolerance, s.distanceTolerance / std::max(v, 1.L))) {
    if (g <= 0 || f(hi) <= 0) {
      Real left = lo, right = g <= 0 ? m : hi;
      for (int i = 0; i < 48 && right - left > 1e-13L; i++) {
        Real q = (left + right) / 2;
        if (f(q) <= 0)
          right = q;
        else
          left = q;
      }
      return right;
    }
    // Time and separation are already within both requested tolerances.
    // Further bisection of an indistinguishable tangency can exhaust the
    // step budget without gaining a representable separating distance.
    if (g <= roundoff)
      return m;
    if (depth >= 52 || hi - lo < 1e-14L)
      return {};
  }
  if (depth >= 4 && disjoint && disjoint(lo, hi))
    return {};
  auto left = search(f, speed, lo, m, s, depth + 1, disjoint);
  if (left)
    return left;
  return search(f, speed, m, hi, s, depth + 1, disjoint);
}
static Vec nearestOnSegment(Vec p, Vec a, Vec b) {
  Vec v = b - a;
  Real d = dot(v, v);
  return a + v * (d ? std::clamp(dot(p - a, v) / d, 0.L, 1.L) : 0);
}
Separation coastGap(const Body &b, const Coast &c, Real t) {
  auto pose = b.motion.at(t);
  auto ax = axes(pose.heading);
  Vec best{}, nearestShore{};
  Real smallest = std::numeric_limits<Real>::infinity();
  auto test = [&](Vec delta, Vec backbone) {
    Real d = dot(delta, delta);
    if (d < smallest) {
      smallest = d;
      best = delta;
      nearestShore = backbone;
    }
  };
  for (Vec point : {c.a, c.b}) {
    Vec d = point - pose.p;
    Vec nearest =
        pose.p + ax[0] * std::clamp(dot(d, ax[0]), -b.width / 2, b.width / 2) +
        ax[1] * std::clamp(dot(d, ax[1]), -b.length / 2, b.length / 2);
    test(nearest - point, point);
  }
  for (auto p : corners(b, t)) {
    Vec q = nearestOnSegment(p, c.a, c.b);
    test(p - q, q);
  }
  // A backbone crossing the rectangle can have both endpoints outside.
  Real enter = 0, leave = 1;
  for (int i = 0; i < 2; i++) {
    Real origin = dot(c.a - pose.p, ax[i]), delta = dot(c.b - c.a, ax[i]);
    Real half = (i ? b.length : b.width) / 2;
    if (delta == 0) {
      if (std::abs(origin) > half)
        enter = 2;
    } else {
      Real a = (-half - origin) / delta, bound = (half - origin) / delta;
      enter = std::max(enter, std::min(a, bound));
      leave = std::min(leave, std::max(a, bound));
    }
  }
  if (enter <= leave)
    return {-c.radius, {}, pose.p};
  Real d = std::sqrt(smallest);
  Vec normal = d ? best * (1 / d) : Vec{};
  return {d - c.radius, normal, nearestShore + normal * c.radius};
}
// Entry of a ray into the union of an oriented strip and its two circular
// ends. Parameter is distance along the ray. Outgoing roots are excluded.
static void rayCapsule(Vec origin, Vec ray, Vec a, Vec b, Real r, bool tangency,
                       Real &best) {
  auto circle = [&](Vec center) {
    Vec d = center - origin;
    Real along = dot(d, ray), cross = d.x * ray.y - d.y * ray.x;
    Real discriminant = r * r - cross * cross;
    if (discriminant < 0 || (!tangency && discriminant == 0))
      return;
    Real distance = along - std::sqrt(discriminant);
    if (distance >= -1e-9L && distance <= best)
      best = std::max(0.L, distance);
  };
  circle(a);
  if (a.x == b.x && a.y == b.y)
    return;
  circle(b);
  Vec basis = b - a;
  Real len = norm(basis);
  basis = basis * (1 / len);
  Vec normal{basis.y, -basis.x};
  Real rate = dot(ray, normal);
  if (rate == 0)
    return;
  Real edge = rate < 0 ? r : -r;
  Real distance = (edge - dot(origin - a, normal)) / rate;
  if (distance < -1e-9L || distance > best)
    return;
  Real x = dot(origin + ray * distance - a, basis);
  if (x >= 0 && x <= len)
    best = std::max(0.L, distance);
}
std::optional<Real> coastFraction(Vec start, Vec end, const Coast &c) {
  const Real r = c.radius;
  if (std::min(start.x, end.x) > std::max(c.a.x, c.b.x) + r ||
      std::max(start.x, end.x) < std::min(c.a.x, c.b.x) - r ||
      std::min(start.y, end.y) > std::max(c.a.y, c.b.y) + r ||
      std::max(start.y, end.y) < std::min(c.a.y, c.b.y) - r)
    return {};
  Vec offset = start - nearestOnSegment(start, c.a, c.b);
  if (dot(offset, offset) <= r * r)
    return 0;
  Vec delta = end - start;
  Real distance = norm(delta);
  if (!distance)
    return {};
  Real first = std::numeric_limits<Real>::infinity();
  rayCapsule(start, delta * (1 / distance), c.a, c.b, r, true, first);
  if (first > distance + 1e-9L)
    return {};
  return std::min(1.L, first / distance);
}
std::optional<Real> coastContact(const Body &b, const Coast &c, Real lo,
                                 Real hi, Solver &s, bool projectile) {
  if (hi < lo)
    return {};
  Vec p = b.motion.at(lo).p;
  Real extent =
      b.radius() + c.radius + (hi - lo) * b.motion.bound(b.radius(), lo, hi);
  if (p.x < std::min(c.a.x, c.b.x) - extent ||
      p.x > std::max(c.a.x, c.b.x) + extent ||
      p.y < std::min(c.a.y, c.b.y) - extent ||
      p.y > std::max(c.a.y, c.b.y) + extent)
    return {};
  auto gap = [&](Real time) { return coastGap(b, c, time).distance; };
  Real initial = b.length == 0 && b.width == 0
                     ? norm(p - nearestOnSegment(p, c.a, c.b)) - c.radius
                     : gap(lo);
  if (projectile && initial <= 1e-9L)
    return lo;
  hi = std::min(hi, b.motion.stoppedAt);
  if (hi <= lo || b.motion.bound(b.radius(), lo, hi) == 0)
    return {};
  if (b.motion.angular == 0) {
    auto pose = b.motion.at(lo);
    Vec ray = direction(pose.heading);
    Real best = std::numeric_limits<Real>::infinity();
    if (b.length == 0 && b.width == 0) {
      rayCapsule(pose.p, ray, c.a, c.b, c.radius, projectile, best);
    } else {
      // First contact is either a hull corner at the coast, or an island
      // endpoint radius touching an edge of the translating rectangle.
      auto hull = corners(b, lo);
      for (unsigned i = 0; i < 4; i++) {
        rayCapsule(hull[i], ray, c.a, c.b, c.radius, projectile, best);
        for (Vec endpoint : {c.a, c.b})
          rayCapsule(endpoint, ray * (-1), hull[i], hull[(i + 1) % 4], c.radius,
                     projectile, best);
      }
    }
    Real travel = b.motion.distance(hi) - b.motion.distance(lo);
    if (best > travel + 1e-9L)
      return {};
    if (best == 0)
      return lo;
    Real terminalSpeed = std::sqrt(std::max(
        0.L, pose.speed * pose.speed + 2 * b.motion.acceleration * best));
    return std::min(hi, lo + 2 * best / (pose.speed + terminalSpeed));
  }
  if (initial <= 1e-8L) {
    Real probe = std::min(hi, lo + 1e-6L);
    if (gap(probe) < initial - 1e-12L)
      return lo;
    lo = probe;
  }
  return search(
      gap, [&](Real a, Real z) { return b.motion.bound(b.radius(), a, z); }, lo,
      hi, s);
}
static void roots(Real a, Real b, Real c, Real lo, Real hi,
                  std::vector<Real> &out) {
  if (std::abs(a) < 1e-25L) {
    if (std::abs(b) > 1e-25L) {
      Real t = -c / b;
      if (t >= lo && t <= hi)
        out.push_back(t);
    }
    return;
  }
  Real discriminant = b * b - 4 * a * c;
  if (discriminant < 0)
    return;
  Real d = std::sqrt(discriminant), q = -.5L * (b + std::copysign(d, b));
  Real t1 = q / a, t2 = q == 0 ? -b / (2 * a) : c / q;
  if (t1 >= lo && t1 <= hi)
    out.push_back(t1);
  if (t2 >= lo && t2 <= hi)
    out.push_back(t2);
}
static Vec velocity(const Motion &m) {
  return direction(m.initial.heading) * m.initial.speed;
}
static Vec accel(const Motion &m) {
  return direction(m.initial.heading) * m.acceleration;
}
// Constant heading gives quadratic coordinates, allowing exact interval roots
// including tangencies, parallel grazing and contact at the final endpoint.
static std::optional<Real> quadratic_contact(const Body &a, const Body &b,
                                             Real lo, Real hi) {
  std::vector<Real> partitions = {lo, hi};
  for (auto &m : {a.motion, b.motion})
    if (m.stoppedAt > lo && m.stoppedAt < hi)
      partitions.push_back(m.stoppedAt);
  std::sort(partitions.begin(), partitions.end());
  for (size_t part = 0; part + 1 < partitions.size(); part++) {
    Real begin = partitions[part], end = partitions[part + 1];
    auto poly = [&](const Motion &m) {
      if (m.stoppedAt <= begin)
        return std::array<Vec, 3>{m.at(begin).p, Vec{}, Vec{}};
      return std::array<Vec, 3>{m.initial.p, velocity(m), accel(m) * .5L};
    };
    auto ap = poly(a.motion), bp = poly(b.motion);
    auto aa = axes(a.motion.initial.heading),
         ab = axes(b.motion.initial.heading);
    std::vector<Real> candidates = {begin, end};
    for (Vec axis : {aa[0], aa[1], ab[0], ab[1]}) {
      Real radius = std::abs(dot(axis, aa[0])) * a.width / 2 +
                    std::abs(dot(axis, aa[1])) * a.length / 2 +
                    std::abs(dot(axis, ab[0])) * b.width / 2 +
                    std::abs(dot(axis, ab[1])) * b.length / 2;
      Real c = dot(ap[0] - bp[0], axis), v = dot(ap[1] - bp[1], axis),
           acc = dot(ap[2] - bp[2], axis);
      roots(acc, v, c - radius, begin, end, candidates);
      roots(acc, v, c + radius, begin, end, candidates);
    }
    std::sort(candidates.begin(), candidates.end());
    for (Real t : candidates)
      if (gap(a, b, t) <= 1e-10L)
        return t;
  }
  return {};
}
// Fixed-centre rotation has sinusoidal vertex/edge crossing equations.
// Enumerating all their roots also captures interior tangency double roots.
static std::optional<Real> rotating_fixed_contact(const Body &rotating,
                                                  const Body &fixed, Real lo,
                                                  Real hi) {
  Real theta0 = rotating.motion.initial.heading * pi / 180,
       w = rotating.motion.angular;
  std::vector<Real> times = {lo, hi};
  auto equation = [&](Real a, Real b, Real c) {
    Real amplitude = std::hypotl(a, b);
    if (amplitude == 0)
      return;
    Real ratio = c / amplitude;
    if (std::abs(c) > amplitude + 1e-10L)
      return;
    if (std::abs(std::abs(c) - amplitude) <= 1e-10L)
      ratio = std::copysign(1.L, c);
    ratio = std::clamp(ratio, -1.L, 1.L);
    Real phase = std::atan2(b, a), offset = std::acos(ratio);
    Real tmin = std::min(theta0 + w * lo, theta0 + w * hi),
         tmax = std::max(theta0 + w * lo, theta0 + w * hi);
    for (Real root : {phase - offset, phase + offset}) {
      int first = int(std::floor((tmin - root) / (2 * pi))) - 1,
          last = int(std::ceil((tmax - root) / (2 * pi))) + 1;
      for (int k = first; k <= last; k++) {
        Real t = (root + k * 2 * pi - theta0) / w;
        if (t >= lo - 1e-12L && t <= hi + 1e-12L)
          times.push_back(std::clamp(t, lo, hi));
      }
    }
  };
  Vec delta = rotating.motion.initial.p - fixed.motion.initial.p;
  auto ax = axes(fixed.motion.initial.heading);
  for (Real u : {-rotating.width / 2, rotating.width / 2})
    for (Real v : {-rotating.length / 2, rotating.length / 2}) {
      for (int axis = 0; axis < 2; axis++) {
        Real half = axis ? fixed.length / 2 : fixed.width / 2;
        for (Real edge : {-half, half})
          equation(u * ax[axis].x + v * ax[axis].y,
                   v * ax[axis].x - u * ax[axis].y,
                   edge - dot(delta, ax[axis]));
      }
    }
  for (auto vertex : corners(fixed, 0)) {
    Vec d = vertex - rotating.motion.initial.p;
    for (Real edge : {-rotating.width / 2, rotating.width / 2})
      equation(d.x, -d.y, edge);
    for (Real edge : {-rotating.length / 2, rotating.length / 2})
      equation(d.y, d.x, edge);
  }
  std::sort(times.begin(), times.end());
  for (Real t : times)
    if (gap(rotating, fixed, t) <= 1e-10L)
      return t;
  return {};
}
// Project each opposing corner into the moving frame of a hull face.
// A scalar projection with |f''| <= M differs from its endpoint chord by
// at most M * dt^2 / 8. One continuously separating face excludes contact.
static bool sweptDisjoint(const Body &a, const Body &b, Real lo, Real hi) {
  for (const auto *body : {&a, &b})
    if (body->motion.stoppedAt > lo && body->motion.stoppedAt < hi)
      return false;
  auto separates = [&](const Body &base, const Body &other) {
    const Real dt = hi - lo;
    auto p = base.motion.at(lo), q = other.motion.at(lo);
    const Real w = std::abs(base.motion.angular),
               v = base.motion.bound(0, lo, hi);
    const Real ow = std::abs(other.motion.angular),
               ov = other.motion.bound(other.radius(), lo, hi);
    const Real accel = std::abs(base.motion.acceleration) + w * v +
                       std::abs(other.motion.acceleration) + ow * ov;
    const Real maxDistance = norm(q.p - p.p) + other.radius() + dt * (v + ov);
    const Real slack =
        (accel + 2 * w * (v + ov) + w * w * maxDistance) * dt * dt / 8 + 2e-12L;
    Real gaps[4];
    std::fill_n(gaps, 4, std::numeric_limits<Real>::infinity());
    for (Real t : {lo, hi}) {
      auto bp = base.motion.at(t), op = other.motion.at(t);
      auto ba = axes(bp.heading), oa = axes(op.heading);
      for (int k = 0; k < 2; k++) {
        const Real d = dot(op.p - bp.p, ba[k]);
        const Real r = (k ? base.length : base.width) / 2 +
                       (std::abs(dot(oa[0], ba[k])) * other.width +
                        std::abs(dot(oa[1], ba[k])) * other.length) /
                           2;
        gaps[2 * k] = std::min(gaps[2 * k], d - r);
        gaps[2 * k + 1] = std::min(gaps[2 * k + 1], -d - r);
      }
    }
    return *std::max_element(gaps, gaps + 4) > slack;
  };
  return separates(a, b) || separates(b, a);
}
std::optional<Real> contact(const Body &a, const Body &b, Real lo, Real hi,
                            Solver &s) {
  if (lo > hi)
    return {};
  if (a.motion.angular == 0 && b.motion.angular == 0) {
    s.evaluations++;
    if (lo == hi)
      return gap(a, b, lo) <= 0 ? std::optional<Real>(lo) : std::nullopt;
    return quadratic_contact(a, b, lo, hi);
  }
  auto stationaryCenter = [](const Body &b) {
    return b.motion.initial.speed == 0 && b.motion.acceleration == 0 &&
           b.motion.stoppedAt > 1;
  };
  if (stationaryCenter(a) && stationaryCenter(b)) {
    if (a.motion.angular != 0 && b.motion.angular == 0) {
      s.evaluations++;
      return rotating_fixed_contact(a, b, lo, hi);
    }
    if (b.motion.angular != 0 && a.motion.angular == 0) {
      s.evaluations++;
      return rotating_fixed_contact(b, a, lo, hi);
    }
  }
  Real reach =
      a.radius() + b.radius() +
      (a.motion.bound(0, lo, hi) + b.motion.bound(0, lo, hi)) * (hi - lo);
  if (norm(a.motion.at(lo).p - b.motion.at(lo).p) > reach + 1e-9L)
    return {};
  return search([&](Real t) { return gap(a, b, t); },
                [&](Real l, Real h) {
                  return a.motion.bound(a.radius(), l, h) +
                         b.motion.bound(b.radius(), l, h);
                },
                lo, hi, s, 0,
                [&](Real l, Real h) { return sweptDisjoint(a, b, l, h); });
}
std::optional<Real> boundary(const Body &b, Real lo, Real hi, Real size,
                             Solver &s) {
  if (lo > hi || b.motion.bound(b.radius(), lo, hi) == 0)
    return {};
  Real start = lo;
  if (b.motion.angular == 0) {
    Real end = std::min(hi, b.motion.stoppedAt);
    if (end < lo)
      return {};
    auto v = velocity(b.motion), acc = accel(b.motion);
    std::vector<Real> candidates;
    for (auto p : corners(b, 0))
      for (int axis = 0; axis < 2; axis++) {
        Real c = axis ? p.y : p.x, d = axis ? v.y : v.x,
             a = (axis ? acc.y : acc.x) / 2;
        for (Real wall : {0.L, size}) {
          std::vector<Real> rr;
          roots(a, d, c - wall, lo, end, rr);
          for (Real t : rr) {
            Real inward = (wall == 0 ? 1 : -1) * (d + 2 * a * t);
            if (t > lo + 1e-14L || inward < 0 ||
                (inward == 0 && (wall == 0 ? 1 : -1) * a < 0))
              candidates.push_back(t);
          }
        }
      }
    s.evaluations++;
    if (candidates.empty())
      return {};
    return *std::min_element(candidates.begin(), candidates.end());
  }
  if (margin(b, lo, size) <= 1e-10L) {
    // A hull resting on a wall may sail back inside; stationary/tangent
    // contact is legal until the path would leave the closed map.
    Real probe = std::min(hi, lo + 1e-7L);
    Real v = margin(b, probe, size);
    if (v < 0)
      return lo;
    if (v > 0)
      start = probe;
    else { // e.g. travelling parallel to a wall: inspect only negative margins.
      return search(
          [&](Real t) { return margin(b, t, size) + 1e-10L; },
          [&](Real l, Real h) { return b.motion.bound(b.radius(), l, h); },
          probe, hi, s);
    }
  }
  return search(
      [&](Real t) { return margin(b, t, size); },
      [&](Real l, Real h) { return b.motion.bound(b.radius(), l, h); }, start,
      hi, s);
}
std::optional<Real> exit_body(const Body &point, const Body &ship, Real lo,
                              Real hi, Solver &s) {
  return search([&](Real t) { return -gap(point, ship, t) + 1e-10L; },
                [&](Real l, Real h) {
                  return point.motion.bound(0, l, h) +
                         ship.motion.bound(ship.radius(), l, h);
                },
                lo, hi, s);
}
} // namespace naval
