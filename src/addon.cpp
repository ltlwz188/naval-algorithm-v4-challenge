#include "world.hpp"
#include <cstring>
#include <memory>
#include <node_api.h>
napi_value delta(napi_env, napi_callback_info);
using namespace game;
namespace {
void check(napi_status s) {
  if (s != napi_ok)
    throw std::runtime_error("invalid native argument");
}
struct Args {
  napi_env env;
  napi_value a[4]{};
  size_t n = 4;
  Args(napi_env env, napi_callback_info info) : env(env) {
    check(napi_get_cb_info(env, info, &n, a, nullptr, nullptr));
  }
  int number(int i) {
    int32_t value;
    check(napi_get_value_int32(env, a[i], &value));
    return value;
  }
  World &world() {
    void *p;
    check(napi_get_value_external(env, a[0], &p));
    return *static_cast<World *>(p);
  }
};
napi_value integer(napi_env env, int i) {
  napi_value v;
  check(napi_create_int32(env, i, &v));
  return v;
}
napi_value create(napi_env env, Args &a) {
  Chart map;
  if (a.n > 1) {
    void *data;
    size_t length;
    check(napi_get_buffer_info(env, a.a[1], &data, &length));
    if (length < offsetof(Chart, islands) || length > sizeof(Chart))
      throw std::runtime_error("invalid chart");
    std::memcpy(&map, data, length);
    if (map.islandCount > maxIslands ||
        length != offsetof(Chart, islands) + map.islandCount * sizeof(Island))
      throw std::runtime_error("invalid island array");
    for (unsigned i=0; i<map.islandCount; ++i)
      if (map.islands[i].height > HighIsland)
        throw std::runtime_error("invalid island height");
  }
  auto world = std::make_unique<World>(a.number(0), map);
  napi_value v;
  check(napi_create_external(
      env, world.get(),
      [](napi_env, void *p, void *) { delete static_cast<World *>(p); },
      nullptr, &v));
  world.release();
  return v;
}
napi_value frame(napi_env env, Args &a) {
  Bytes bytes;
  int side = a.number(1);
  if (side == 3) {
    auto &e = a.world().events;
    append(bytes, e.data(), e.size());
  } else if (side == 2)
    a.world().truth(bytes);
  else if (side == 0 || side == 1)
    a.world().encode(side, bytes);
  else
    throw std::runtime_error("side");
  napi_value v;
  check(napi_create_buffer_copy(env, bytes.size(), bytes.data(), nullptr, &v));
  return v;
}
napi_value advance(napi_env env, Args &a) {
  std::vector<Action> commands[2];
  World &w = a.world();
  if (w.winner != -2)
    throw std::runtime_error("match finished");
  for (int i = 0; i < 2; i++) {
    void *p;
    size_t n;
    check(napi_get_buffer_info(env, a.a[i + 1], &p, &n));
    if (n % sizeof(Action) || n / sizeof(Action) > maxActions)
      throw std::runtime_error("action buffer size");
    commands[i].resize(n / sizeof(Action));
    if (n)
      std::memcpy(commands[i].data(), p, n);
  }
  w.step(commands);
  return integer(env, w.winner);
}
napi_value status(napi_env env, Args &a) {
  auto &w = a.world();
  napi_value v;
  check(napi_create_object(env, &v));
  for (auto [key, num] : std::initializer_list<std::pair<const char *, int>>{
           {"tick", w.t},
           {"winner", w.winner},
           {"blue", w.score[0]},
           {"red", w.score[1]},
           {"objects", int(w.shots.size())},
           {"fired", int(w.shotsFired)},
           {"hits", int(w.hitCount)},
           {"aaHits", int(w.aaHits)},
           {"accepted", int(w.actionsAccepted)},
           {"unitSize", sizeof(Unit)}})
    check(napi_set_named_property(env, v, key, integer(env, num)));
  napi_value reason;
  check(napi_create_string_utf8(env, w.reason.c_str(), NAPI_AUTO_LENGTH,
                                &reason));
  check(napi_set_named_property(env, v, "reason", reason));
  return v;
}
template <napi_value (*f)(napi_env, Args &)>
napi_value call(napi_env env, napi_callback_info info) {
  try {
    Args a(env, info);
    return f(env, a);
  } catch (const std::exception &e) {
    napi_throw_error(env, nullptr, e.what());
    return nullptr;
  }
}
napi_value init(napi_env env, napi_value exports) {
  for (auto [name, fn] :
       std::initializer_list<std::pair<const char *, napi_callback>>{
           {"delta", delta},
           {"create", call<create>},
           {"frame", call<frame>},
           {"advance", call<advance>},
           {"status", call<status>}}) {
    napi_value v;
    check(napi_create_function(env, name, NAPI_AUTO_LENGTH, fn, nullptr, &v));
    check(napi_set_named_property(env, exports, name, v));
  }
  return exports;
}
} // namespace
NAPI_MODULE(naval_v4, init)
