#include "world.hpp"
#include <numeric>

namespace game {
namespace {
struct SkillRule {
  int kind, cooldown, duration;
};
constexpr SkillRule rules[SkillCount] = {
    {Destroyer, 300, 50},  {Destroyer, 600, 90}, {Destroyer, 300, 0},
    {Cruiser, 500, 0},     {Cruiser, 500, 0},    {Battleship, 800, 20},
    {Battleship, 800, 30}, {Carrier, 300, 10},   {Carrier, 500, 20},
    {Submarine, 500, 50},  {Submarine, 500, 0}};
} // namespace
bool World::useSkill(Entity &e, const Action &a) {
  int skill = a.slot;
  if (skill < 0 || skill >= SkillCount || rules[skill].kind != e.u.kind ||
      e.u.skillReady[skill] > t)
    return false;
  int target = e.index;
  if (skill == GrantShield) {
    auto it =
        std::find_if(entities.begin(), entities.end(), [&](const Entity &ally) {
          return ally.team == e.team && !ally.air() && ally.onMap() &&
                 ally.u.id == a.count;
        });
    if (it == entities.end())
      return false;
    target = it->index;
  }
  e.u.skillReady[skill] = t + rules[skill].cooldown;
  if (skill == FastTorpedoes)
    e.u.torpedoMask = 7;
  else if (skill == SlowShells)
    e.u.slowMask = (1u << 10) - 1;
  else
    scheduled.push_back({e.index, skill, target, t + 1, e.pos()});
  events.push_back({SkillUsed, t, e.index, skill, double(target), 0});
  return true;
}
bool World::inSmoke(Vec position) const {
  return std::any_of(smokes.begin(), smokes.end(), [&](const Smoke &s) {
    return inside(position, {s.x, s.y}, 3000);
  });
}
void World::startSkills() {
  std::erase_if(smokes, [&](const Smoke &s) { return t > s.until; });
  for (auto &e : entities)
    if (e.u.shieldUntil <= t)
      e.u.shield = 0;
  for (const auto &activation : scheduled) {
    if (activation.due != t)
      continue;
    auto &e = entities[activation.source];
    auto &target = entities[activation.target];
    int skill = activation.skill;
    int end = t + rules[skill].duration;
    e.u.activeUntil[skill] = end;
    switch (skill) {
    case SmokeScreen:
      smokes.push_back({double(activation.origin.x),
                        double(activation.origin.y), t, end - 1});
      break;
    case Disrupt: {
      auto cleared = [&](const Shot &s, Vec p) {
        if (s.team == e.team || !inside(e.pos(), p, 8000))
          return false;
        if (s.kind != ShipTorpedo && !(s.kind == AirTorpedo && t - s.born >= 2))
          return false;
        events.push_back({TorpedoCleared, t, e.index, s.kind, 0, s.id});
        return true;
      };
      std::erase_if(shots, [&](const Shot &s) { return cleared(s, s.p); });
      std::erase_if(pendingHits, [&](const TorpedoHit &h) {
        return cleared(h.shot, h.contact);
      });
      break;
    }
    case GrantShield:
      if (target.u.hp > 0) {
        target.u.shield = stats[target.u.kind].hp;
        target.u.shieldUntil = end;
      }
      break;
    case Oxygen:
      if (e.u.hp > 0)
        e.u.oxygen = std::min(500, e.u.oxygen + 100);
      break;
    }
  }
  scheduled.clear(); // every activation is scheduled for the immediately
                     // following tick
  for (auto &e : entities) {
    if (!e.onMap())
      continue;
    auto &u = e.u;
    if (u.activeUntil[Maintenance] > t)
      u.hp = std::min(stats[u.kind].hp, u.hp + 2500);
    if (e.air()) {
      const auto &mother = entities[e.team * 18 + (e.index % 18 - 14) / 2];
      if (mother.u.activeUntil[AirRepair] > t) {
        for (double &hp : u.memberHP)
          if (hp > 0)
            hp = std::min(stats[u.kind].hp, hp + 1000);
        u.hp = std::accumulate(u.memberHP, u.memberHP + 8, 0.);
      }
    }
    if (u.kind == Submarine && t > 0) {
      if (u.mode == Surface)
        u.oxygen = std::min(500, u.oxygen + 2);
      if (u.mode == Submerged && --u.oxygen <= 0) {
        u.oxygen = 0;
        u.mode = Surfacing;
        u.transitionUntil = t + 25;
      }
    }
  }
}
void World::commitTorpedoes() {
  for (const auto &hit : pendingHits) {
    const auto &s = hit.shot;
    damage[hit.victim][s.team] += s.damage;
    ++hitCount;
    events.push_back({Impact, t, hit.victim, s.kind, s.damage, s.id});
  }
  pendingHits.clear();
}
} // namespace game
