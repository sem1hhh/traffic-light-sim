// render/src/render_canvas.cpp — simulasyon cizimi.
//
// Cizim API'si: global gDrawLine/gDrawRectangle + setColor (core/gRenderer.h).
// FPS: root->getElapsedTime() (deltaTime, sn) — simulasyon adimlarini kare
// hizindan bagimsiz ilerletmek icin bunu akumulatore veriyoruz.

#include "render_canvas.h"

#include "tsc/intersection.hpp"

#include <GLFW/glfw3.h>   // GLFW_KEY_HOME (Home tusu; engine pkg-config ile erisir)

#include <cmath>
#include <iostream>
#include <set>
#include <utility>

namespace tsc {
namespace render {

namespace {
// Izgara cizimi: 9x7 hucre, hucre basina piksel. 1280x720 pencereye sigar.
constexpr int kCellPx = 60;
constexpr int kGridCols = 9;
constexpr int kGridRows = 7;
constexpr int kFpsLogInterval = 120;  // kare

// Izgarayi pencerenin ortasina hizala.
constexpr int kOriginX = (1280 - kGridCols * kCellPx) / 2;
constexpr int kOriginY = (720 - kGridRows * kCellPx) / 2;

// Hucre koordinatindan (cell-center, 0.5 ekli) piksele.
float px(double cx) { return static_cast<float>(kOriginX) + static_cast<float>(cx * kCellPx); }
float py(double cy) { return static_cast<float>(kOriginY) + static_cast<float>(cy * kCellPx); }
}  // namespace

RenderCanvas::RenderCanvas(RenderApp* root)
    : gBaseCanvas(root), scene(1, 10), scene3d(1280, 720) {
    this->root = root;
}

RenderCanvas::~RenderCanvas() {
}

void RenderCanvas::setup() {
    std::cout << "[render] canvas setup: " << getWidth() << "x" << getHeight()
              << "  izgara " << kGridCols << "x" << kGridRows
              << "  sim hizi: " << scene.stepsPerSecond() << " adim/sn" << std::endl;
    std::cout << "[render] 3B modeller: " << (scene3d.modelsLoaded() ? "YUKLENDI" : "YOK")
              << " (araba + yol)" << std::endl;
}

void RenderCanvas::update() {
    // Simulasyonu kare hizindan BAGIMSIZ ilerlet: gecen gercek sureyi ver.
    scene.advance(static_cast<double>(root->getElapsedTime()));

    // FPS logu (trafik akarken olculur).
    fpssum += static_cast<double>(root->getFramerate());
    ++framecounter;
    if (framecounter >= kFpsLogInterval) {
        std::cout << "[render] FPS ortalamasi (" << framecounter << " kare): "
                  << (fpssum / static_cast<double>(framecounter))
                  << "  sim-sn: " << scene.world().time() << std::endl;
        framecounter = 0;
        fpssum = 0.0;
    }
}

void RenderCanvas::draw() {
    clearColor(18, 24, 22);
    if (scene3d.modelsLoaded()) {
        drawScene3D();
        return;
    }
    //3B model yoksa2D yedek.
    drawRoads();
    drawJunctions();
    drawVehicles();
}

void RenderCanvas::keyPressed(int key) {
    // D: hata ayiklama katmanini ac/kapa.
    if (key == 'd' || key == 'D') {
        debuglayer = !debuglayer;
        std::cout << "[render] hata ayiklama katmani: "
                  << (debuglayer ? "ACIK" : "KAPALI") << std::endl;
        return;
    }
    // P: duraklat / devam et (IG-2). Sim duraklarken draw calismaya devam
    // eder (ekran donmaz), yalnizca simulasyon adimi atilmaz.
    if (key == 'p' || key == 'P') {
        scene.setPaused(!scene.paused());
        std::cout << "[render] simulasyon: "
                  << (scene.paused() ? "DURAKLADI (P)" : "DEVAM (P)")
                  << "  sim-sn: " << scene.world().time() << std::endl;
        return;
    }
    // HOME: simulasyonu ayni tohumla basa dondur — bitince araclari yeniden
    // gormek ve yon dogrulamasini tekrarlamak icin.
    if (key == GLFW_KEY_HOME) {
        scene.reset();
        std::cout << "[render] simulasyon YENIDEN BASLATILDI (Home)"
                  << std::endl;
        return;
    }
}

void RenderCanvas::drawScene3D() {
    scene3d.begin();
    const Network& net = scene.network();
    const World& w = scene.world();

    // YOL KAROLARI: artik HER seridin TAM yolundan (from-dugum -> cells ->
    // to-dugum) doseliyor — ayni kaynak debug mavi cizgisinin kaynagi. Boylece
    // "mavi cizgi var, altinda karo yok" kalmasin (0 hucreli kollar dahil).
    // Ayni hucreyi iki serit paylasir; tekrar dosememek icin set.
    std::set<std::pair<int, int>> tiled;
    for (const LaneId lid : net.allLanes()) {
        const std::vector<Vec2d> path = scene.laneCenterPath(lid);
        for (const Vec2d& p : path) {
            // laneCenterPath x+0.5 (hucre merkezi) dondurur; floor ile hucre indeksi.
            const int cx = static_cast<int>(std::floor(p.x));
            const int cy = static_cast<int>(std::floor(p.y));
            if (tiled.insert({cx, cy}).second) {
                scene3d.drawRoadTile(Vec2i{cx, cy});
            }
        }
    }
    // Kavsong karolari (kavsong hucresi): sinyalli dugumler.
    for (const JunctionId j : net.signalJunctions()) {
        scene3d.drawJunctionTile(net.node(j).cell);
    }

    // Araclar: konum + yon. YON artik serit GEOMETRISINDEN (laneDirAt);
    // konum türevi 0 hucreli kolda sifir donuyordu.
    for (const Vehicle& v : w.vehicles()) {
        if (!v.valid()) continue;
        const Vec2d p = scene.lanePointAt(v.currentLane, v.position);
        const Vec2d dir = scene.laneDirAt(v.currentLane, v.position);
        const double heading = (dir.x == 0.0 && dir.y == 0.0)
                                   ? 0.0
                                   : std::atan2(dir.x, dir.y);
        scene3d.drawCar(static_cast<float>(p.x), static_cast<float>(p.y), heading);
    }

    // Hata ayiklama katmani (D tusu): serit merkez cizgileri + arac kutulari.
    if (debuglayer) {
        drawDebugLayer();
    }

    // YON OLCEMI (her karede calisir, D bagimsiz): omurgadaki (yatay) ve
    // dikey koldaki bir arac yaw'i — aralarinda 90 derece fark OLMALI.
    // Kaynak: laneDirAt (serit GEOMETRISI). Konum türevi 0 hucreli kolda
    // sifirdi; o yuzden dikey araclar eleniyordu ve log hic basilmadi (0 satir).
    // Duraklatildiginda (P) degerler degismez; ayni satiri tekrar basmamak icin
    // sayaci donduruyoruz.
    if (scene.paused()) {
        yonlogcounter_ = 0;
    } else if (++yonlogcounter_ >= 120) {
        yonlogcounter_ = 0;
        const Vehicle* yatay = nullptr;
        const Vehicle* dikey = nullptr;
        const auto dirOf = [&](const Vehicle& v, double& dirX, double& dirZ) {
            const Vec2d d = scene.laneDirAt(v.currentLane, v.position);
            dirX = d.x;
            dirZ = d.y;
        };
        for (const Vehicle& v : w.vehicles()) {
            if (!v.valid()) continue;
            double dx = 0.0, dz = 0.0;
            dirOf(v, dx, dz);
            if (dx == 0.0 && dz == 0.0) continue;
            if (std::abs(dz) < std::abs(dx) && !yatay) yatay = &v;
            if (std::abs(dx) < std::abs(dz) && !dikey) dikey = &v;
        }
        if (yatay && dikey) {
            double hx = 0.0, hz = 0.0, vx = 0.0, vz = 0.0;
            dirOf(*yatay, hx, hz);
            dirOf(*dikey, vx, vz);
            const double omurgaDeg = carYawDeg(hx, hz);
            const double kolDeg = carYawDeg(vx, vz);
            double fark = std::abs(omurgaDeg - kolDeg);
            if (fark > 180.0) fark = 360.0 - fark;
            std::cout << "[yon] omurga yaw=" << omurgaDeg
                      << "  kol yaw=" << kolDeg
                      << "  |fark|=" << fark << " derece"
                      << (std::abs(fark - 90.0) < 1.0 ? "  [OK 90]" : "  [!! 90 DEGIL]")
                      << std::endl;
        }
    }

    scene3d.end();
}

void RenderCanvas::drawDebugLayer() {
    const Network& net = scene.network();
    const World& w = scene.world();

    // 1) Serit merkez cizgileri (ofsetsiz, yolun kendisi) — ince renkli cizgi.
    setColor(90, 200, 255);
    for (const LaneId lid : net.allLanes()) {
        const std::vector<Vec2d> path = scene.laneCenterPath(lid);
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            scene3d.drawDebugLine(path[i].x, path[i].y,
                                  path[i + 1].x, path[i + 1].y);
        }
    }

    // 2) Arac kutulari (sim konumunda, sag-ofset uygulanmis) — kirmizi.
    setColor(255, 80, 80);
    for (const Vehicle& v : w.vehicles()) {
        if (!v.valid()) continue;
        const Vec2d p = scene.lanePointAt(v.currentLane, v.position);
        const Vec2d dir = scene.laneDirAt(v.currentLane, v.position);
        const double heading = (dir.x == 0.0 && dir.y == 0.0)
                                   ? 0.0
                                   : std::atan2(dir.x, dir.y);
        scene3d.drawVehicleBox(p.x, p.y, heading);
    }

    // 3) Gidis yonu oklari (YESIL): serit GEOMETRISI yonu (laneDirAt). Ayni
    //    yon vektoru drawCar'da carYawDeg'e giriyor — ok ile modelin burnu
    //    HIZALI olmali (ok = arac burnunun bakmasi gereken yon).
    setColor(60, 255, 120);
    for (const Vehicle& v : w.vehicles()) {
        if (!v.valid()) continue;
        const Vec2d p = scene.lanePointAt(v.currentLane, v.position);
        const Vec2d dir = scene.laneDirAt(v.currentLane, v.position);
        if (dir.x == 0.0 && dir.y == 0.0) continue;
        scene3d.drawDebugArrow(p.x, p.y, dir.x, dir.y);
    }
}

void RenderCanvas::drawRoads() {
    const Network& net = scene.network();
    setColor(52, 60, 56);
    for (const LaneId lid : net.allLanes()) {
        const Lane& lane = net.lane(lid);
        for (const Vec2i& c : lane.cells) {
            // Her serit hucresini kare olarak doldur.
            gDrawRectangle(px(static_cast<double>(c.x)) - kCellPx / 2.0f,
                           py(static_cast<double>(c.y)) - kCellPx / 2.0f,
                           static_cast<float>(kCellPx), static_cast<float>(kCellPx),
                           true);
        }
    }
}

void RenderCanvas::drawJunctions() {
    const Network& net = scene.network();
    const World& w = scene.world();

    for (const JunctionId j : net.signalJunctions()) {
        const Node& node = net.node(j);
        const Intersection& inter = w.intersection(j);

        // Kavsak hucresi (daha acik renk).
        setColor(70, 78, 74);
        gDrawRectangle(px(static_cast<double>(node.cell.x)) - kCellPx / 2.0f,
                       py(static_cast<double>(node.cell.y)) - kCellPx / 2.0f,
                       static_cast<float>(kCellPx), static_cast<float>(kCellPx), true);

        // Isik gostergeleri: her kol icin kucuk renkli kare (fazla senkron).
        // Kol basina konum: kolun disina dogru kucuk bir kare.
        const float r = static_cast<float>(kCellPx) * 0.18f;
        for (const Dir arm : inter.arms()) {
            // Kolun merkez yonu: hucre disinda, kol yonunde.
            double ox = 0.0, oy = 0.0;
            switch (arm) {
                case Dir::North: oy = -0.5; break;
                case Dir::South: oy = 0.5; break;
                case Dir::West:  ox = -0.5; break;
                case Dir::East:  ox = 0.5; break;
            }
            const float cxp = px(static_cast<double>(node.cell.x) + ox);
            const float cyp = py(static_cast<double>(node.cell.y) + oy);

            // Renk: yesil / sari / kirmizi (faz durumuyla senkron).
            if (inter.hasGreen(arm)) {
                setColor(60, 220, 90);       // yesil
            } else if (inter.state() == SignalState::Yellow &&
                       phaseOfDirection(arm) == inter.phase()) {
                setColor(240, 210, 40);      // sari (bu faz sarida)
            } else {
                setColor(220, 60, 50);       // kirmizi
            }
            gDrawRectangle(cxp - r, cyp - r, r * 2.0f, r * 2.0f, true);
        }
    }
}

void RenderCanvas::drawVehicles() {
    const World& w = scene.world();
    setColor(235, 240, 245);
    for (const Vehicle& v : w.vehicles()) {
        if (!v.valid()) {
            continue;
        }
        const Vec2d p = scene.lanePointAt(v.currentLane, v.position);
        const float r = static_cast<float>(kCellPx) * 0.22f;
        gDrawRectangle(px(p.x) - r, py(p.y) - r, r * 2.0f, r * 2.0f, true);
    }
}

}  // namespace render
}  // namespace tsc
