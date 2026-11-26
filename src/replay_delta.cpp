#include <cstdint>
#include <cstring>
#include <node_api.h>

// Return changed 32-byte blocks with uint16 indices. Fall back to a full
// frame if it would be smaller. Buffer size here is limited to the SDK range.
napi_value delta(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  void *cur = nullptr, *prev = nullptr;
  size_t n = 0, oldSize = 0;
  if (count != 2 || napi_get_buffer_info(env, args[0], &cur, &n) != napi_ok ||
      napi_get_buffer_info(env, args[1], &prev, &oldSize) != napi_ok ||
      n != oldSize || n > 2 * 1024 * 1024) {
    napi_throw_error(env, nullptr, "invalid delta buffers");
    return nullptr;
  }
  auto changed = [&](auto emit) {
    size_t used = 0;
    for (size_t pos = 0; pos < n; pos += 32) {
      size_t len = n - pos < 32 ? n - pos : 32;
      auto *block = static_cast<const uint8_t *>(cur) + pos;
      if (std::memcmp(block, static_cast<const uint8_t *>(prev) + pos, len)) {
        emit(used, static_cast<uint16_t>(pos / 32), block, len);
        used += 2 + len;
      }
    }
    return used;
  };
  const auto size = changed([](auto...) {});
  if (size >= n)
    return args[0];
  void *out;
  napi_value result;
  napi_create_buffer(env, size, &out, &result);
  if (size)
    changed([&](size_t i, uint16_t index, const uint8_t *block, size_t len) {
      auto *p = static_cast<uint8_t *>(out) + i;
      std::memcpy(p, &index, 2);
      std::memcpy(p + 2, block, len);
    });
  return result;
}
