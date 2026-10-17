#include "bench.hpp"

#include "controller.hpp"

namespace tsc {
namespace bench {

Grid buildScenarioGrid(const Scenario& s) {
    // Izgara ortasinda yatay omurga, ucun da dikey kollar: T kavsaklar.
    // Sol ve sag uclar serit sonudur -> giris/cikis noktasi olur.
    Grid g{GridSize{9, 7}};
    for (int x = 0; x < s.size.width; ++x) {
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

RunResult runOnce(const Scenario& scenario, ControllerFactory factory, std::uint64_t seed) {
    RunResult r;
    r.seed = seed;

    const Network net = buildNetwork(buildScenarioGrid(scenario));

    WorldConfig cfg;
    cfg.vehicleCount = scenario.vehicleCount;
    cfg.spawnInterval = scenario.spawnInterval;
    cfg.timeLimit = scenario.timeLimit;
    cfg.maxSpeed = scenario.maxSpeed;
    cfg.profile = scenario.profile;
    cfg.startHour = scenario.startHour;
    cfg.signal.minGreen = scenario.minGreen;
    cfg.signal.maxGreen = scenario.maxGreen;
    cfg.signal.yellow = scenario.yellow;
    cfg.signal.allRed = scenario.allRed;

    World world{net, cfg, seed};
    SignalDriver driver{world, factory};

    // Baslangic transiyentini (warmup) disarida birak: Little yasasi ve
    // karsilastirmalar kararli durum icin gecerlidir.
    while (!world.finished() && !world.timedOut() && world.time() < scenario.timeLimit) {
        driver.step();
    }

    const Metrics& m = world.metrics();
    r.averageTravelTime = m.averageTravelTime();
    // Sansursuz metrik: sistemde kalan araclari da sayar.
    r.averageTimeInSystem = world.averageTimeInSystem();
    r.arrivedFraction = m.arrivedFraction();
    const auto wait = m.averageWaitTime();
    r.averageWaitTime = wait.has_value() ? *wait : -1.0;
    r.arrived = m.arrived;
    r.spawned = m.spawned;
    r.averageSystemCount = m.averageSystemCount();
    r.averageQueue = m.averageQueue();
    r.maxQueue = m.maxQueue;
    r.collisions = m.collisions;
    r.simSeconds = world.time();
    r.completed = world.complete();
    r.deadlocked = world.deadlocked();
    r.switchRequests = driver.switchRequests();
    r.digest = world.digest();
    return r;
}

std::vector<RunResult> runMany(const Scenario& scenario, ControllerFactory factory,
                               int seedCount, const std::string& label,
                               std::uint64_t firstSeed) {
    std::vector<RunResult> out;
    out.reserve(static_cast<std::size_t>(seedCount));
    for (int i = 0; i < seedCount; ++i) {
        RunResult r = runOnce(scenario, factory, firstSeed + static_cast<std::uint64_t>(i));
        // Etiket burada yazilir; runOnce bilmez (kosu motoru etiketle ilgilenmez).
        r.controller = label;
        static_cast<void>(label);
        out.push_back(r);
    }
    return out;
}

}  // namespace bench
}  // namespace tsc
