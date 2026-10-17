// ===========================================================================
// bench — karsi lastirma deney motoru (K3 altyapisi)
//
// Amac: ayni harita + ayni tohumlar uzerinde BIRDEN FAZLA kontrolcuyu
// calistirip metrikleri CSV'ye yazmak ve eslestirilmis t-testi ile
// karsilastirmak.
//
// Kullanım:
//   bench                       # varsayilan: 3 kontrolcu x 30 tohum
//   bench --seeds 50            # daha cok tohum
//   bench --out csv/            # CSV klasoru (bos birakilirsa stdout)
//   bench --vehicles 80         # yogunluk
//   bench --interval 3.0        # dogum araligi (saniye)
//   bench --hours 900           # maks simule edilen sure
//   bench --profile peak       # talep profili: uniform|morning|evening|constant
//   bench --no-csv              # yalnizca ekrana yaz
//
// Cikti:
//   1) ham kosu satirlari (CSV) — her kontrolcu x her tohum
//   2) ozet tablosu (ekrana)
//   3) karsilastirma (ekrana) — her kontrolcu, sabit zamali KONTROL GRUBU'na karsi
// ===========================================================================

#include "bench.hpp"
#include "stats.hpp"

#include "fixed_time.hpp"
#include "threshold.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace tsc;
using namespace tsc::bench;

// --- istek ayristirma -------------------------------------------------------

struct Options {
    int seeds = 30;
    std::string outDir;
    int vehicles = 0;   // 0 = rejimin kendi degerini kullan
    double interval = 5.0;
    double hours = 1800.0;
    std::string profile = "uniform";
    bool csv = true;
    std::string load = "all";   // light | medium | saturated | all
};

/// Talep rejimi. Ucunun birlikte calistirilmasi, rapor icin
/// YUK-KAPASITE EGRISI verir: kontrolcunun degerinin nereye kadar
/// gorunur oldugunu gosterir.
struct LoadProfile {
    const char* name;
    int vehicles;
    double interval;
};

const LoadProfile kLight{    "light",     60, 5.0};
const LoadProfile kMedium{   "medium",    80, 3.5};
const LoadProfile kSaturated{"saturated", 90, 2.0};

const LoadProfile* loadProfiles(const std::string& which,
                                std::vector<LoadProfile>& out) {
    if (which == "all") {
        out = {kLight, kMedium, kSaturated};
    } else if (which == "light") {
        out = {kLight};
    } else if (which == "medium") {
        out = {kMedium};
    } else if (which == "saturated") {
        out = {kSaturated};
    } else {
        return nullptr;
    }
    return out.data();
}

void printUsage(const char* argv0) {
    std::cout
        << "Kullanim: " << argv0 << " [secenekler]\n\n"
        << "  --seeds N          tohum sayisi (varsayilan 30)\n"
        << "  --out DIR          CSV cikti klasoru (yoksa sadece ekran)\n"
        << "  --vehicles N       arac sayisini SABITLER (varsayilan: rejimin kendi degeri)\n"
        << "  --interval S       dogum araligi, saniye (varsayilan 5)\n"
        << "  --hours S          maks simule edilen sure, saniye (varsayilan 1800)\n"
        << "  --profile P        uniform | morning | evening\n"
        << "  --load L           light | medium | saturated | all (varsayilan all)\n"
        << "  --no-csv           CSV yazma\n"
        << "  -h, --help         bu yardim\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Eksik deger: " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--seeds") {
            o.seeds = std::atoi(next("--seeds"));
        } else if (a == "--out") {
            o.outDir = next("--out");
        } else if (a == "--vehicles") {
            o.vehicles = std::atoi(next("--vehicles"));
        } else if (a == "--interval") {
            o.interval = std::atof(next("--interval"));
        } else if (a == "--hours") {
            o.hours = std::atof(next("--hours"));
        } else if (a == "--profile") {
            o.profile = next("--profile");
        } else if (a == "--load") {
            o.load = next("--load");
        } else if (a == "--no-csv") {
            o.csv = false;
        } else if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Bilinmeyen secenek: " << a << "\n";
            printUsage(argv[0]);
            return false;
        }
    }
    if (o.seeds < 2) {
        std::cerr << "En az 2 tohum gerekir (t-testi icin).\n";
        return false;
    }
    return true;
}

DemandProfile parseProfile(const std::string& s) {
    if (s == "morning") return DemandProfile::MorningPeak;
    if (s == "evening") return DemandProfile::EveningPeak;
    return DemandProfile::Uniform;
}

// --- kontrolcu fabrikalari --------------------------------------------------

bench::ControllerFactory fixedFactory() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<FixedTimeController>();
    };
}

/// Kural tabanli kontrolcu. Azami yesil suresi BURADA verilmez: kontrolcu
/// kavsagin kendi `maxGreen`'inden turetir (bkz. ThresholdRuleController::Params).
/// Vermek, olcumde "azami yesilli" yazmamiza raomen o kuralin olcumde
/// tetiklenmemis olmasi demekti.
bench::ControllerFactory thresholdFactory() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<ThresholdRuleController>();
    };
}

// --- ozet ------------------------------------------------------------------

std::vector<double> column(const std::vector<RunResult>& rs, double RunResult::*m) {
    std::vector<double> v;
    v.reserve(rs.size());
    for (const RunResult& r : rs) {
        v.push_back(r.*m);
    }
    return v;
}

/// Bekleme sutunu — OLCELMEMIS degerler (-1.0 isareti) ELLENIR.
///
/// Neden: `averageWaitTime` hic arac varmadiginda -1.0 doner. O deger
/// describe() ortalamasina karissaydi ortalamayi asagi cekerdi: yani en
/// KOTU kosul, EN IYI bekleme suresini uretirdi (BULGU 2 ile ayni tuzak).
/// Artik ortalama yalnizca GERCEKTEN olculmus kosularin ortalamasidir.
std::vector<double> beklemeColumn(const std::vector<RunResult>& rs) {
    std::vector<double> v;
    v.reserve(rs.size());
    for (const RunResult& r : rs) {
        if (r.averageWaitTime >= 0.0) {   // -1.0 isareti ele
            v.push_back(r.averageWaitTime);
        }
    }
    return v;
}

/// Sansur (censoring) uyarisi: zaman asiminda kalan araclar `averageTravelTime`
/// hesabina GIRMEZ. Iki kontrolcunun bitirme orani farkliysa karsilastirma
/// esitsizdir: daha cok gridlock yapan kontrolcu, daha IYI gorunen bir
/// yolculuk suresi uretir.
///
/// Olculdu (saturated, 30 tohum): korelasyon(varan, yolculuk) = +0.66 — yani
/// daha az varis, daha dusuk gorunen sure. Bu, "kural tabanli daha iyi"
/// sonucunu tek basina uretebilir. (denetim.md BULGU 2)
void printCensoringWarning(const std::map<std::string, std::vector<RunResult>>& runs) {
    double enKotu = 0.0;
    double enIyi = 1.0;
    std::string enKotuAd, enIyiAd;
    for (const auto& [name, rs] : runs) {
        if (rs.empty()) continue;
        const double fr = describe(column(rs, &RunResult::arrivedFraction)).mean;
        if (fr > enKotu) { enKotu = fr; enKotuAd = name; }
        if (fr < enIyi) { enIyi = fr; enIyiAd = name; }
    }
    // 5 puan fark, "hangisi daha iyi" kararini zaten bozebilir.
    if (enKotu - enIyi < 0.05) {
        return;
    }
    std::ostringstream oran;
    oran << std::fixed << std::setprecision(1) << (100.0 * enIyi) << "%";
    std::cout << "\n[SANSUR UYARISI] Kontrolculerin varis orani farkli: en dusuk '"
              << enIyiAd << "' (" << oran.str()
              << "), en yuksek '" << enKotuAd << "' (" << std::fixed
              << std::setprecision(1) << (100.0 * enKotu) << "%).\n"
              << "  'yolculuk (s)' sutunu yalnizca VARAN araclari sayar; zaman asiminda\n"
              << "  kalanlar hesaba girmez. Boylece daha cok gridlock yapan kontrolcu\n"
              << "  daha IYI gorunebilir. Karsilastirmayi 'ortalama sistemde kalis'\n"
              << "  (CSV: average_time_in_system) ile kontrol edin.\n";
}

void printSummary(const std::map<std::string, std::vector<RunResult>>& runs) {
    std::cout << "\n=== OZET (ortalama +/- orneklem sapmasi) ===\n";
    std::cout << std::left << std::setw(16) << "kontrolcu" << std::setw(5) << "n"
              << std::setw(19) << "yolculuk (s)" << std::setw(19) << "sistemde (s)"
              << std::setw(9) << "varis %" << std::setw(19) << "bekleme (s)"
              << std::setw(19) << "ort. kuyruk" << std::setw(11) << "tamamlanan"
              << std::setw(11) << "gridlock" << std::setw(13) << "ort. gecis"
              << std::setw(10) << "carpisma" << "\n";
    std::cout << std::string(150, '-') << "\n";

    for (const auto& [name, rs] : runs) {
        if (rs.empty()) continue;
        const auto t = describe(column(rs, &RunResult::averageTravelTime));
        // Sansursuz metrik: sistemde kalan araclari da sayar.
        const auto ts = describe(column(rs, &RunResult::averageTimeInSystem));
        const auto fr = describe(column(rs, &RunResult::arrivedFraction));
        // -1.0 isareti ele; ortalama yalnizca GERCEKTEN olculmus kosulda.
        const auto w = describe(beklemeColumn(rs));
        const auto q = describe(column(rs, &RunResult::averageQueue));
        int done = 0;
        int coll = 0;
        for (const RunResult& r : rs) {
            if (r.completed) ++done;
            coll += r.collisions;
        }
        // Ort +/- sapma icin tek bir hucre: "9.77 +/- 1.40". setw YALNIZCA
        // bir sonraki yazma icin gecerlidir; birden fazla parcayi birlikte
        // yazarsan (t.mean << " +/-" << t.sd) sonraki sutunlar kayar.
        // Her hucre ONCEDEN string'e cevrilip tek setw ile basilir.
        auto hucre = [](double mean, double sd) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2) << mean << " +/- " << sd;
            return os.str();
        };
        int kilitli = 0;
        double gecisOrt = 0.0;
        for (const RunResult& r : rs) {
            if (r.deadlocked) ++kilitli;
            gecisOrt += static_cast<double>(r.switchRequests);
        }
        gecisOrt /= static_cast<double>(rs.size());
        std::ostringstream tam;
        tam << done << "/" << rs.size();
        std::ostringstream gec;
        gec << std::fixed << std::setprecision(1) << gecisOrt;
        std::ostringstream varis;
        varis << std::fixed << std::setprecision(1) << (100.0 * fr.mean) << "%";
        std::cout << std::left << std::setw(16) << name << std::setw(5) << rs.size()
                  << std::setw(19) << hucre(t.mean, t.sd) << std::setw(19)
                  << hucre(ts.mean, ts.sd) << std::setw(9) << varis.str()
                  << std::setw(19) << hucre(w.mean, w.sd) << std::setw(19)
                  << hucre(q.mean, q.sd) << std::setw(11) << tam.str() << std::setw(11) << kilitli
                  << std::setw(13) << gec.str() << std::setw(10) << coll << "\n";
    }
}

void printComparisons(const std::map<std::string, std::vector<RunResult>>& runs,
                      const std::string& kontrolGrubu) {
    auto it = runs.find(kontrolGrubu);
    if (it == runs.end() || it->second.size() < 2) {
        return;
    }
    const std::vector<double> ref = column(it->second, &RunResult::averageTravelTime);

    std::cout << "\n=== KARSILASTIRMA — karsi referans: " << kontrolGrubu
              << " (eslestirilmis t-testi, yolculuk suresi) ===\n";
    std::cout << "kontrolcu ile " << kontrolGrubu << " arasindaki ORTALAMA FARK negatifse,\n"
              << "yani sure KISALDIYSA, kontrolcu daha iyidir.\n\n";
    // Kucuk orneklemde t-testi ispat gucu zayiftir. Bunu soylemek, sonucu
    // oldugundan guclu gostermekten iyidir.
    if (ref.size() < 10) {
        std::cout << "\n[UYARI] Yalnizca " << ref.size() << " tohum kullanildi. "
                  << "Guven araligi genis, p degeri guvenilmezdir; "
                  << "rapor icin en az 30 tohum onerilir.\n";
    }
    std::cout << std::left << std::setw(16) << "kontrolcu" << std::setw(14) << "ort. fark (s)"
              << std::setw(10) << "t" << std::setw(9) << "p" << std::setw(20)
              << "%95 guven arligi" << std::setw(14) << "sonuc" << "\n";
    std::cout << std::string(83, '-') << "\n";

    for (const auto& [name, rs] : runs) {
        if (name == kontrolGrubu || rs.size() != ref.size()) continue;
        const auto r = pairedTTest(column(rs, &RunResult::averageTravelTime), ref);
        std::ostringstream ci;
        ci << std::fixed << std::setprecision(1) << "[" << r.ciLow << ", " << r.ciHigh << "]";
        std::ostringstream fark;
        fark << std::fixed << std::setprecision(2) << r.meanDiff;
        std::ostringstream t;
        t << std::fixed << std::setprecision(2) << r.tStat;
        std::cout << std::left << std::setw(16) << name << std::setw(14) << fark.str()
                  << std::setw(10) << t.str() << std::setw(9) << formatP(r.pTwoSided)
                  << std::setw(20) << ci.str() << std::setw(14)
                  << (r.significant() ? (r.meanDiff < 0.0 ? "DAHA IYI" : "DAHA KOTU")
                                      : "fark yok") << "\n";
    }
}

void writeCsv(const std::string& dir, const std::vector<RunResult>& runs) {
    // Klasoru ac (deney sirasinda hata verip durmasin diye).
    const std::string cmd = "mkdir -p '" + dir + "'";
    if (std::system(cmd.c_str()) != 0) {
        std::cerr << "UYARI: CSV klasoru olusturulamadi: " << dir << "\n";
        return;
    }
    const std::string path = dir + "/bench_runs.csv";
    std::ofstream f{path};
    if (!f) {
        std::cerr << "UYARI: CSV acilamadi: " << path << "\n";
        return;
    }
    f << "load,controller,seed,average_travel_time,average_time_in_system,"
         "arrived_fraction,average_wait_time,arrived,spawned,"
         "average_system_count,average_queue,max_queue,collisions,sim_seconds,completed,"
         "deadlocked,switch_requests,digest\n";
    f << std::fixed << std::setprecision(6);
    for (const RunResult& r : runs) {
        // "kontrolcu@rejim" etiketi iki parcaya ayrilir: sutunlar ayri ayri
        // olca rapor tablosu filtrelenebilir.
        const std::size_t at = r.controller.find('@');
        f << (at == std::string::npos ? std::string("") : r.controller.substr(at + 1)) << ','
          << (at == std::string::npos ? r.controller : r.controller.substr(0, at)) << ','
          << r.seed << ',' << r.averageTravelTime << ',' << r.averageTimeInSystem << ','
          << r.arrivedFraction << ',' << r.averageWaitTime << ','
          << r.arrived << ',' << r.spawned << ',' << r.averageSystemCount << ','
          << r.averageQueue << ',' << r.maxQueue << ',' << r.collisions << ','
          << r.simSeconds << ',' << (r.completed ? 1 : 0) << ',' << (r.deadlocked ? 1 : 0)
          << ',' << r.switchRequests << ',' << r.digest << '\n';
    }
    std::cout << "\nCSV yazildi: " << path << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        return 2;
    }

    std::vector<LoadProfile> profiller;
    if (loadProfiles(opt.load, profiller) == nullptr) {
        std::cerr << "Bilinmeyen rejim: " << opt.load
                  << " (light | medium | saturated | all)\n";
        return 2;
    }

    struct Entry {
        std::string name;
        bench::ControllerFactory factory;
    };
    // KONTROL GRUBU ilk sirada: karsilastirma referansi odur.
    const std::vector<Entry> kontroller{
        {"sabit-zamanli", fixedFactory()},
        {"kural-esikli", thresholdFactory()},
    };

    std::cout << "=== TRAFIK ISIGI KONTROLU KARSILASTIRMASI ===\n"
              << "Harita : T tipi, 3 kavsak, 9x7 izgara\n"
              << "Tohum  : " << opt.seeds << " (eslestirilmis tasarim)\n"
              << "Maks   : " << opt.hours << " s\n"
              << "Rejim  : " << opt.load << "\n";

    // Tum kosular TEK duz listeye yazilir; rejim, kontrolcu etiketinin
    // icinde tasinir ve CSV'de ayri sutun olur. (map kullanmak tohumu
    // etiketin parcasina katistiriyordu: load sutunu "light/1" oluyordu.)
    std::vector<RunResult> tumKosular;

    for (const LoadProfile& lp : profiller) {
        Scenario sc;
        sc.name = std::string("load=") + lp.name;
        sc.vehicleCount = opt.vehicles > 0 ? opt.vehicles : lp.vehicles;
        sc.spawnInterval = lp.interval;
        sc.timeLimit = opt.hours;
        sc.profile = parseProfile(opt.profile);

        std::cout << "\n\n########## REJIM: " << lp.name << " (" << sc.vehicleCount
                  << " arac / " << sc.spawnInterval << " s aralik) ##########\n";

        std::map<std::string, std::vector<RunResult>> runs;
        for (const Entry& e : kontroller) {
            std::cout << "  " << e.name << " (" << opt.seeds << " tohum)... " << std::flush;
            std::vector<RunResult> rs =
                runMany(sc, e.factory, opt.seeds, e.name);
            // CSV etiketi rejimi tasi: ayni kontrolcu farkli rejimlerde
            // farkli sonuc verir, tek isimde kaybolurlardi.
            for (RunResult& r : rs) {
                r.controller = std::string(e.name) + "@" + lp.name;
                tumKosular.push_back(r);
            }
            runs[e.name] = std::move(rs);
            std::cout << "tamam\n";
        }

        printSummary(runs);
        printComparisons(runs, "sabit-zamanli");
        printCensoringWarning(runs);
    }

    if (opt.csv && !opt.outDir.empty()) {
        writeCsv(opt.outDir, tumKosular);
    }
    return 0;
}
