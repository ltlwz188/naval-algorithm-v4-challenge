#include "world.hpp"
namespace game {
namespace {
double reach(const Body &b) { return b.radius() + b.motion.bound(0, 0, 1); }
bool boxesOverlap(Vec a, Vec b, double combinedRadius) {
  return std::abs(a.x - b.x) <= combinedRadius &&
         std::abs(a.y - b.y) <= combinedRadius;
}
naval::Coast coast(const Island &i) {
  return {{i.ax, i.ay}, {i.bx, i.by}, i.radius};
}
} // namespace
void World::moveShots(double elapsed, double duration,
                      const std::array<double, 36> &extent,
                      naval::Solver &solver) {
  std::erase_if(shots, [&](Shot &s) {
    if (s.kind == Shell) {
      double f = double(t + elapsed + duration - s.born) / (s.due - s.born);
      if (s.blockingIsland >= 0 && f >= s.shoreFraction) {
        Vec p = s.origin + (s.target - s.origin) * s.shoreFraction;
        events.push_back({ShellBlocked, t + 1, s.source, s.blockingIsland, 0,
                          s.id, double(p.x), double(p.y)});
        return true;
      }
      s.p = s.origin + (s.target - s.origin) * f;
      return false;
    }
    if (s.kind == Bomb)
      return false;
    double traveled = (t + elapsed - s.born) * s.speed;
    double hi = std::min(duration, (s.limit - traveled) / s.speed);
    Vec velocity = direction(s.heading) * s.speed;
    for (auto [pos, v] :
         {std::pair{s.p.x, velocity.x}, std::pair{s.p.y, velocity.y}}) {
      if (v > 0)
        hi = std::min(hi, double((100000 - pos) / v));
      if (v < 0)
        hi = std::min(hi, double(-pos / v));
    }
    double lo = std::max(0., (s.arm - traveled) / s.speed);
    Body torp{{{s.p, s.heading, s.speed}}, 0, 0};
    double best = 2;
    int victim = -1;
    if (lo <= hi)
      for (auto &e : entities)
        if (e.onMap() && !e.air() &&
            boxesOverlap(s.p, e.pos(), s.speed + extent[e.index])) {
          if (auto at = naval::contact(torp, bodies[e.index], lo, hi, solver);
              at && *at < best) {
            best = *at;
            victim = e.index;
          }
        }
    double water = s.kind == AirTorpedo ? lo : 0;
    for (unsigned j = 0; j < chart.islandCount; ++j) {
      if (auto shore = naval::coastContact(torp, coast(chart.islands[j]), water,
                                           std::min(hi, best), solver, true);
          shore && *shore <= best)
        return true;
    }
    if (victim >= 0) {
      pendingHits.push_back({s, victim, s.p + velocity * best});
      return true;
    }
    s.p = s.p + velocity * hi;
    return hi < duration || traveled + s.speed >= s.limit;
  });
}
void World::move() {
  naval::Solver solver;
  double elapsed = 0;
  while (elapsed < 1) {
    const double remaining = 1 - elapsed;
    double when = remaining;
    enum Collision { None, Boundary, Ship, Land } event = None;
    int first = -1, second = -1;
    std::array<double, 36> extent{};
    for (auto &e : entities) {
      if (!e.onMap())
        continue;
      auto &b = bodies[e.index];
      extent[e.index] = reach(b);
      if (!b.motion.bound(b.radius(), 0, remaining))
        continue;
      Vec p = e.pos();
      if (std::min({p.x, p.y, 100000 - p.x, 100000 - p.y}) <= extent[e.index]) {
        if (auto at = naval::boundary(b, 0, when, 100000, solver);
            at && (!e.air() || *at < remaining ||
                   naval::margin(b, remaining, 100000) < 0)) {
          when = *at;
          first = e.index;
          event = Boundary;
        }
      }
      for (unsigned j = 0; j < chart.islandCount; j++) {
        const Island &island = chart.islands[j];
        if (e.air() && island.height != HighIsland)
          continue;
        const auto radius = island.radius + extent[e.index];
        if (p.x < std::min(island.ax, island.bx) - radius ||
            p.x > std::max(island.ax, island.bx) + radius ||
            p.y < std::min(island.ay, island.by) - radius ||
            p.y > std::max(island.ay, island.by) + radius)
          continue;
        if (auto at = naval::coastContact(b, coast(island), 0, when, solver,
                                          e.air())) {
          when = *at;
          first = e.index;
          second = j;
          event = Land;
        }
      }
    }
    for (int i = 0; i < 36; i++) {
      auto &a = entities[i];
      if (!a.onMap() || a.air() || a.u.mode == Submerged)
        continue;
      for (int j = i + 1; j < 36; j++) {
        auto &b = entities[j];
        if (!b.onMap() || b.air() || b.u.mode == Submerged ||
            !boxesOverlap(a.pos(), b.pos(), extent[i] + extent[j]))
          continue;
        auto &left = bodies[i], &right = bodies[j];
        if (left.motion.bound(left.radius(), 0, remaining) == 0 &&
            right.motion.bound(right.radius(), 0, remaining) == 0)
          continue;
        double lo = 0;
        if (naval::gap(left, right, 0) <= 1e-9L) {
          double probe = std::min(when, 1e-5);
          if (naval::gap(left, right, probe) > naval::gap(left, right, 0))
            lo = probe;
        }
        if (auto at = naval::contact(left, right, lo, when, solver);
            at &&
            (*at < when || event != Ship ||
             std::minmax(a.u.id, b.u.id) <
                 std::minmax(entities[first].u.id, entities[second].u.id))) {
          when = *at;
          first = i;
          second = j;
          event = Ship;
        }
      }
    }
    if (when > 0)
      moveShots(elapsed, when, extent, solver);
    for (auto &e : entities)
      if (e.onMap()) {
        auto &motion = bodies[e.index].motion;
        auto p = motion.at(when);
        e.u.x = p.p.x;
        e.u.y = p.p.y;
        e.u.heading = p.heading;
        e.u.speed = p.speed;
        motion.initial = p;
      }
    elapsed += when;
    if (event == None)
      break;
    auto stop = [&](int index) {
      auto &m = bodies[index].motion;
      m.initial.speed = 0;
      m.acceleration = 0;
      m.angular = 0;
      entities[index].u.speed = 0;
    };
    auto &e = entities[first];
    if (event == Land && e.air()) {
      events.push_back(
          {AirCrash, t + 1, first, second, e.u.hp, 0, e.u.x, e.u.y});
      auto &observed = delayed[1 - e.team];
      if (std::any_of(observed.contacts.begin(), observed.contacts.end(),
                      [&](const Contact &c) { return c.id == e.contact; }))
        observed.losses.push_back({e.contact, t + 1, 0});
      std::fill_n(e.u.memberHP, 8, 0.);
      e.u.hp = 0;
      continue;
    }
    if (event == Boundary || event == Ship) {
      stop(first);
      if (event == Ship)
        stop(second);
      else if (e.air()) {
        std::fill_n(e.u.memberHP, 8, 0.);
        e.u.hp = 0;
      }
      continue;
    }
    auto &motion = bodies[first].motion;
    auto contact =
        naval::coastGap(bodies[first], coast(chart.islands[second]), 0);
    auto n = contact.normal;
    Vec forward = direction(e.u.heading);
    double inward = std::max(0., -double(naval::dot(n, forward)) * e.u.speed);
    double base = e.u.kind == Submarine && e.u.mode != Surface
                      ? 90
                      : stats[e.u.kind].speed;
    double amount = stats[e.u.kind].hp * .5 * (inward / base) * (inward / base);
    shoreDamage[first] += amount;
    events.push_back({ShoreImpact, t + 1, first, second, amount, 0,
                      double(contact.point.x), double(contact.point.y)});
    motion.angular = 0;
    motion.acceleration = 0;
    if (inward > 1e-9) {
      Vec reflected = forward - n * (2 * naval::dot(n, forward));
      motion.initial.heading = e.u.heading = naval::heading(
          std::atan2(reflected.x, reflected.y) * 180 / naval::pi);
    }
    // An exact tangent is legal. Remove roundoff at an incoming contact.
    motion.initial.p = motion.initial.p + n * 1e-7L;
    e.u.x = motion.initial.p.x;
    e.u.y = motion.initial.p.y;
  }
}
} // namespace game
