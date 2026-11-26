#pragma once
#include <cstddef>
#include <cstdint>

namespace game {
inline constexpr unsigned abi = 6;
enum Kind {
  Carrier,
  Battleship,
  Cruiser,
  Destroyer,
  Submarine,
  TorpedoPlane,
  Bomber
};
enum Mode { Surface, Diving, Submerged, Surfacing, Deck, Flying, Recovering };
enum Verb {
  Move,
  MainGun,
  SecondaryGun,
  Torpedo,
  AAGun,
  Launch,
  Recover,
  Drop,
  Dive,
  SurfaceSub,
  UseSkill
};
enum Object { Shell, ShipTorpedo, AirTorpedo, Bomb };
enum Skill {
  Boost,
  SmokeScreen,
  FastTorpedoes,
  Disrupt,
  SlowShells,
  Maintenance,
  RapidReload,
  AirRepair,
  GrantShield,
  Silent,
  Oxygen,
  SkillCount
};
struct Circle {
  double x, y, radius;
};
// A circle has coincident backbone endpoints. Coordinates are in L.
enum IslandHeight : unsigned { LowIsland, MediumIsland, HighIsland };
struct Island {
  double ax, ay, bx, by, radius;
  IslandHeight height = LowIsland;
};
inline constexpr int maxIslands = 256;
struct Chart {
  Circle zones[3]{
      {25000, 50000, 7000}, {50000, 50000, 10000}, {75000, 50000, 7000}};
  unsigned islandCount{}, reserved{};
  Island islands[maxIslands]{};
  static constexpr double size = 100000;
  static constexpr double spawnX = 50000, spawnY[2]{15000, 85000},
                          heading[2]{0, 180};
  static constexpr int dx[14]{-3000, 3000,  -3000, 0,    3000, -4000, 0,
                              4000,  -6000, -2000, 2000, 6000, -9000, 9000};
  static constexpr int dy[14]{-8000, -8000, -3000, -3000, -3000, 1000, 1000,
                              1000,  6000,  6000,  6000,  6000,  0,    0};
};
inline Chart chartStorage;
struct Smoke {
  double x, y;
  int from, until;
}; // inclusive, radius 3000
struct Point {
  int owner, attacker, progress, contested;
};
struct Header {
  unsigned version, tick, side, seed;
  int score[2];
  Point points[3];
  unsigned own, contacts, incoming, flashes, feedback, destroyed, smokes,
      reserved;
};
struct Unit {
  int id, kind, mode, mother;
  double x, y, heading, speed, hp;
  int oxygen, transitionUntil, readyAt, nextLaunch;
  double memberHP[8];
  int ammo[8], repairAt[8];
  int mainAt[12], secondaryAt[20], torpedoAt[8], aaAt[4], burstAt;
  int skillReady[SkillCount], activeUntil[SkillCount]; // exclusive end
  unsigned torpedoMask, slowMask;
  int lockedUntil, shieldUntil; // exclusive
  double shield;
};
struct Contact {
  uint64_t id;
  int kind, observedAt;
  double x, y, heading, speed, hp;
  int count, reserved;
};
struct Incoming {
  uint64_t id;
  int kind, observedAt;
  double x, y, heading, speed;
};
struct Flash {
  double x, y;
  int observedAt, reserved;
};
struct Loss {
  uint64_t id;
  int observedAt, reserved;
};
struct Feedback {
  int action, accepted;
};
struct Action {
  int unit, verb, slot, count;
  double x, y;
  double spacing = 0;
};

// Wire order follows this view. IDs in actions refer to your own Unit.id.
// MOVE x=heading,y=speed; guns x/y=point; TORPEDO x=heading,y=fan half-angle.
// Main/secondary gun slot=-1 fires available slots up to count (0=all).
// AA slot is a single mount. LAUNCH acts on the squadron; x=initial speed.
// DROP x/y=point; torpedo spacing (0..25 L) uses spacing. Deadlines are
// absolute T.
struct View {
  const Header &h;
  const Unit *own;
  const Contact *contacts;
  const Incoming *incoming;
  const Flash *flashes;
  const Feedback *feedback;
  const Loss *destroyed;
  const Smoke *smokes;
  const Chart &chart;
  explicit View(const void *p, const Chart &map = chartStorage)
      : h(*static_cast<const Header *>(p)),
        own(reinterpret_cast<const Unit *>(&h + 1)),
        contacts(reinterpret_cast<const Contact *>(own + h.own)),
        incoming(reinterpret_cast<const Incoming *>(contacts + h.contacts)),
        flashes(reinterpret_cast<const Flash *>(incoming + h.incoming)),
        feedback(reinterpret_cast<const Feedback *>(flashes + h.flashes)),
        destroyed(reinterpret_cast<const Loss *>(feedback + h.feedback)),
        smokes(reinterpret_cast<const Smoke *>(destroyed + h.destroyed)),
        chart(map) {}
};
inline constexpr int maxActions = 512;
#ifdef __wasm__
alignas(8) inline unsigned char input[2 * 1024 * 1024];
inline Action output[maxActions];
extern "C" {
__attribute__((export_name("chart"))) inline Chart *chart_ptr() {
  return &chartStorage;
}
__attribute__((export_name("naval_abi"))) inline int version() { return abi; }
__attribute__((export_name("input"))) inline unsigned char *input_ptr() {
  return input;
}
__attribute__((export_name("output"))) inline Action *output_ptr() {
  return output;
}
}
#define NAVAL_PLAYER(decide)                                                   \
  extern "C" __attribute__((export_name("step"))) int step() {                 \
    return decide(game::View(game::input), game::output);                      \
  }
#endif
static_assert(sizeof(Island) == 48 && offsetof(Island,height) == 40 && sizeof(Unit) == 496 && sizeof(Header) == 104 &&
              offsetof(Chart, islands) == 80 && sizeof(Contact) == 64 &&
              sizeof(Action) == 40);
} // namespace game
