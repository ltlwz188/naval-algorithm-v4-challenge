#include "world.hpp"
#include <numeric>
namespace game {
namespace {
constexpr int kinds[] = {
    Carrier,   Carrier,   Battleship,   Battleship, Battleship,   Cruiser,
    Cruiser,   Cruiser,   Destroyer,    Destroyer,  Destroyer,    Destroyer,
    Submarine, Submarine, TorpedoPlane, Bomber,     TorpedoPlane, Bomber};
} // namespace
uint64_t World::newId() {
  uint64_t z = (++sequence) * 0x9e3779b97f4a7c15ULL + 0x97b567379442c7bbULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
World::World(int seed, const Chart &map) : seed(seed), chart(map) {
  for (int team = 0; team < 2; team++)
    for (int i = 0; i < 18; i++) {
      auto &e = entities[team * 18 + i];
      auto &u = e.u;
      int slot = i < 14 ? i : (i - 14) / 2, sign = team ? -1 : 1;
      e.team = team;
      e.index = team * 18 + i;
      e.contact = newId();
      u.id = int(newId() & 0x7fffffff);
      u.kind = kinds[i];
      u.x = chart.spawnX + sign * chart.dx[slot];
      u.y = chart.spawnY[team] + sign * chart.dy[slot];
      u.heading = chart.heading[team];
      u.hp = stats[u.kind].hp;
      u.mother = -1;
      if (u.kind == Submarine)
        u.oxygen = 500;
      if (e.air()) {
        u.mode = Deck;
        u.mother = entities[team * 18 + slot].u.id;
        u.hp *= 8;
        for (int j = 0; j < 8; j++) {
          u.memberHP[j] = stats[u.kind].hp;
          u.ammo[j] = 2;
        }
      }
    }
  for (auto &p : points)
    p = {-1, -1, 0, 0};
  prepare();
}
void World::aircraft() {
  for (auto &e : entities) {
    auto &u = e.u;
    if (!e.air())
      continue;
    auto &mother = entities[e.team * 18 + (e.index % 18 - 14) / 2].u;
    if (mother.hp <= 0) {
      u.hp = 0;
      u.mode = Deck;
      continue;
    }
    auto dock = [&] {
      u.mode = Deck;
      u.readyAt = t + 50;
      for (int j = 0; j < 8; j++)
        if (u.memberHP[j] <= 0 && !u.repairAt[j])
          u.repairAt[j] = t + 150;
    };
    for (auto &hp : u.memberHP)
      hp = std::max(0., hp);
    u.hp = std::accumulate(u.memberHP, u.memberHP + 8, 0.);
    if (u.mode != Deck && u.hp == 0)
      dock();
    if (u.mode == Recovering && t >= e.recoveryEnd)
      dock();
    if (u.mode == Deck) {
      u.x = mother.x;
      u.y = mother.y;
      u.speed = 0;
      u.heading = mother.heading;
      for (int j = 0; j < 8; j++) {
        if (u.repairAt[j] && t >= u.repairAt[j]) {
          u.memberHP[j] = stats[u.kind].hp;
          u.ammo[j] = 2;
          u.repairAt[j] = 0;
        }
        if (t >= u.readyAt && u.memberHP[j] > 0) {
          u.memberHP[j] = stats[u.kind].hp;
          u.ammo[j] = 2;
        }
      }
      u.hp = std::accumulate(u.memberHP, u.memberHP + 8, 0.);
    }
  }
}
void World::prepare() {
  aircraft();
  startSkills();
  for (auto &e : entities) {
    auto &u = e.u;
    if (u.kind == Submarine && u.transitionUntil && t >= u.transitionUntil) {
      u.mode = u.mode == Diving ? Submerged : Surface;
      u.transitionUntil = 0;
    }
    if (u.lockedUntil > t)
      u.speed = 0;
    e.activeAtStart = e.onMap();
    e.modeAtStart = u.mode;
    bodies[e.index] = {{{e.pos(), u.heading, u.speed}},
                       stats[u.kind].len,
                       stats[u.kind].width};
    if (!e.air() && u.speed > stats[u.kind].speed && u.activeUntil[Boost] <= t)
      bodies[e.index].motion =
          naval::maneuver({e.pos(), u.heading, u.speed}, u.heading,
                          stats[u.kind].speed, stats[u.kind].acc, 0);
  }
  observe();
}
void World::observe() {
  for (int side = 0; side < 2; side++) {
    auto &frame = sample[side];
    frame.clear();
    auto visible = [&](Vec p, auto range) {
      if (inSmoke(p))
        return false;
      for (int i = 0; i < 18; i++) {
        const auto &o = entities[side * 18 + i];
        if (o.onMap() && inside(o.pos(), p, range(stats[o.u.kind])))
          return true;
      }
      return false;
    };
    for (const auto &e : entities) {
      if (e.team == side || !e.onMap() || e.u.activeUntil[Silent] > t)
        continue;
      const auto &u = e.u;
      if (visible(e.pos(), [&](const Stats &s) {
            return e.air() ? s.air : u.mode == Submerged ? s.sub : s.see;
          }))
        frame.contacts.push_back(
            {e.contact, u.kind, t, u.x, u.y, u.heading, u.speed, u.hp,
             int(std::count_if(u.memberHP, u.memberHP + 8,
                               [](double hp) { return hp > 0; })),
             0});
    }
    std::sort(frame.contacts.begin(), frame.contacts.end(),
              [](auto &a, auto &b) { return a.id < b.id; });
    for (auto &s : shots) {
      if (s.team == side || (s.kind == AirTorpedo && t - s.born < 2))
        continue;
      bool torp = s.kind == AirTorpedo || s.kind == ShipTorpedo;
      double radius = torp ? 4000 : s.kind == Shell ? 8000 : 2000;
      if (visible(s.p, [&](const Stats &) { return radius; }))
        frame.incoming.push_back({s.id, torp ? ShipTorpedo : s.kind, t,
                                  double(s.p.x), double(s.p.y),
                                  torp ? s.heading : 0, torp ? s.speed : 0});
    }
    std::sort(frame.incoming.begin(), frame.incoming.end(),
              [](auto &a, auto &b) { return a.id < b.id; });
  }
}
void World::encode(int side, Bytes &b) const {
  const auto &f = delayed[side];
  Header h{};
  h.version = abi;
  h.tick = t;
  h.side = side;
  h.seed = seed;
  std::copy(score, score + 2, h.score);
  std::copy(points.begin(), points.end(), h.points);
  for (auto &e : entities)
    if (e.team == side &&
        (e.u.hp > 0 ||
         (e.air() && entities[side * 18 + (e.index % 18 - 14) / 2].u.hp > 0)))
      h.own++;
  h.contacts = f.contacts.size();
  h.incoming = f.incoming.size();
  h.flashes = f.flashes.size();
  h.feedback = feedback[side].size();
  h.smokes = smokes.size();
  h.destroyed = f.losses.size();
  b.clear();
  append(b, &h);
  for (auto &e : entities)
    if (e.team == side &&
        (e.u.hp > 0 ||
         (e.air() && entities[side * 18 + (e.index % 18 - 14) / 2].u.hp > 0)))
      append(b, &e.u);
  append(b, f.contacts.data(), h.contacts);
  append(b, f.incoming.data(), h.incoming);
  append(b, f.flashes.data(), h.flashes);
  append(b, feedback[side].data(), h.feedback);
  append(b, f.losses.data(), h.destroyed);
  append(b, smokes.data(), smokes.size());
}
void World::truth(Bytes &b) const {
  b.clear();
  for (auto &e : entities) {
    append(b, &e.u);
    append(b, &e.contact);
  }
  for (auto &s : shots) {
    Incoming item{s.id,          s.kind,    s.born, double(s.p.x),
                  double(s.p.y), s.heading, s.speed};
    append(b, &item);
  }
}
void World::witnessLoss(const Entity &e) {
  const auto &seen = sample[1 - e.team].contacts;
  if (std::any_of(seen.begin(), seen.end(),
                  [&](auto &c) { return c.id == e.contact; }))
    sample[1 - e.team].losses.push_back({e.contact, t, 0});
}
void World::finishDamage() {
  for (auto &e : entities) {
    if (e.air() || e.u.hp <= 0)
      continue;
    auto &d = damage[e.index];
    double total = d[0] + d[1] + shoreDamage[e.index];
    if (!total)
      continue;
    double absorbed = std::min(e.u.shield, total);
    if (absorbed) {
      e.u.shield -= absorbed;
      events.push_back({ShieldAbsorbed, t, e.index, 0, absorbed, 0});
    }
    e.u.hp = std::max(0., e.u.hp - (total - absorbed));
    if (e.u.hp == 0) {
      events.push_back({ShipLost, t, e.index, 0, total, e.contact});
      witnessLoss(e);
      int loss = d[e.team] > d[1 - e.team] + shoreDamage[e.index] ? 100 : 50;
      score[e.team] -= loss;
      score[1 - e.team] += loss;
      events.push_back(
          {ScoreAward, t, e.team, loss == 100 ? 3 : 2, -double(loss), 0});
      events.push_back(
          {ScoreAward, t, 1 - e.team, loss == 100 ? 3 : 2, double(loss), 0});
      e.burstDue = -1;
      if (e.u.kind == Carrier)
        for (auto &wing : entities)
          if (wing.air() && wing.u.mother == e.u.id) {
            if (wing.onMap() &&
                std::any_of(wing.u.memberHP, wing.u.memberHP + 8,
                            [](auto hp) { return hp > 0; }))
              witnessLoss(wing);
            for (double &hp : wing.u.memberHP)
              hp = 0;
            wing.u.hp = 0;
            wing.u.mode = Deck;
          }
    }
  }
  damage = {};
  shoreDamage = {};
}
void World::scoring() {
  for (int i = 0; i < 3; i++) {
    auto &p = points[i];
    bool present[2]{};
    const auto &zone = chart.zones[i];
    Vec center{zone.x, zone.y};
    for (auto &e : entities)
      if (!e.air() && e.onMap() && inside(e.pos(), center, zone.radius))
        present[e.team] = true;
    p.contested = present[0] && present[1];
    if (p.attacker >= 0 && !present[p.attacker]) {
      p.attacker = -1;
      p.progress = 0;
    }
    if (present[0] != present[1]) {
      int side = present[1] ? 1 : 0;
      if (side != p.owner && t > 0) {
        p.attacker = side;
        if (++p.progress == 300) {
          p.owner = side;
          p.attacker = -1;
          p.progress = 0;
          claimedAt[i] = t;
          events.push_back({Ownership, t, i, side, 0, 0});
        }
      }
    }
    if (p.owner >= 0 && !present[1 - p.owner] && t > claimedAt[i] &&
        (t - claimedAt[i]) % 20 == 0) {
      score[p.owner] += 20;
      events.push_back({ScoreAward, t, p.owner, 1, 20, 0});
    }
  }
  int live[2]{};
  for (auto &e : entities)
    if (!e.air() && e.onMap())
      live[e.team]++;
  for (int i = 0; i < 2; i++) {
    if (t && t % 20 == 0) {
      score[i] += 10;
      events.push_back({ScoreAward, t, i, 0, 10, 0});
    }
    if (score[i] < 0) {
      events.push_back({ScoreAward, t, i, 4, double(-score[i]), 0});
      score[i] = 0;
    }
  }
  if (!live[0] || !live[1]) {
    winner = live[0] ? 0 : live[1] ? 1 : -1;
    reason = "fleet";
  } else if (score[0] >= 3500 || score[1] >= 3500) {
    winner = score[1] < 3500 ? 0 : score[0] < 3500 ? 1 : -1;
    reason = "points";
  } else if (t == 4500) {
    winner = score[0] > score[1] ? 0 : score[0] < score[1] ? 1 : -1;
    reason = "time";
  }
}
void World::step(const std::vector<Action> (&commands)[2]) {
  events.clear();
  for (int side = 0; side < 2; side++) {
    auto &old = delayed[side].contacts;
    auto &now = sample[side].contacts;
    auto absent = [](auto &list, uint64_t id) {
      return std::none_of(list.begin(), list.end(),
                          [&](auto &c) { return c.id == id; });
    };
    for (auto &c : now)
      if (absent(old, c.id))
        events.push_back({Spotted, t, side, c.kind, 0, c.id});
    for (auto &c : old)
      if (absent(now, c.id))
        events.push_back({LostContact, t, side, c.kind, 0, c.id});
  }
  int priorScore[2]{score[0], score[1]};
  flashed = {};
  torpedoFired = {};
  selectBursts();
  for (int side = 0; side < 2; side++) {
    feedback[side].resize(commands[side].size());
    for (bool skillsFirst : {true, false})
      for (size_t i = 0; i < commands[side].size(); ++i) {
        const auto &a = commands[side][i];
        if ((a.verb == UseSkill) != skillsFirst)
          continue;
        bool ok = act(side, a);
        feedback[side][i] = {int(i), ok};
        actionsAccepted += ok;
      }
  }
  flak();
  commitTorpedoes();
  resolveShots();
  finishDamage();
  scoring();
  for (int i = 0; i < 2; i++)
    if (score[i] != priorScore[i])
      events.push_back(
          {ScoreChange, t, i, score[i], double(score[i] - priorScore[i]), 0});
  for (int side = 0; side < 2; side++)
    std::swap(delayed[side], sample[side]);
  if (winner != -2)
    return;
  move();
  ++t;
  prepare();
}
} // namespace game
