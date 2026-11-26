#pragma once
#include "naval.hpp"
#include "naval/geometry.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <vector>
namespace game {
using naval::Body;
using naval::Pose;
using naval::Vec;
using Bytes = std::vector<unsigned char>;
template <class T> void append(Bytes &b, const T *p, size_t n = 1) {
  if (n) {
    auto s = reinterpret_cast<const unsigned char *>(p);
    b.insert(b.end(), s, s + n * sizeof(T));
  }
}
inline double dist2(Vec a, Vec b) {
  auto d = a - b;
  return naval::dot(d, d);
}
inline bool inside(Vec a, Vec b, double r) { return dist2(a, b) <= r * r; }
inline Vec direction(double deg) {
  double r = deg * naval::pi / 180;
  return {sin(r), cos(r)};
}
struct Stats {
  double hp, len, width, speed, acc, turn, see, sub, air;
  int aa, aaRange, aaPoint, burstRange, burst;
};
inline constexpr Stats stats[] = {
    {80000, 360, 70, 90, 1.2, 4, 14000, 10000, 14000, 2, 10000, 600, 6000,
     1500},
    {90000, 300, 55, 80, 1.2, 4, 12000, 8000, 12000, 2, 10000, 400, 6000, 1000},
    {60000, 200, 32, 96, 2, 6, 16000, 10000, 16000, 4, 14000, 1000, 8000, 2500},
    {30000, 130, 20, 120, 2.4, 8, 22000, 16000, 22000, 0, 0, 0, 6000, 1000},
    {25000, 90, 12, 110, 1.6, 5, 14000, 14000, 14000, 0, 0, 0, 0, 0},
    {20000, 0, 0, 400, 15, 8, 15000, 8000, 15000, 0, 0, 0, 0, 0},
    {22000, 0, 0, 420, 15, 8, 15000, 8000, 15000, 0, 0, 0, 0, 0}};
struct Entity {
  Unit u{};
  uint64_t contact{};
  bool activeAtStart{};
  int modeAtStart{}, team{}, index{}, recoveryEnd{}, burstTarget = -1,
                                                     burstDue = -1;
  bool onMap() const {
    return u.hp > 0 &&
           (u.kind < TorpedoPlane || u.mode == Flying || u.mode == Recovering);
  }
  bool air() const { return u.kind >= TorpedoPlane; }
  Vec pos() const { return {u.x, u.y}; }
};
struct Shot {
  uint64_t id{};
  int team{}, kind{}, born{}, due{}, n = 1;
  Vec origin{}, target{}, p{};
  double heading{}, speed{}, damage{}, limit{}, arm{};
  int source = -1;
  bool slow = false;
  int blockingIsland = -1;
  double shoreFraction = 0; // fraction of the full shell path at first shore contact
};
enum EventKind {
  ShotReleased,
  Impact,
  PlaneDamage,
  ShipLost,
  Ownership,
  ScoreChange,
  FlakPoint,
  Spotted,
  LostContact,
  ScoreAward,
  SkillUsed,
  ShoreImpact,
  ShieldAbsorbed,
  ReloadAdvanced,
  TorpedoCleared,
  ShellBlocked,
  AirCrash
};
struct Event {
  int kind, tick, unit, detail;
  double value;
  uint64_t object;
  double x = 0, y = 0; // shoreline contact position for ShoreImpact
};
static_assert(sizeof(Event) == 48);
struct Volley {
  int due, team, kind;
  Vec aim;
};
struct Sample {
  std::vector<Contact> contacts;
  std::vector<Incoming> incoming;
  std::vector<Flash> flashes;
  std::vector<Loss> losses;
  void clear() {
    contacts.clear();
    incoming.clear();
    flashes.clear();
    losses.clear();
  }
};
struct World {
  int t = 0, seed = 0, score[2]{900, 900}, winner = -2;
  std::string reason;
  uint64_t sequence = 0;
  std::array<Entity, 36> entities{};
  std::array<Body, 36> bodies{};
  std::array<Point, 3> points{};
  int claimedAt[3]{};
  std::vector<Shot> shots;
  std::vector<Volley> volleys;
  std::vector<Event> events;
  Sample delayed[2], sample[2];
  std::vector<Feedback> feedback[2];
  std::array<std::array<double, 2>, 36> damage{};
  std::array<bool, 36> flashed{}, torpedoFired{};
  unsigned shotsFired = 0, hitCount = 0, aaHits = 0, actionsAccepted = 0;
  Chart chart;
  struct Scheduled {
    int source, skill, target, due;
    Vec origin;
  };
  std::vector<Scheduled> scheduled;
  std::vector<Smoke> smokes;
  struct TorpedoHit {
    Shot shot;
    int victim;
    Vec contact;
  };
  std::vector<TorpedoHit> pendingHits;
  std::array<double, 36> shoreDamage{};
  explicit World(int seed, const Chart &map = Chart{});
  bool useSkill(Entity &e, const Action &a);
  void startSkills();
  void commitTorpedoes();
  bool inSmoke(Vec pos) const;

  void prepare();
  void observe();
  void encode(int side, Bytes &b) const;
  void truth(Bytes &b) const;
  void step(const std::vector<Action> (&commands)[2]);
  bool act(int side, const Action &a);
  void aircraft();
  void flak();
  void selectBursts();
  std::array<double, 36> dueBursts{};
  void resolveShots();
  void move();
  void moveShots(double elapsed, double duration,
                 const std::array<double, 36> &extent, naval::Solver &solver);
  void finishDamage();
  void witnessLoss(const Entity &e);
  void scoring();
  uint64_t newId();
};
} // namespace game
