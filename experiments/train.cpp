#include "train.hpp"

#include "controller.hpp"
#include "fixed_time.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace tsc {
namespace bench {
namespace {

/// Degerlendirmede kullanilan tohum sayisi. Tek tohum egrinin taban
/// gürültüsünden ayırt edilemiyordu.
constexpr int kEvalSeeds = 5;

/// Bir bolumu DONER DONDURerek calistirir ve ogrenilen tablolari modele
/// geri yazar.
///
/// Neden runOnce() yetmiyor: runOnce kontrolcuyu fabrikadan URETIR ve kosu
/// bitince nesneyi atar. Ogrenme bu nesnenin ICINDE olur; fabrika verilen
/// tablonun kopyasi degistirilmez. Ilk denemede boyle yazilmisti ve
/// 200 bolum egitimden sonra 0/2560 durum ogrenilmisti — "ogrenme calismiyor"
/// goruntusu, oysa sonuc sadece bir nesne yasam suresi sorunu idi.
struct EpisodeOutcome {
    double averageTravelTime = 0.0;
    double averageQueue = 0.0;
    int completed = 0;
    std::uint64_t switchRequests = 0;
};

EpisodeOutcome runEpisode(const Scenario& scenario, QLearningController& model,
                          std::uint64_t seed) {
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

    // Her kavsak AYRI bir Q tablosu tutar: (s,a,r,s') zinciri kavsaklar
    // arasi tasinmamalidir. Ortak tablo kullanilsa tek kavsagin deneyimi
    // digerine karisir.
    SignalDriver driver{world, [&model](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<QLearningController>(model);
    }};

    while (!world.finished() && !world.timedOut() && world.time() < scenario.timeLimit) {
        driver.step();
    }

    // Ogrenilen tablolari modele geri yaz: her kavsagin tablosu ayni
    // modelden kopyalandigi icin ilk kavsagin tablosu yeterlidir; yine de
    // hepsini birlestiriyoruz (simetrik agda zaten aynilardir).
    for (const JunctionId j : net.signalJunctions()) {
        const auto& c = dynamic_cast<const QLearningController&>(driver.controllerAt(j));
        model = c.copy();
    }

    EpisodeOutcome out;
    out.averageTravelTime = world.metrics().averageTravelTime();
    out.averageQueue = world.metrics().averageQueue();
    out.completed = world.complete() ? 1 : 0;
    out.switchRequests = driver.switchRequests();
    return out;
}

/// Kesif olasiligini bolum ilerlemesine gore azalt.
double epsilonAt(const TrainConfig& cfg, int episode) {
    if (cfg.episodes <= 1) {
        return cfg.epsilonEnd;
    }
    const double t = static_cast<double>(episode) / static_cast<double>(cfg.episodes - 1);
    return cfg.epsilonStart + (cfg.epsilonEnd - cfg.epsilonStart) * t;
}

}  // namespace

TrainResult train(const Scenario& scenario, const TrainConfig& cfg, bool verbose) {
    TrainResult sonuc;

    QLearningController::Params p;
    p.alpha = cfg.alpha;
    p.gamma = cfg.gamma;
    p.epsilon = cfg.epsilonStart;
    sonuc.policy = QLearningController{p};

    for (int ep = 0; ep < cfg.episodes; ++ep) {
        const double eps = epsilonAt(cfg, ep);
        sonuc.policy.setEpsilon(eps);

        // --- 1) Egitim kosusu (kesif ACIK) --------------------------------
        // Tohum her bolumde degisir: hep ayni kosuda egitmek, o kosuya
        // asiri uyum (overfitting) uretir ve genellenme olcumunu bozar.
        const std::uint64_t seed = cfg.firstSeed + static_cast<std::uint64_t>(ep);
        const EpisodeOutcome egitim = runEpisode(scenario, sonuc.policy, seed);

        // --- 2) Degerlendirme kosusu (kesif KAPALI) -----------------------
        sonuc.policy.setEpsilon(0.0);
        // Degerlendirme icin SABIT bir tohum: tum bolumler ayni kosuda
        // olculurse egri karsilastirilabilir olur.
        // Degerlendirme TEK tohumda yapilirsa egrri kiris tabanindan
        // gürültü olarak ayırt edilemez. Bir kac tohumun ORTALAMASI alinir.
        EpisodeOutcome d;
        for (int k = 0; k < kEvalSeeds; ++k) {
            const EpisodeOutcome e =
                runEpisode(scenario, sonuc.policy, 9001 + static_cast<std::uint64_t>(k));
            d.averageTravelTime += e.averageTravelTime / static_cast<double>(kEvalSeeds);
            d.averageQueue += e.averageQueue / static_cast<double>(kEvalSeeds);
            d.completed += e.completed;
            d.switchRequests += e.switchRequests;
        }
        sonuc.policy.setEpsilon(eps);

        sonuc.evalCurve.push_back(d.averageTravelTime);

        EpisodeResult r;
        r.episode = ep;
        r.epsilon = eps;
        r.trainTravelTime = egitim.averageTravelTime;
        r.evalTravelTime = d.averageTravelTime;
        r.evalQueue = d.averageQueue;
        r.evalCompleted = d.completed;
        r.evalSwitches = d.switchRequests;
        r.visitedStates = sonuc.policy.visitedStates();
        sonuc.history.push_back(r);

        if (verbose && (ep % 10 == 0 || ep == cfg.episodes - 1)) {
            std::cout << "  bolum " << std::setw(4) << ep << "  eps=" << std::fixed
                      << std::setprecision(3) << eps
                      << "  degerlendirme yolculuk=" << std::setprecision(2)
                      << d.averageTravelTime << " s"
                      << "  kuyruk=" << std::setprecision(2) << d.averageQueue
                      << "  gecis=" << d.switchRequests
                      << "  durum=" << r.visitedStates << "/" << kQStateCount << "\n";
        }
    }

    // Degerlendirmede kullanilan kesif sifirlanir: kaydedilen politika
    // KESIF YAPMAYAN politika olmalidir.
    sonuc.policy.setEpsilon(0.0);
    return sonuc;
}

bool savePolicy(const std::string& path, const QLearningController& policy) {
    std::ofstream f{path};
    if (!f) {
        return false;
    }
    policy.serialize(f);
    return static_cast<bool>(f);
}

bool loadPolicy(const std::string& path, QLearningController& policy) {
    std::ifstream f{path};
    if (!f) {
        return false;
    }
    try {
        policy.deserialize(f);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

}  // namespace bench
}  // namespace tsc
