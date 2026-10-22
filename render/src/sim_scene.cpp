// render/src/sim_scene.cpp — SimScene implementasyonu.
//
// Burada hicbir is mantigi YENIDEN YAZILMAZ: World adimlandirir, SignalDriver
// karar verir, sabit zamanli kontrolcu faz degistirir — hepsi sim-core ve
// controllers. Bu dosya yalnizca onlari KURAR ve AKUMULATORLE ilerletir.

#include "sim_scene.h"

#include "coords.h"   // applyLaneOffset / kCellToMeters (olcumde cizimle AYNI)

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
      world_(net_, config_, seed),
      driver_(world_, defaultFactory()),
      seed_(seed) {
    buildRoadAxes();
    setStepsPerSecond(stepsPerSecond);
}

SignalDriver::ControllerFactory SimScene::defaultFactory() {
    // Simdilik sabit zamanli kontrolcu. Q-learning sonraki adimda
    // politika dosyasindan yuklenebilir.
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<FixedTimeController>();
    };
}

void SimScene::reset() {
    // Home tusu: ayni tohumla BASA don. Uye sirasi onemli — world_ eski
    // net_'i, driver_ eski world_'u gostermemeli; taze nesneler kurulur.
    // World/Network/SignalDriver'in hibirinde kullanici tanimli dtor/copy
    // yok (olcum): yerinde atanabilirler. sim-core'a dokunulmaz.
    net_ = buildNetwork(buildRenderGrid());
    world_ = World(net_, config_, seed_);
    driver_ = SignalDriver(world_, defaultFactory());
    accumulator_ = 0.0;
    paused_ = false;
    buildRoadAxes();
}

void SimScene::setDemandProfile(DemandProfile profile) {
    // IG-3: profil yalnizca varis ZAMANLARINI degistirir (arac sayisi sabit).
    // Profil degisimi dunya kurulumunu gerektirir; ayni tohumla basa donulur
    // (determinizm: ayni tohum + ayni profil = ayni kosu). Duraklatma durumu
    // KORUNUR: profil degistirmek kullaniciyi istemeden kosuya sokmaz.
    const bool wasPaused = paused_;
    config_.profile = profile;
    reset();
    paused_ = wasPaused;
}

void SimScene::buildRoadAxes() {
    // Karo eksenini TAHMIN etme: her seridin cizim yolunu (from -> cells ->
    // to) yuru, her SEGMENTIN eksenini o segmentin dokundugu iki hucreye yaz.
    // Ayni hucre iki farkli eksenden segment goruyorsa -> Both (kavsak).
    roadAxes_.clear();
    const auto mark = [this](const Vec2i& c, RoadAxis axis) {
        const auto key = std::make_pair(c.x, c.y);
        const auto it = roadAxes_.find(key);
        if (it == roadAxes_.end()) {
            roadAxes_.emplace(key, axis);
        } else if (it->second != axis) {
            it->second = RoadAxis::Both;
        }
    };
    const auto cellOf = [](const Vec2d& p) {
        return Vec2i{static_cast<int>(std::floor(p.x)),
                     static_cast<int>(std::floor(p.y))};
    };

    for (const LaneId lid : net_.allLanes()) {
        const std::vector<Vec2d> path = laneCenterPath(lid);
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            const double dx = path[i + 1].x - path[i].x;
            const double dz = path[i + 1].y - path[i].y;
            if (dx == 0.0 && dz == 0.0) continue;
            const RoadAxis axis = (std::abs(dx) >= std::abs(dz))
                                      ? RoadAxis::EastWest
                                      : RoadAxis::NorthSouth;
            mark(cellOf(path[i]), axis);
            mark(cellOf(path[i + 1]), axis);
        }
    }
}

RoadAxis SimScene::roadAxisAt(const Vec2i& cell) const {
    const auto it = roadAxes_.find(std::make_pair(cell.x, cell.y));
    if (it == roadAxes_.end()) return RoadAxis::EastWest;  // yol olmayan hucre
    return it->second;
}

std::vector<SimScene::VehicleOffset> SimScene::measureVehicleOffsets(
    bool forceUnrotatedTiles) const {
    std::vector<VehicleOffset> out;
    for (const Vehicle& v : world_.vehicles()) {
        if (!v.valid()) continue;
        const Vec2d p = lanePointAt(v.currentLane, v.position);
        const Vec2d d = laneDirAt(v.currentLane, v.position);
        const double len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 1e-9) continue;  // geometri yok: olcum yapilamaz
        const double fx = d.x / len;
        const double fz = d.y / len;
        // coords.h headingRight(h) = (cos h, -sin h), h = atan2(dirX, dirZ).
        const double rx = fz;
        const double rz = -fx;
        const double h = std::atan2(d.x, d.y);
        double cx = 0.0, cz = 0.0;
        applyLaneOffset(p.x, p.y, h, cx, cz);

        VehicleOffset o;
        o.lane = v.currentLane;
        o.horizontal = std::abs(fx) > std::abs(fz);
        o.lateralM = ((cx - p.x) * rx + (cz - p.y) * rz) * kCellToMeters;

        const Vec2i cell{static_cast<int>(std::floor(p.x)),
                         static_cast<int>(std::floor(p.y))};
        const RoadAxis axis = roadAxisAt(cell);
        if (axis == RoadAxis::Both) continue;  // kavsak hucresi
        const RoadAxis effective = forceUnrotatedTiles ? RoadAxis::EastWest : axis;
        const double cellcx = static_cast<double>(cell.x) + 0.5;
        const double cellcz = static_cast<double>(cell.y) + 0.5;
        o.tileM = (effective == RoadAxis::EastWest ? std::abs(cz - cellcz)
                                                   : std::abs(cx - cellcx)) *
                  kCellToMeters;
        out.push_back(o);
    }
    return out;
}


void SimScene::setStepsPerSecond(int n) {
    // 0 veya negatif "dur" demektir; 1..kMaxStepsPerFrame araligina sikistir.
    stepsPerSecond_ = std::clamp(n, 0, kMaxStepsPerFrame);
}

void SimScene::advance(double dtSeconds) {
    if (stopped() || stepsPerSecond_ <= 0 || paused_) {
        return;  // P tusuyla duraklatildiginda adim atilmaz (IG-2)
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

    // CIZIM YOLU: from-dugum -> cells -> to-dugum.
    //
    // ÖLÇÜM (2026-10-21, teşhis): sim-core'da `lane.cells` iki dugumun hucresini
    // HARIC tutar ve `length = cells.size() * kCellLength` yalnizca bu ara
    // hucreleri olcer. Sonuc:
    //   - 0 hucreli serit (komsu dugumler): position=0, cells bos -> eski kod
    //     (0,0) donduruyordu => arac IZGARA KOSESONDE (boslukta).
    //   - 1 hucreli serit: lastIdx=0, her position -> hep cells[0] => arac
    //     YERINDE SAYIYORDU (pos artiyor ama world sabit).
    // Duzeltme: cizim yoluna dugum hucreleri de eklenir; position [0, length]
    // bu TAM yol (cells.size()+1 segment) boyunca interpolasyonlanir. Boylece
    // arac hic bosluga dusmez ve serit boyunca GERCEKTEN kayar.
    const Vec2i fromCell = net_.node(lane.from).cell;
    const Vec2i toCell = net_.node(lane.to).cell;
    const std::size_t n = cells.size();

    if (n == 0) {
        // Komsu dugumler arasi bos serit: position hep 0 (length=0). Araci
        // from hucresinde goster (bosluga DEGIL).
        return Vec2d{static_cast<double>(fromCell.x) + 0.5,
                     static_cast<double>(fromCell.y) + 0.5};
    }

    // position -> [0,1] -> tam yol boyunca (n+1 segment) interpolasyon.
    const double length = lane.length;  // n * kCellLength
    const double f = (length > 0.0) ? std::clamp(distMeters / length, 0.0, 1.0) : 0.0;
    const double segPos = f * static_cast<double>(n + 1);
    int seg = static_cast<int>(segPos);
    if (seg > static_cast<int>(n)) {
        seg = static_cast<int>(n);
    }
    const double frac = segPos - static_cast<double>(seg);

    // Nokta listesi indeksleme: 0=from, 1..n=cells[0..n-1], n+1=to.
    const auto pointAt = [&](int idx) -> Vec2i {
        if (idx <= 0) return fromCell;
        if (idx <= static_cast<int>(n)) return cells[static_cast<std::size_t>(idx - 1)];
        return toCell;
    };

    const Vec2i p0 = pointAt(seg);
    const Vec2i p1 = pointAt(seg + 1);
    const double x = (static_cast<double>(p0.x) + 0.5) * (1.0 - frac) +
                     (static_cast<double>(p1.x) + 0.5) * frac;
    const double y = (static_cast<double>(p0.y) + 0.5) * (1.0 - frac) +
                     (static_cast<double>(p1.y) + 0.5) * frac;
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

std::vector<Vec2d> SimScene::laneCenterPath(LaneId laneId) const {
    const Lane& lane = net_.lane(laneId);
    const Vec2i fromCell = net_.node(lane.from).cell;
    const Vec2i toCell = net_.node(lane.to).cell;

    auto push = [](std::vector<Vec2d>& out, const Vec2i& c) {
        out.push_back(Vec2d{static_cast<double>(c.x) + 0.5,
                            static_cast<double>(c.y) + 0.5});
    };

    std::vector<Vec2d> path;
    push(path, fromCell);
    for (const Vec2i& c : lane.cells) {
        push(path, c);
    }
    push(path, toCell);
    return path;
}

Vec2d SimScene::laneDirAt(LaneId laneId, double distMeters) const {
    // GIDIS YON VEKTORU — kaynak laneCenterPath GEOMETRISI (konum türevi degil).
    // Neden: 0 hucreli kolda (dikey kollar) konum türevi sifirdi; ayni iki nokta
    // donuyordu. Geometri (from -> cells -> to) her seritte dogru yonu verir.
    // Segment haritalamasi lanePointAt ile AYNI (position/[0,1] -> n+1 segment).
    const std::vector<Vec2d> path = laneCenterPath(laneId);
    const std::size_t nseg = (path.size() >= 2) ? path.size() - 1 : 0;
    if (nseg == 0) return Vec2d{0.0, 0.0};

    const Lane& lane = net_.lane(laneId);
    const double length = lane.length;
    const double f = (length > 0.0) ? std::clamp(distMeters / length, 0.0, 1.0) : 0.0;
    std::size_t seg = static_cast<std::size_t>(f * static_cast<double>(nseg));
    if (seg >= nseg) seg = nseg - 1;

    const Vec2d& a = path[seg];
    const Vec2d& b = path[seg + 1];
    const double dx = b.x - a.x;
    const double dz = b.y - a.y;
    if (dx == 0.0 && dz == 0.0) {
        // Cok kisa segment: ilk gecerli segmenti bul (geometri sifir olmaz,
        // yan yana iki farkli hucre var; yine de guvenlik).
        for (std::size_t i = 0; i < nseg; ++i) {
            const double ex = path[i + 1].x - path[i].x;
            const double ez = path[i + 1].y - path[i].y;
            if (ex != 0.0 || ez != 0.0) return Vec2d{ex, ez};
        }
        return Vec2d{0.0, 0.0};
    }
    return Vec2d{dx, dz};
}

}  // namespace render
}  // namespace tsc
