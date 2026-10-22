// render/tools/eval_check.cpp — IG-7 sonuc panelinin SAYILARINI GUI'siz basar.
//
// Neden gerekli: panelin dogrulugu "ekranda gorundu" ile olculmez. Bu arac
// panelin kullandigi AYNI fonksiyonu (runHeadless) ve AYNI senaryoyu
// (renderScenarioConfig) cagirir; boylece sayilar pencere acmadan
// tekrarlanabilir sekilde kontrol edilebilir.
//
// Ayrica BOS OLMA KANITI (AGENTS §8.14): Q-learning satiri sabit zamanliyla
// birebir ayniysa ya tablo yuklenmemistir ya da politika hic farkli karar
// vermiyordur. Arac bunu uyari olarak basar ve sifirdan farkli kodla cikar.
//
// Kullanim: ./build/tsc_eval_check [tohum...]      (varsayilan: 1)

#include "sim_scene.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool loadPolicyFromCandidates(tsc::QLearningController& out, std::string& path) {
    for (int i = 0; tsc::render::qPolicyPathCandidate(i) != nullptr; ++i) {
        const char* p = tsc::render::qPolicyPathCandidate(i);
        std::ifstream f{p};
        if (!f) continue;
        try {
            out.deserialize(f);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[eval] %s okunamadi: %s\n", p, e.what());
            return false;
        }
        out.setEpsilon(0.0);  // gosterim/degerlendirme: kesif YOK
        path = p;
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::uint64_t> seeds;
    for (int i = 1; i < argc; ++i) {
        seeds.push_back(std::strtoull(argv[i], nullptr, 10));
    }
    if (seeds.empty()) seeds.push_back(1);

    tsc::QLearningController policy;
    std::string policypath;
    const bool loaded = loadPolicyFromCandidates(policy, policypath);
    if (loaded) {
        std::printf("[eval] Q politikasi: %s  (ziyaret %zu/%zu, bilinen %zu, epsilon %.2f)\n",
                    policypath.c_str(), policy.visitedStates(), policy.tableSize(),
                    policy.knownStates(), policy.params().epsilon);
    } else {
        std::printf("[eval] UYARI: Q politikasi bulunamadi; Q satiri sabit zamanliya duser\n");
    }

    const tsc::WorldConfig cfg = tsc::render::renderScenarioConfig();
    std::printf("[eval] senaryo: %d arac / %.1f s / profil %s / minGreen %.1f maxGreen %.1f\n",
                cfg.vehicleCount, cfg.spawnInterval,
                tsc::toString(cfg.profile), cfg.signal.minGreen,
                cfg.signal.maxGreen);

    const tsc::render::ControllerKind kinds[3] = {
        tsc::render::ControllerKind::FixedTime,
        tsc::render::ControllerKind::ThresholdRule,
        tsc::render::ControllerKind::QLearning};

    bool anydiff = false;
    for (const std::uint64_t seed : seeds) {
        std::printf("\n== tohum %llu ==\n", static_cast<unsigned long long>(seed));
        std::printf("%-16s %10s %10s %8s %10s %8s %8s %s\n", "kontrolcu",
                    "yolculuk", "sistemde", "varis%", "varan/uretilen",
                    "gridlock", "sim-sn", "durum");
        tsc::render::EvalRow fixed;
        tsc::render::EvalRow q;
        for (const auto k : kinds) {
            const tsc::render::EvalRow r =
                tsc::render::runHeadless(seed, k, cfg, loaded ? &policy : nullptr);
            std::printf("%-16s %9.2f %10.2f %7.1f%% %6d/%-5d %8d %8.0f %s\n",
                        r.controller.c_str(), r.averageTravelTime,
                        r.averageTimeInSystem, r.arrivedFraction * 100.0,
                        r.arrived, r.spawned, r.stalledEvents, r.simSeconds,
                        r.finished && !r.timedOut ? "tamam" : "zaman asimi");
            if (k == tsc::render::ControllerKind::FixedTime) fixed = r;
            if (k == tsc::render::ControllerKind::QLearning) q = r;
        }
        // "Bos degil" kontrolu: iki kontrolcu FARKLI sonuc uretebiliyor mu?
        // Sansursuz metrik de karsilastirilir; yolculuk tek basina yetmez.
        const bool diff = (fixed.arrived != q.arrived) ||
                          (fixed.averageTravelTime != q.averageTravelTime) ||
                          (fixed.averageTimeInSystem != q.averageTimeInSystem) ||
                          (fixed.stalledEvents != q.stalledEvents);
        anydiff = anydiff || diff;
        if (!diff) {
            std::printf("  [UYARI] Q satiri sabit zamanliyla BIREBIR AYNI; "
                        "politika kullanilmiyor olabilir.\n");
        }
    }

    std::printf("\n%s\n", anydiff ? "[eval] OK: Q satiri farkli sonuc uretiyor (bos degil)."
                                  : "[eval] FARKLI SONUC YOK (politika etkisiz?).");
    return anydiff ? 0 : 1;
}
