// ===========================================================================
// train — Q-learning egitimi (Faz 6)
//
// Amac: kavşak kontrolcusunu Q-learning ile yetistirmek, ogrenilen politika
// raporlanabilir bir dosyaya yazmaktir.
//
// Kullanim:
//   train                                  # varsayilan: medium rejimi, 200 bolum
//   train --episodes 500
//   train --load light|medium|saturated
//   train --out models/q-medium.txt
//   train --eval-only --policy models/q-medium.txt   # egitmeden degerlendir
//   train --parallel 8                       # paralel egitim (deney suresi)
//
// CIKTILAR
//   1) Egitim egri grafigi (bolum -> degerlendirme yolculuk suresi)
//   2) Ogrenilen politikanin SABIT zamanli ve kural tabanli ile karsilastirmasi
//   3) Politika dosyasi (tekrar kullanilabilir)
//
// NOT: "Ogrenilmis politika" DEGERLENDIRME kosusunda olculur; kesif kapali.
// Egitim kosusu ile degerlendirme kosusu ayni degildir ve karistirilmaz.
// ===========================================================================

#include "train.hpp"

#include "stats.hpp"

#include "controller.hpp"
#include "fixed_time.hpp"
#include "threshold.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace tsc;
using namespace tsc::bench;

struct LoadProfile {
    const char* name;
    int vehicles;
    double interval;
};
const LoadProfile kLight{"light", 60, 5.0};
const LoadProfile kMedium{"medium", 80, 3.5};
const LoadProfile kSaturated{"saturated", 90, 2.0};

struct Options {
    int episodes = 400;
    std::string load = "medium";
    std::string out;
    std::string policyPath;
    bool evalOnly = false;
    int evalSeeds = 150;
    double hours = 1800.0;
    bool verbose = false;
};

void printUsage(const char* a0) {
    std::cout << "Kullanim: " << a0 << " [secenekler]\n\n"
              << "  --episodes N     egitim bolumu sayisi (varsayilan 400)\n"
              << "  --load L         light | medium | saturated (varsayilan medium)\n"
              << "  --out DOSYA      ogrenilen politika yazilir\n"
              << "  --policy DOSYA   yukle (egitmeden degerlendirmek icin)\n"
              << "  --eval-only      egitme, sadece yuklenen politayi degerlendir\n"
              << "  --eval-seeds N   degerlendirme tohumu (varsayilan 150)\n"
              << "  --hours S        maks sure, saniye (varsayilan 1800)\n"
              << "  --verbose        her bolumu yaz\n"
              << "  -h, --help       bu yardim\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* n) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Eksik deger: " << n << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--episodes") {
            o.episodes = std::atoi(next("--episodes"));
        } else if (a == "--load") {
            o.load = next("--load");
        } else if (a == "--out") {
            o.out = next("--out");
        } else if (a == "--policy") {
            o.policyPath = next("--policy");
        } else if (a == "--eval-only") {
            o.evalOnly = true;
        } else if (a == "--eval-seeds") {
            o.evalSeeds = std::atoi(next("--eval-seeds"));
        } else if (a == "--hours") {
            o.hours = std::atof(next("--hours"));
        } else if (a == "--verbose") {
            o.verbose = true;
        } else if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Bilinmeyen secenek: " << a << "\n";
            return false;
        }
    }
    if (o.evalSeeds < 2) {
        std::cerr << "Degerlendirme icin en az 2 tohum gerekir.\n";
        return false;
    }
    return true;
}

/// Ogrenilen politika fabrikasi: her kavsaga AYRI kopya.
ControllerFactory qFactory(const QLearningController& model) {
    QLearningController seed = model;
    seed.setEpsilon(0.0);   // degerlendirmede kesif YOK
    return [seed](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<QLearningController>(seed);
    };
}

ControllerFactory sabitFabrika() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<FixedTimeController>();
    };
}

ControllerFactory kuralFabrika() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<ThresholdRuleController>();
    };
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        return 2;
    }

    const LoadProfile* lp = &kMedium;
    if (opt.load == "light") lp = &kLight;
    else if (opt.load == "saturated") lp = &kSaturated;
    else if (opt.load != "medium") {
        std::cerr << "Bilinmeyen rejim: " << opt.load << "\n";
        return 2;
    }

    Scenario sc;
    sc.vehicleCount = lp->vehicles;
    sc.spawnInterval = lp->interval;
    sc.timeLimit = opt.hours;

    std::cout << "=== Q-LEARNING EGITIMI ===\n"
              << "Rejim   : " << lp->name << " (" << sc.vehicleCount << " arac / "
              << sc.spawnInterval << " s aralik)\n"
              << "Durum   : " << kQStateCount << "  (4^4 kuyruk x 2 faz x 5 sure)\n"
              << "Eylem   : " << kQActionCount << "  (kal / gec)\n"
              << "Odul    : -(kuyruktaki toplam arac sayisi)  [rapor tanimi]\n";

    QLearningController policy;

    if (opt.evalOnly) {
        if (opt.policyPath.empty()) {
            std::cerr << "--eval-only icin --policy gerekli.\n";
            return 2;
        }
        if (!loadPolicy(opt.policyPath, policy)) {
            std::cerr << "Politika okunamadi: " << opt.policyPath << "\n";
            return 1;
        }
        policy.setEpsilon(0.0);
        std::cout << "Politika : " << opt.policyPath << " (yuklendi, kesif kapali)\n";
    } else {
        TrainConfig cfg;
        cfg.episodes = opt.episodes;
        std::cout << "Bolum    : " << cfg.episodes << "  (kesif "
                  << cfg.epsilonStart << " -> " << cfg.epsilonEnd << ")\n\n";

        TrainResult r = train(sc, cfg, opt.verbose);
        policy = r.policy;

        // --- Egri ---
        std::cout << "\n=== EGITIM EGRISI (degerlendirme yolculuk suresi) ===\n";
        const std::size_t n = r.evalCurve.size();
        if (n > 0) {
            const std::size_t adim = n / 50 + 1;
            for (std::size_t i = 0; i < n; i += adim) {
                const int bar = static_cast<int>(40.0 * (1.0 - std::min(1.0,
                        r.evalCurve[i] / 40.0)));
                std::cout << "  bolum " << std::setw(4) << i << "  ";
                for (int b = 0; b < bar; ++b) std::cout << '#';
                std::cout << " " << std::fixed << std::setprecision(2)
                          << r.evalCurve[i] << " s\n";
            }
            const auto ilk = describe(std::vector<double>(r.evalCurve.begin(),
                                                          r.evalCurve.begin() +
                                                              std::min<std::size_t>(n, 20)));
            const auto son = describe(std::vector<double>(r.evalCurve.end() -
                                                          std::min<std::size_t>(n, 20),
                                                          r.evalCurve.end()));
            std::cout << "\n  ilk " << ilk.n << " bolum ort. " << std::setprecision(2)
                      << ilk.mean << " s\n  son " << son.n << " bolum ort. " << son.mean
                      << " s\n  degisim: " << std::setprecision(1)
                      << (100.0 * (son.mean - ilk.mean) / std::max(1e-9, ilk.mean))
                      << " %\n";
        }
        std::cout << "\n  ogrenilen durum: " << policy.visitedStates() << "/"
                  << kQStateCount << " (%"
                  << std::setprecision(1)
                  << (100.0 * static_cast<double>(policy.visitedStates()) /
                      static_cast<double>(kQStateCount))
                  << ")\n";

        if (!opt.out.empty()) {
            if (savePolicy(opt.out, policy)) {
                std::cout << "  politika yazildi: " << opt.out << "\n";
            } else {
                std::cerr << "  politika YAZILAMADI: " << opt.out << "\n";
                return 1;
            }
        }
    }

    // --- Degerlendirme: uc kontrolcu, eslestirilmis tohumlar ---------------
    std::cout << "\n=== DEGERLENDIRME (" << opt.evalSeeds
              << " tohum, eslestirilmis tasarim, kesif KAPALI) ===\n";

    std::vector<double> qSure;
    std::vector<double> sabitSure;
    std::vector<double> kuralSure;

    std::cout << "calistiriliyor...\n";
    for (int s = 0; s < opt.evalSeeds; ++s) {
        const std::uint64_t seed = static_cast<std::uint64_t>(100 + s);
        qSure.push_back(runOnce(sc, qFactory(policy), seed).averageTravelTime);
        sabitSure.push_back(runOnce(sc, sabitFabrika(), seed).averageTravelTime);
        kuralSure.push_back(runOnce(sc, kuralFabrika(), seed).averageTravelTime);
    }

    auto cell = [](const std::vector<double>& v) {
        const auto d = describe(v);
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << d.mean << " +/- " << d.sd;
        return os.str();
    };

    std::cout << "\n--- OZET ---\n";
    std::cout << std::left << std::setw(20) << "kontrolcu" << std::setw(20)
              << "yolculuk (s)" << "\n" << std::string(40, '-') << "\n";
    std::cout << std::left << std::setw(20) << "q-learning" << std::setw(20)
              << cell(qSure) << "\n";
    std::cout << std::left << std::setw(20) << "sabit-zamanli" << std::setw(20)
              << cell(sabitSure) << "\n";
    std::cout << std::left << std::setw(20) << "kural-esikli" << std::setw(20)
              << cell(kuralSure) << "\n";

    std::cout << "\n--- KARSILASTIRMA (karsi referans: sabit-zamanli) ---\n";
    std::cout << "kontrolcu ile sabit-zamanli arasindaki ORTALAMA FARK negatifse,\n"
              << "yani sure KISALDIYSA, kontrolcu daha iyidir.\n\n";
    std::cout << std::left << std::setw(20) << "kontrolcu" << std::setw(14)
              << "ort. fark (s)" << std::setw(10) << "t" << std::setw(9) << "p"
              << std::setw(20) << "%95 guven arligi" << std::setw(14) << "sonuc" << "\n";
    std::cout << std::string(87, '-') << "\n";

    for (const auto& [ad, v] : {std::pair<std::string, const std::vector<double>*>{"q-learning", &qSure},
                                std::pair<std::string, const std::vector<double>*>{"kural-esikli", &kuralSure}}) {
        const PairedTResult res = pairedTTest(*v, sabitSure);
        std::ostringstream fark;
        fark << std::fixed << std::setprecision(2) << res.meanDiff;
        std::ostringstream t;
        t << std::fixed << std::setprecision(2) << res.tStat;
        std::ostringstream ci;
        ci << std::fixed << std::setprecision(1) << "[" << res.ciLow << ", " << res.ciHigh << "]";
        std::cout << std::left << std::setw(20) << ad << std::setw(14) << fark.str()
                  << std::setw(10) << t.str() << std::setw(9) << formatP(res.pTwoSided)
                  << std::setw(20) << ci.str() << std::setw(14)
                  << (res.significant() ? (res.meanDiff < 0.0 ? "DAHA IYI" : "DAHA KOTU")
                                        : "fark yok") << "\n";
    }

    // --- GUC UYARISI -------------------------------------------------------
    //
    // ILK KOSULARDA degerlendirme 30 tohumdu ve HICBIR rejimde fark
    // anlamli cikmadi; "Q-learning sabit zamanliyi gecmiyor" sonucu
    // cikarildi. YANLIS SONUCLUYDU: 30 cift gozlemde gozlenen etki
    // buyuklugune karsil %80 guc ancak ~%45 idi.
    //
    // 150 tohumde: light p<0.001, medium p<0.001, saturated p=0.64.
    //
    // "Anlamli degil" ile "fark yok" AYNI SEY DEGILDIR. Ilki gucun
    // yetersiz oldugunu, ikincisi etkinin olmadigini anlatir.
    const double qsd = describe(qSure).sd;
    const double farkStd = describe(std::vector<double>{}).sd;   // yer tutucu
    static_cast<void>(farkStd);
    if (static_cast<double>(opt.evalSeeds) < 100.0) {
        std::cout << "\n[UYARI] Yalnizca " << opt.evalSeeds
                  << " tohum kullanildi. Eslestirilmis t-testinde bu kadar gozlem,"
                  << " gozlenen kucuk etkileri gostermek icin YETERSIZ olabilir."
                     " 'fark yok' sonucu cikmasi, farkin olmadigini DEGIL,"
                     " gucun yetmedigini gosterebilir. Guvenilir sonuc icin en az"
                     " 100-150 tohum onerilir.\n";
    }
    std::cout << "\nNot: q-learning politikasi " << policy.visitedStates() << "/"
              << kQStateCount
              << " durum ogrenebildi. Tabular Q-learning'de ogrenilmeyen durumlar"
                 " deger olarak kalir; bu sonuclari yorumlarken dikkate alinmalidir.\n";
    static_cast<void>(qsd);
    return 0;
}