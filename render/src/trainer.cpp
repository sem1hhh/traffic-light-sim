// render/src/trainer.cpp — IG-6 arka plan egitimi (İOG-3).
//
// Bu dosyada ogrenme mantigi YOKTUR: experiments/train.cpp icindeki
// dogrulanmis `train()` cagrilir. Burada yalnizca (a) worker thread, (b)
// ilerleme kopyasi, (c) modeli dosyaya yazma ve (d) devir vardir.

#include "trainer.h"

#include "bench.hpp"      // Scenario, buildScenarioGrid
#include "sim_scene.h"    // buildRenderGrid, kTrainedPolicyPaths
#include "train.hpp"      // train(), TrainConfig, savePolicy

#include "tsc/grid.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>

namespace tsc {
namespace render {

namespace {

/// Iptal sinyali. Yalnizca progress callback'ten atilir; `train()` bunu
/// yukari birakir ve is parcaciginda yakalanir. Amac: pencere kapanirken
/// 10+ saniye beklemek yerine bir sonraki bolumde temiz durmak.
struct Cancelled {};

/// Canli dunyanin yapilandirmasindan egitim senaryosu.
///
/// Alanlar bench::runOnce'in WorldConfig eslemesiyle AYNI (train.cpp icindeki
/// runEpisode de ayni eslemeyi kullanir). Izgara boyutu varsayilan 9x7 ve
/// buildScenarioGrid canli izgaranin aynisini uretir; bu, start() icinde
/// hucre hucre DOGRULANIR (varsayim birakilmaz).
bench::Scenario scenarioFrom(const WorldConfig& c) {
    bench::Scenario s;
    s.name = "gui-train";
    s.vehicleCount = c.vehicleCount;
    s.spawnInterval = c.spawnInterval;
    s.timeLimit = c.timeLimit;
    s.maxSpeed = c.maxSpeed;
    s.profile = c.profile;
    s.startHour = c.startHour;
    s.minGreen = c.signal.minGreen;
    s.maxGreen = c.signal.maxGreen;
    s.yellow = c.signal.yellow;
    s.allRed = c.signal.allRed;
    return s;
}

/// Iki izgaranin hucre hucre ayni olup olmadigi. Egitim ile canli kosu
/// farkli bir yolda egitilirse Q politikasi "egitildigi isi" yapmaz; bu
/// kontrol o sessiz hatayi baslatma aninda yakalar.
bool sameGrid(const Grid& a, const Grid& b) {
    if (a.size().width != b.size().width || a.size().height != b.size().height) {
        return false;
    }
    for (int y = 0; y < a.size().height; ++y) {
        for (int x = 0; x < a.size().width; ++x) {
            const Vec2i c{x, y};
            if (a.at(c) != b.at(c)) return false;
        }
    }
    return true;
}

}  // namespace

BackgroundTrainer::~BackgroundTrainer() {
    {
        // Iptal: worker bir sonraki bolum sinirinda durur (<= bir bolum).
        std::lock_guard<std::mutex> lk(mu_);
        cancel_ = true;
    }
    if (worker_.joinable()) {
        worker_.join();  // kisa: callback her bolumde iptali kontrol eder
    }
}

bool BackgroundTrainer::start(int episodes, const WorldConfig& live) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (snap_.state == State::Running) return false;
    }
    // Bitmis worker'i topla (join kilit DISINDA: worker da kilidi ister).
    if (worker_.joinable()) worker_.join();

    if (episodes < 1) episodes = 1;
    const bench::Scenario scenario = scenarioFrom(live);

    // Varsayimi dogrula: egitim izgarasi = canli izgara.
    const bool ayniizgara =
        sameGrid(bench::buildScenarioGrid(scenario), buildRenderGrid());
    std::cout << "[egitim] senaryo: " << scenario.vehicleCount << " arac / "
              << scenario.spawnInterval << " s / profil "
              << toString(scenario.profile)
              << "  (canli izgara ile ayni: " << (ayniizgara ? "EVET" : "HAYIR")
              << ")" << std::endl;
    if (!ayniizgara) {
        std::cout << "[egitim] UYARI: egitim izgarasi canli izgaradan FARKLI; "
                     "ogrenilen politika canli senaryoya uymayabilir"
                  << std::endl;
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        snap_ = Snapshot{};
        snap_.state = State::Running;
        snap_.episodes = episodes;
        hasResult_ = false;
        cancel_ = false;
    }

    std::cout << "[egitim] basladi: " << episodes
              << " bolum  (ogrenme TEK is parcaciginda; arayuz donmaz)"
              << std::endl;

    worker_ = std::thread(&BackgroundTrainer::run, this, episodes, live);
    return true;
}

void BackgroundTrainer::run(int episodes, WorldConfig live) {
    const bench::Scenario scenario = scenarioFrom(live);
    const auto basla = std::chrono::steady_clock::now();
    try {
        bench::TrainConfig cfg;
        cfg.episodes = episodes;

        // Ilerleme callback'i WORKER thread'inde cagrilir (train() sozlesmesi).
        // Yaptigi tek sey kilit altinda bir kopya guncellemek; Q tablosuna
        // dokunmaz, cizim yapmaz, dosya yazmaz.
        const auto ilerleme = [this, basla](const bench::EpisodeResult& r) {
            if (cancel_) throw Cancelled{};
            const double gecen = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - basla)
                                     .count();
            std::lock_guard<std::mutex> lk(mu_);
            snap_.done = r.episode + 1;
            snap_.currentEpisode = r.episode;
            snap_.epsilon = r.epsilon;
            snap_.evalTravelTime = r.evalTravelTime;
            snap_.visitedStates = static_cast<int>(r.visitedStates);
            snap_.elapsedSeconds = gecen;
        };

        bench::TrainResult r = bench::train(scenario, cfg, false, ilerleme);
        if (cancel_) throw Cancelled{};

        // Modeli yaz: "kaydedilip yeniden kullanilabilmeli" (IG-6).
        std::string yazilan;
        for (const char* aday : kTrainedPolicyPaths) {
            if (bench::savePolicy(aday, r.policy)) {
                yazilan = aday;
                break;
            }
        }

        const double sure = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - basla)
                                .count();

        std::lock_guard<std::mutex> lk(mu_);
        result_ = std::move(r.policy);
        hasResult_ = true;
        snap_.state = State::Done;
        snap_.elapsedSeconds = sure;
        snap_.saved = !yazilan.empty();
        snap_.savePath = yazilan;
        if (yazilan.empty()) {
            snap_.message = "politika dosyaya YAZILAMADI";
        }
    } catch (const Cancelled&) {
        std::lock_guard<std::mutex> lk(mu_);
        snap_.state = State::Cancelled;
        snap_.message = "iptal edildi (pencere kapandi)";
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(mu_);
        snap_.state = State::Failed;
        snap_.message = e.what();
    } catch (...) {
        std::lock_guard<std::mutex> lk(mu_);
        snap_.state = State::Failed;
        snap_.message = "bilinmeyen hata";
    }
}

BackgroundTrainer::Snapshot BackgroundTrainer::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

bool BackgroundTrainer::takeFinished(QLearningController& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!hasResult_) return false;
    hasResult_ = false;
    out = std::move(result_);
    return true;
}

bool BackgroundTrainer::running() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_.state == State::Running;
}

void BackgroundTrainer::noteFrame(double dtSeconds) {
    if (!(dtSeconds > 0.0)) return;
    // Yalnizca egitim SURERKEN ve ILK BOLUM BITMEDEN SONRA olculur:
    // "egitim arayuzu dondurdu mu" sorusu. Ilk kare, egitim baslamadan
    // ONCEKI kurulumu (model/font yukleme, shader derleme) de kapsar; onu
    // saymak olcumu yaniltir.
    std::lock_guard<std::mutex> lk(mu_);
    if (snap_.state != State::Running || snap_.done == 0) return;
    if (snap_.done == 0) return;
    const double ms = dtSeconds * 1000.0;
    if (ms > snap_.maxFrameMs) snap_.maxFrameMs = ms;
}

}  // namespace render
}  // namespace tsc
