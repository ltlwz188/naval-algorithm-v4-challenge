#include "world.hpp"
#include <limits>
namespace game {
bool World::act(int side, const Action &a) {
  auto first = entities.begin() + side * 18;
  auto it = std::find_if(first, first + 18,
                         [&](const Entity &e) { return e.u.id == a.unit; });
  if (it == first + 18 || !std::isfinite(a.x) || !std::isfinite(a.y))
    return false;
  auto &e = *it;
  auto &u = e.u;
  auto &s = stats[u.kind];
  auto &body = bodies[e.index];
  if (u.hp <= 0)
    return false;
  Vec aim{a.x, a.y};
  auto mother = [&]() -> Unit & {
    return entities[side * 18 + (e.index % 18 - 14) / 2].u;
  };
  auto launchShot = [&](int kind, Vec origin, Vec target, double bearing,
                        double speed, double damage, double limit, double arm,
                        int due, int n = 1) {
    shots.push_back({newId(), side, kind, t, due, n, origin, target, origin,
                     bearing, speed, damage, limit, arm});
    shots.back().source = e.index;
    shotsFired++;
    events.push_back({ShotReleased, t, e.index, kind, damage, shots.back().id});
  };
  switch (a.verb) {
  case UseSkill:
    return useSkill(e, a);
  case Move: {
    bool boosted = u.activeUntil[Boost] > t;
    double cap = u.kind == Submarine && u.mode != Surface
                     ? 90
                     : s.speed * (boosted ? 1.2 : 1.);
    if (!e.activeAtStart || !e.onMap() || a.y < (e.air() ? 160 : 0) ||
        a.y > cap)
      return false;
    body.motion = naval::maneuver(
        {e.pos(), u.heading, u.speed}, a.x, u.lockedUntil > t ? 0 : a.y,
        s.acc * (boosted ? 2 : 1), s.turn * (boosted ? 2 : 1));
    return true;
  }
  case Dive:
  case SurfaceSub: {
    if (u.kind != Submarine ||
        (a.verb == Dive ? (u.mode != Surface || u.speed > 60 || !u.oxygen)
                        : u.mode != Submerged))
      return false;
    u.mode = a.verb == Dive ? Diving : Surfacing;
    u.transitionUntil = t + 25;
    return true;
  }
  case Launch: {
    if (!e.air() || u.mode != Deck || u.readyAt > t ||
        mother().nextLaunch > t || mother().hp <= 0 || a.x < 160 ||
        a.x > s.speed)
      return false;
    u.mode = Flying;
    u.heading = mother().heading;
    u.speed = a.x;
    u.x = mother().x;
    u.y = mother().y;
    body.motion = {{e.pos(), u.heading, u.speed}};
    mother().nextLaunch = t + 50;
    return true;
  }
  case Recover:
    if (!e.air() || e.modeAtStart != Flying || u.mode != Flying ||
        !inside(e.pos(), {mother().x, mother().y}, 1000))
      return false;
    u.mode = Recovering;
    e.recoveryEnd = t + 10;
    return true;
  case MainGun:
  case SecondaryGun: {
    bool secondary = a.verb == SecondaryGun;
    if ((secondary && u.kind != Battleship) ||
        (!secondary && (u.kind < Battleship || u.kind > Destroyer)))
      return false;
    int n = secondary ? 20 : u.kind == Battleship ? 12 : 10;
    int reach = secondary              ? 15000
                : u.kind == Battleship ? 25000
                : u.kind == Cruiser    ? 20000
                                       : 16000;
    int cooldown = secondary              ? 40
                   : u.kind == Battleship ? 60
                   : u.kind == Cruiser    ? 30
                                          : 25;
    double damage = secondary              ? 800
                    : u.kind == Battleship ? 2500
                    : u.kind == Cruiser    ? 1500
                                           : 1000;
    if (a.slot >= n || a.slot < -1 || a.count < 0 ||
        !inside(e.pos(), aim, reach))
      return false;
    auto *at = secondary ? u.secondaryAt : u.mainAt;
    int fired = 0;
    double distance = naval::norm(aim - e.pos());
    int travel = std::max(1, int(std::ceil(distance / (reach / 10.))));
    // All slots in this command share one segment through static terrain.
    int blocker = -1;
    double firstFraction = 2;
    for (unsigned i = 0; i < chart.islandCount; ++i) {
      const auto &island = chart.islands[i];
      if (island.height == LowIsland)
        continue;
      auto at = naval::coastFraction(
          e.pos(), aim,
          {{island.ax, island.ay}, {island.bx, island.by}, island.radius});
      if (at && *at < firstFraction) {
        firstFraction = *at;
        blocker = int(i);
      }
    }
    for (int j = 0; j < n; j++)
      if ((a.slot < 0 || j == a.slot) && at[j] <= t &&
          (!a.count || fired < a.count)) {
        launchShot(Shell, e.pos(), aim, 0, 0, damage, distance, 0, t + travel);
        shots.back().blockingIsland = blocker;
        shots.back().shoreFraction = firstFraction;
        if (!secondary && (u.slowMask & (1u << j))) {
          shots.back().slow = true;
          u.slowMask &= ~(1u << j);
        }
        at[j] = t + cooldown;
        ++fired;
      }
    if (fired && !flashed[e.index] && !inSmoke(e.pos())) {
      sample[1 - side].flashes.push_back({u.x, u.y, t, 0});
      flashed[e.index] = true;
    }
    return fired > 0;
  }
  case Torpedo: {
    if (u.kind != Cruiser && u.kind != Destroyer && u.kind != Submarine)
      return false;
    if (u.mode == Diving || u.mode == Surfacing || torpedoFired[e.index])
      return false;
    int groups = u.kind == Destroyer ? 3 : u.kind == Cruiser ? 2 : 8;
    int n = u.kind == Destroyer ? 5 : u.kind == Cruiser ? 4 : 1;
    if (a.slot < 0 || a.slot >= groups || u.torpedoAt[a.slot] > t ||
        (n > 1 && a.y != 5 && a.y != 15))
      return false;
    for (int j = 0; j < n; j++) {
      double h =
          naval::heading(a.x + (n == 1 ? 0 : a.y * (2. * j / (n - 1) - 1)));
      launchShot(ShipTorpedo, e.pos(), {}, h,
                 u.kind == Destroyer
                     ? ((u.torpedoMask & (1u << a.slot)) ? 600 : 400)
                 : u.kind == Cruiser ? 200
                                     : 300,
                 u.kind == Destroyer ? 10000 : 8000,
                 u.kind == Destroyer ? 25000 : 18000, 1200, 0);
    }
    u.torpedoMask &= ~(1u << a.slot);
    u.torpedoAt[a.slot] = t + (u.kind == Destroyer ? 50 : 80);
    torpedoFired[e.index] = true;
    return true;
  }
  case AAGun:
    if (a.slot < 0 || a.slot >= s.aa || u.aaAt[a.slot] > t ||
        !inside(e.pos(), aim, s.aaRange))
      return false;
    volleys.push_back({t + 1, side, u.kind, aim});
    u.aaAt[a.slot] = t + 10;
    return true;
  case Drop: {
    if (!e.air() || e.modeAtStart != Flying || u.mode != Flying ||
        t < u.readyAt || !inside(e.pos(), aim, 130))
      return false;
    if (u.kind == TorpedoPlane &&
        (!std::isfinite(a.spacing) || a.spacing < 0 || a.spacing > 25))
      return false;
    int n = 0;
    for (int i = 0; i < 8; i++)
      if (u.memberHP[i] > 0 && u.ammo[i] > 0) {
        u.ammo[i]--;
        n++;
      }
    if (!n)
      return false;
    if (u.kind == Bomber)
      launchShot(Bomb, aim, aim, 0, 0, 4000, 0, 0, t + 5, n);
    else
      for (int j = 0; j < n; j++)
        launchShot(AirTorpedo,
                   aim + direction(u.heading + 90) * (j * a.spacing), {},
                   u.heading, 300, 5000, 8000, 600, 0);
    u.readyAt = t + 40;
    return true;
  }
  default:
    return false;
  }
}
void World::selectBursts() {
  dueBursts = {};
  for (auto &e : entities) {
    auto &u = e.u;
    const auto &s = stats[u.kind];
    if (!s.burst || !e.onMap())
      continue;
    if (e.burstDue == t) {
      dueBursts[e.burstTarget] += s.burst;
      e.burstDue = -1;
    }
    if (u.burstAt > t)
      continue;
    std::tuple<double, uint64_t, int> best{
        std::numeric_limits<double>::infinity(), 0, 0};
    int target = -1;
    for (auto &p : entities) {
      if (!p.air() || p.team == e.team || !p.onMap() ||
          !inside(e.pos(), p.pos(), s.burstRange))
        continue;
      for (int i = 0; i < 8; i++)
        if (p.u.memberHP[i] > 0) {
          auto key = std::make_tuple(p.u.memberHP[i], p.contact, i);
          if (key < best) {
            target = p.index;
            best = key;
          }
        }
    }
    if (target >= 0) {
      e.burstTarget = target;
      e.burstDue = t + 1;
      u.burstAt = t + 5;
    }
  }
}
void World::flak() {
  std::array<std::array<double, 8>, 36> cannon{};
  for (auto &v : volleys)
    if (v.due == t) {
      for (Vec offset :
           {Vec{0, 0}, Vec{80, 0}, Vec{-80, 0}, Vec{0, 80}, Vec{0, -80}}) {
        std::array<int, 4> hit{};
        int count = 0, n = 0;
        for (auto &p : entities)
          if (p.team != v.team && p.air() && p.activeAtStart &&
              inside(p.pos(), v.aim + offset, 130)) {
            hit[count++] = p.index;
            n += std::count_if(p.u.memberHP, p.u.memberHP + 8,
                               [](double x) { return x > 0; });
          }
        if (!n)
          continue;
        ++aaHits;
        for (int j = 0; j < count; j++)
          events.push_back({FlakPoint, t, hit[j], v.kind,
                            double(stats[v.kind].aaPoint) / n, 0});
        for (int j = 0; j < count; j++)
          for (int i = 0; i < 8; i++)
            if (entities[hit[j]].u.memberHP[i] > 0)
              cannon[hit[j]][i] += double(stats[v.kind].aaPoint) / n;
      }
    }
  std::erase_if(volleys, [&](auto &v) { return v.due <= t; });
  for (auto &p : entities)
    if (p.air() && p.onMap()) {
      auto hp = p.u.memberHP;
      double before[8];
      std::copy(hp, hp + 8, before);
      for (int i = 0; i < 8; i++)
        hp[i] -= cannon[p.index][i];
      double rem = dueBursts[p.index];
      while (rem > 0) {
        int lowest = -1;
        for (int i = 0; i < 8; i++)
          if (hp[i] > 0 && (lowest < 0 || hp[i] < hp[lowest]))
            lowest = i;
        if (lowest < 0)
          break;
        double used = std::min(hp[lowest], rem);
        hp[lowest] -= used;
        rem -= used;
      }
      if (std::none_of(hp, hp + 8, [](auto h) { return h > 0; }))
        witnessLoss(p);
      for (int j = 0; j < 8; j++)
        if (before[j] != hp[j])
          events.push_back({PlaneDamage, t, p.index, j, before[j] - hp[j], 0});
    }
}
void World::resolveShots() {
  std::array<int, 36> reloadHits{};
  std::erase_if(shots, [&](Shot &s) {
    if (s.kind != Shell && s.kind != Bomb)
      return false;
    if (t < s.due)
      return false;
    bool enemyHit = false;
    for (auto &e : entities) {
      if (!e.onMap() || e.air() || e.u.mode == Submerged)
        continue;
      double hits = 0;
      if (s.kind == Bomb) {
        double d = naval::norm(e.pos() - s.target);
        if (d <= 2000) {
          int band = std::max(0, int(std::ceil(d / 400)) - 1);
          hits = (s.n * (5 - band) + 4) / 5;
        }
      } else if (naval::contains(bodies[e.index], s.target, 0))
        hits = 1;
      if (hits) {
        if (s.kind == Shell && s.team != e.team) {
          enemyHit = true;
          if (s.slow)
            e.u.lockedUntil = std::max(e.u.lockedUntil, t + 11);
        }
        damage[e.index][s.team] += s.damage * hits;
        ++hitCount;
        events.push_back({Impact, t, e.index, s.kind, s.damage * hits, s.id});
      }
    }
    if (enemyHit && s.source >= 0 &&
        entities[s.source].u.activeUntil[RapidReload] > t)
      ++reloadHits[s.source];
    return true;
  });
  for (auto &e : entities)
    if (int reduction = reloadHits[e.index] * 5) {
      for (int &at : e.u.mainAt)
        at = std::max(t, at - reduction);
      for (int &at : e.u.secondaryAt)
        at = std::max(t, at - reduction);
      events.push_back({ReloadAdvanced, t, e.index, 0, double(reduction), 0});
    }
}
} // namespace game
