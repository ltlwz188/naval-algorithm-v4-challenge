#include "naval.hpp"
#include <cmath>

// 示例让驱逐舰按本局海图向 B 点航行，进入占领圈后减速停车。
int decide(const game::View &v, game::Action *out) {
    const auto &zone = v.chart.zones[1];
    int n = 0;
    for (unsigned i = 0; i < v.h.own; ++i) {
        const auto &ship = v.own[i];
        if (ship.kind != game::Destroyer || ship.hp <= 0)
            continue;
        const double dx = zone.x - ship.x;
        const double dy = zone.y - ship.y;
        const double heading = std::atan2(dx, dy) * 180.0 / 3.141592653589793;
        const double speed = std::hypot(dx, dy) < zone.radius - 1000 ? 0.0 : 55.0;
        out[n++] = {ship.id, game::Move, 0, 0, std::fmod(heading + 360., 360.), speed};
    }
    return n;
}
NAVAL_PLAYER(decide)
