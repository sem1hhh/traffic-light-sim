// render/src/render_canvas.cpp — simulasyon cizimi.
//
// Cizim API'si: global gDrawLine/gDrawRectangle + setColor (core/gRenderer.h).
// FPS: root->getElapsedTime() (deltaTime, sn) — simulasyon adimlarini kare
// hizindan bagimsiz ilerletmek icin bunu akumulatore veriyoruz.

#include "render_canvas.h"

#include "tsc/intersection.hpp"

#include <iostream>

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
    : gBaseCanvas(root), scene(1, 1), scene3d(1280, 720) {
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

void RenderCanvas::drawScene3D() {
    scene3d.begin();
    const Network& net = scene.network();
    const World& w = scene.world();

    // Yollar + kavsonglar: her serit hucresi bir 3B karo.
    for (const LaneId lid : net.allLanes()) {
        const Lane& lane = net.lane(lid);
        for (const Vec2i& c : lane.cells) {
            scene3d.drawRoadTile(c);
        }
    }
    for (const JunctionId j : net.signalJunctions()) {
        scene3d.drawJunctionTile(net.node(j).cell);
    }

    // Araclar: konum + yon.
    for (const Vehicle& v : w.vehicles()) {
        if (!v.valid()) continue;
        const Vec2d p = scene.lanePointAt(v.currentLane, v.position);
        const double heading = scene.laneHeadingAt(v.currentLane, v.position);
        scene3d.drawCar(static_cast<float>(p.x), static_cast<float>(p.y), heading);
    }

    scene3d.end();
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
