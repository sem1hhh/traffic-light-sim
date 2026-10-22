// render/tools/measure_offset.cpp — SERIT OFSETI OLCUMU (GUI'siz kanit araci).
//
// NEDEN: "Dikey kollardaki araclarin yol merkezine uzakligi omurgadakinden
// farkli gorunuyor" gozlemi (2026-10-22) tahminle degil OLCUMLE dogrulanmali
// (AGENTS.md §8.10: >=0 gibi her kosulda dogru bir kontrol olcum yapmaz).
//
// Iki ayri buyukluk olculur:
//   1) SERIT MERKEZINE dik uzaklik  -> cizim ofsetinin kendisi. Beklenen
//      kLaneOffsetM = 1.75 m (coords.h). Bu deger her yonde AYNI olmali.
//   2) CIZILEN KARO EKSENINE dik uzaklik -> yolun ekranda gorunen ekseni.
//      Karo dikey kolda dondurulmemisse bu deger 1.75'ten AYRISIR.
//
// Iki olcumun ayrismasi karo yonunun hatasini kanitlar; ayrismamasi da
// "olcum bos degil" kanitidir (--assume-unrotated ile eski davranis olculur).
//
// Kullanim:
//   tsc_measure_offset [adim] [tohum]
//   tsc_measure_offset --assume-unrotated [adim] [tohum]
//
// Ornek:
//   ./render/build/tsc_measure_offset 600 1
//   ./render/build/tsc_measure_offset --assume-unrotated 600 1

#include "coords.h"
#include "sim_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace tsc;
using namespace tsc::render;

namespace {

struct Stats {
    int samples = 0;
    double latMin = 0.0, latMax = 0.0, latSum = 0.0;
    double tileMin = 0.0, tileMax = 0.0, tileSum = 0.0;

    void add(const SimScene::VehicleOffset& o) {
        if (samples == 0) {
            latMin = latMax = o.lateralM;
            tileMin = tileMax = o.tileM;
        } else {
            latMin = std::min(latMin, o.lateralM);
            latMax = std::max(latMax, o.lateralM);
            tileMin = std::min(tileMin, o.tileM);
            tileMax = std::max(tileMax, o.tileM);
        }
        latSum += o.lateralM;
        tileSum += o.tileM;
        ++samples;
    }
};

void printStats(const char* ad, const Stats& s) {
    if (s.samples == 0) {
        std::printf("[olcum] %-5s ornek YOK\n", ad);
        return;
    }
    const bool ok = std::abs(s.latMin - kLaneOffsetM) < 0.05 &&
                    std::abs(s.latMax - kLaneOffsetM) < 0.05 &&
                    std::abs(s.tileMin - kLaneOffsetM) < 0.05 &&
                    std::abs(s.tileMax - kLaneOffsetM) < 0.05;
    std::printf("[olcum] %-5s n=%-5d serit merkezi %.3f..%.3f m (ort %.3f) | "
                "karo ekseni %.3f..%.3f m (ort %.3f) | beklenen %.2f -> %s\n",
                ad, s.samples,
                s.latMin, s.latMax, s.latSum / s.samples,
                s.tileMin, s.tileMax, s.tileSum / s.samples,
                kLaneOffsetM, ok ? "OK" : "FARKLI");
}

}  // namespace

int main(int argc, char** argv) {
    bool assumeUnrotated = false;
    int steps = 600;
    std::uint64_t seed = 1;

    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assume-unrotated") == 0) {
            assumeUnrotated = true;
        } else if (positional == 0) {
            steps = std::atoi(argv[i]);
            ++positional;
        } else if (positional == 1) {
            seed = static_cast<std::uint64_t>(std::strtoull(argv[i], nullptr, 10));
            ++positional;
        }
    }

    SimScene scene(seed, 1);
    std::printf("[olcum] %d adim, tohum %llu, karo ekseni: %s\n", steps,
                static_cast<unsigned long long>(seed),
                assumeUnrotated ? "DONDURULMEMIS (eski davranis)"
                                : "SERIT GEOMETRISINDEN (duzeltilmis)");

    Stats horizontal;
    Stats vertical;
    for (int i = 0; i < steps; ++i) {
        for (const SimScene::VehicleOffset& o :
             scene.measureVehicleOffsets(assumeUnrotated)) {
            (o.horizontal ? horizontal : vertical).add(o);
        }
        scene.advance(1.0);
    }

    printStats("yatay", horizontal);
    printStats("dikey", vertical);

    // KARO EKSENI DENETIMI: dikey kolda karo gercekten donduruluyor mu?
    int nsCells = 0;
    int ewCells = 0;
    int bothCells = 0;
    const Grid grid = buildRenderGrid();
    for (int y = 0; y < grid.size().height; ++y) {
        for (int x = 0; x < grid.size().width; ++x) {
            const Vec2i c{x, y};
            if (grid.at(c) == CellType::Empty) continue;
            switch (scene.roadAxisAt(c)) {
                case RoadAxis::NorthSouth: ++nsCells; break;
                case RoadAxis::EastWest: ++ewCells; break;
                case RoadAxis::Both: ++bothCells; break;
            }
        }
    }
    std::printf("[karo] hucre ekseni: dogu-bati %d, kuzey-guney %d, kavsak(iki yon) %d\n",
                ewCells, nsCells, bothCells);

    return (horizontal.samples > 0 && vertical.samples > 0) ? 0 : 2;
}
