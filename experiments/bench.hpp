#pragma once

#include "tsc/controller.hpp"
#include "tsc/network.hpp"
#include "tsc/world.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tsc {
namespace bench {

/// Deney icin hazirlanan ortak kosul. Her kosu icin AYNI harita kullanilir;
/// boylece kontrolculer ESLESTIRILMIS olarak karsilastirilabilir (K3).
struct Scenario {
    std::string name = "default";
    GridSize size{9, 7};
    int vehicleCount = 60;
    double spawnInterval = 5.0;
    double maxGreen = 20.0;
    double minGreen = 5.0;
    double yellow = 3.0;
    double allRed = 1.0;
    double timeLimit = 1800.0;
    double maxSpeed = 13.9;
    DemandProfile profile = DemandProfile::Uniform;
    double startHour = 8.0;
};

/// Standart test haritasi (uc kavsakli T ag).
Grid buildScenarioGrid(const Scenario& s);

/// Bir kosunun sonucu.
struct RunResult {
    std::uint64_t seed = 0;
    std::string controller;

    double averageTravelTime = 0.0;
    double averageWaitTime = 0.0;   ///< -1 => olculmedi
    int arrived = 0;
    int spawned = 0;
    double averageSystemCount = 0.0;
    double averageQueue = 0.0;
    int maxQueue = 0;
    int collisions = 0;
    double simSeconds = 0.0;
    bool completed = false;
    bool deadlocked = false;
    std::uint64_t digest = 0;
};

/// Kontrolcu fabrikasi: her kavsak icin ayri kontrolcu uretir.
using ControllerFactory = std::function<std::unique_ptr<IController>(JunctionId)>;

/// Tek kosu. `seed` ayni verilirse sonuc birebir aynidir.
RunResult runOnce(const Scenario& scenario, ControllerFactory factory, std::uint64_t seed);

/// Bir kontrolcuyu N tohumla calistir.
std::vector<RunResult> runMany(const Scenario& scenario, ControllerFactory factory,
                               int seedCount, std::uint64_t firstSeed = 1);

}  // namespace bench
}  // namespace tsc