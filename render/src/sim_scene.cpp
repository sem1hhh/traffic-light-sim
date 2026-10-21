// render/src/sim_scene.cpp — SimScene implementasyonu.
//
// Burada hicbir is mantigi YENIDEN YAZILMAZ: World adimlandirir, SignalDriver
// karar verir, sabit zamanli kontrolcu faz degistirir — hepsi sim-core ve
// controllers. Bu dosya yalnizca onlari KURAR ve AKUMULATORLE ilerletir.

#include "sim_scene.h"

#include <algorithm>
#include <cmath>

namespace tsc {
namespace render {

Grid buildRenderGrid() {
    // experiments/bench.cpp buildScenarioGrid ile AYNI. 9x7 ızgara, ortada
    // yatay omurga, uc noktada dikey kol -> uc T kavsagi (sinyalli).
    Grid g{GridSize{9, 7}};
    for (int x = 0; x < 9; ++x) {
        g.set(Vec2i{x, 2}, CellType::Road);
    }
    const int kol[3] = {2, 4, 6};
    for (const int kx : kol) {
        g.set(Vec2i{kx, 1}, CellType::Road);
        g.set(Vec2i{kx, 3}, CellType::Road);
        g.set(Vec2i{kx, 2}, CellType::Junction);
    }
    return g;
}

SimScene::SimScene(std::uint64_t seed, int stepsPerSecond)
    : net_(buildNetwork(buildRenderGrid())),
      world_(net_, WorldConfig{}, seed),
      driver_(world_, [](JunctionId) -> std::unique_ptr<IController> {
          // Simdilik sabit zamanli kontrolcu. Q-learning sonraki adimda
          // politika dosyasindan yuklenebilir.
          return std::make_unique<FixedTimeController>();
      }) {
    setStepsPerSecond(stepsPerSecond);
}

void SimScene::setStepsPerSecond(int n) {
    // 0 veya negatif "dur" demektir; 1..kMaxStepsPerFrame araligina sikistir.
    stepsPerSecond_ = std::clamp(n, 0, kMaxStepsPerFrame);
}

void SimScene::advance(double dtSeconds) {
    if (stopped() || stepsPerSecond_ <= 0) {
        return;
    }
    // GERCEK gecen sureyi "simulasyon-saniye" olarak biriktir.
    // 1 saniye gercek sure * stepsPerSecond_ = kac sim-sn adim.
    accumulator_ += dtSeconds * static_cast<double>(stepsPerSecond_);

    int steps = 0;
    while (accumulator_ >= 1.0 && steps < kMaxStepsPerFrame && !stopped()) {
        stepOnce();
        accumulator_ -= 1.0;
        ++steps;
    }
    // Biriken pay 1.0'dan kucuk kalir; bir sonraki karede toplanmaya devam eder.
}

bool SimScene::stopped() const {
    return world_.finished() || world_.timedOut();
}

void SimScene::stepOnce() {
    driver_.step();  // karar + world.step() (1 sim-sn)
}

Vec2d SimScene::lanePointAt(LaneId laneId, double distMeters) const {
    const Lane& lane = net_.lane(laneId);
    const auto& cells = lane.cells;
    if (cells.empty()) {
        return Vec2d{0.0, 0.0};
    }
    // Her hucre kCellLength metre. distMeters'i hucre indeksine cevir ve
    // iki hucre merkezi arasinda interpolasyon yap (arac konumu kayan nokta).
    const double cellPos = distMeters / kCellLength;
    const double lastIdx = static_cast<double>(cells.size() - 1);
    const double clamped = std::clamp(cellPos, 0.0, lastIdx);

    const int i0 = static_cast<int>(clamped);
    const int i1 = std::min(i0 + 1, static_cast<int>(cells.size()) - 1);
    const double frac = clamped - static_cast<double>(i0);

    const Vec2i c0 = cells[static_cast<std::size_t>(i0)];
    const Vec2i c1 = cells[static_cast<std::size_t>(i1)];
    // Hucre MERKEZI: (x + 0.5, y + 0.5).
    const double x = (static_cast<double>(c0.x) + 0.5) * (1.0 - frac) +
                     (static_cast<double>(c1.x) + 0.5) * frac;
    const double y = (static_cast<double>(c0.y) + 0.5) * (1.0 - frac) +
                     (static_cast<double>(c1.y) + 0.5) * frac;
    return Vec2d{x, y};
}

double SimScene::laneHeadingAt(LaneId laneId, double distMeters) const {
    // Iki adim arastaki farktan yon acisini hesapla (sadece cizim icin).
    const double eps = 0.5;  // 0.5 metre ileri
    const Vec2d a = lanePointAt(laneId, distMeters);
    const Vec2d b = lanePointAt(laneId, distMeters + eps);
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (dx == 0.0 && dy == 0.0) {
        return 0.0;  // hareketsiz
    }
    return std::atan2(dx, dy);
}

}  // namespace render
}  // namespace tsc
