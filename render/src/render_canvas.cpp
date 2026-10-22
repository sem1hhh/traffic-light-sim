// render/src/render_canvas.cpp — simulasyon cizimi.
//
// Cizim API'si: global gDrawLine/gDrawRectangle + setColor (core/gRenderer.h).
// FPS: root->getElapsedTime() (deltaTime, sn) — simulasyon adimlarini kare
// hizindan bagimsiz ilerletmek icin bunu akumulatore veriyoruz.

#include "render_canvas.h"

#include "tsc/intersection.hpp"

#include <GLFW/glfw3.h>   // GLFW_KEY_HOME / scroll callback (motor pkg-config ile erisir)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace tsc {
namespace render {

namespace {
// --- Trackpad/fare tekerlegi koprusu ---------------------------------------
// SORUN (olculdu): motorun gGLFWWindow scroll callback'i `static_cast<int>`
// yapiyor; macOS trackpad'i surekli ve KESIRLI yoffset uretir (orn. 0.12),
// yani butun olaylar 0'a yuvarlanip canvas'a hicbir zoom ulasmiyordu.
// COZUM: GLFW callback'ini dogrudan kurup `double` degeri kullaniyoruz;
// motorun onceki callback'i de cagrilir (GUI yolu bozulmasin).
RenderCanvas* g_scrollcanvas = nullptr;
GLFWscrollfun g_prevscroll = nullptr;

void scrollThunk(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_prevscroll != nullptr) {
        g_prevscroll(window, xoffset, yoffset);
    }
    if (g_scrollcanvas != nullptr) {
        g_scrollcanvas->onScrollDelta(yoffset);
    }
}

// Zoom adimi: tekerlek tek "tik"i ~%10; trackpad kesirli oldugu icin
// pow() ile yumusak olceklenir.
constexpr double kZoomPerScroll = 1.10;
constexpr float kKeyZoomFactor = 1.12f;
// Kaydirma adimi (hucre). Ok tusu basina.
constexpr float kPanStep = 0.8f;
}  // namespace

void OffsetStats::add(double latM, double tileM) {
    if (samples == 0) {
        latMin = latMax = latM;
        tileMin = tileMax = tileM;
    } else {
        latMin = std::min(latMin, latM);
        latMax = std::max(latMax, latM);
        tileMin = std::min(tileMin, tileM);
        tileMax = std::max(tileMax, tileM);
    }
    latSum += latM;
    tileSum += tileM;
    ++samples;
}

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
    // Scroll koprusu bu canvas'i isaret ediyorsa temizle (dangling pointer).
    if (g_scrollcanvas == this) {
        g_scrollcanvas = nullptr;
    }
}

void RenderCanvas::setup() {
    std::cout << "[render] canvas setup: " << getWidth() << "x" << getHeight()
              << "  izgara " << kGridCols << "x" << kGridRows
              << "  sim hizi: " << scene.stepsPerSecond() << " adim/sn"
              << "  senaryo: " << scene.worldConfig().vehicleCount << " arac / "
              << scene.worldConfig().spawnInterval << " s aralik" << std::endl;
    std::cout << "[render] 3B modeller: " << (scene3d.modelsLoaded() ? "YUKLENDI" : "YOK")
              << " (araba + yol)" << std::endl;
    std::cout << "[render] kontrolcu: " << scene.controllerName() << std::endl;

    // HUD (IG-3): metin cizimi icin alfa karisimi gerekir. Fontlar
    // assets/fonts/ altindan yuklenir (motor ayni yolu GUI icin de kullanir).
    enableAlphaBlending();
    const bool boldok = hudfont_.loadFont("FreeSansBold.ttf", 18);
    const bool smallok = hudfontsmall_.loadFont("FreeSans.ttf", 14);
    hudok_ = boldok && smallok;
    std::cout << "[render] HUD fontu: " << (hudok_ ? "YUKLENDI" : "YOK (HUD cizilmeyecek)")
              << std::endl;

    // Karo eksenleri ve ofset olcumu hakkinda baslangic kaydi (kanit).
    std::cout << "[render] karo ekseni: dikey kollar 90 derece dondurulur" << std::endl;

    // Trackpad/fare tekerlegi: motorun int'e kirpan yolu yerine dogrudan
    // GLFW callback'i (kesirli yoffset korunur). Onceki callback saklanip
    // yine cagrilir; motorun kendi GUI yolu bozulmaz.
    if (GLFWwindow* window = glfwGetCurrentContext()) {
        g_scrollcanvas = this;
        g_prevscroll = glfwSetScrollCallback(window, &scrollThunk);
        std::cout << "[render] scroll koprusu kuruldu (trackpad zoom)" << std::endl;
    } else {
        std::cout << "[render] UYARI: GLFW penceresi yok; trackpad zoom kapali"
                  << std::endl;
    }

    // Panelin GORSEL dogrulamasi icin: kosunun bitmesini beklemeden ac.
    // (Kullanici istegi disinda bir sey degistirmez; yalnizca env degiskeni
    // set edilmisse calisir. Ornek: TSC_SHOW_RESULTS=1 ./build/tsc_render)
    if (std::getenv("TSC_SHOW_RESULTS") != nullptr) {
        showresults_ = true;
        startEvaluation();
        std::cout << "[sonuc] TSC_SHOW_RESULTS: panel acik baslatildi"
                  << std::endl;
    }
}

void RenderCanvas::update() {
    // Simulasyonu kare hizindan BAGIMSIZ ilerlet: gecen gercek sureyi ver.
    scene.advance(static_cast<double>(root->getElapsedTime()));

    // IG-7: kosu bitti -> sonuc panelini ac ve ayni tohumla uc kontrolcuyu
    // (sabit / kural / Q) GUI'siz kosup yan yana koy. Yalnizca BIR kez.
    const World& w = scene.world();
    if (!evalautoopened_ && (w.finished() || w.timedOut())) {
        evalautoopened_ = true;
        showresults_ = true;
        startEvaluation();
    }

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
    } else {
        //3B model yoksa2D yedek.
        drawRoads();
        drawJunctions();
        drawVehicles();
    }
    drawHud();  // IG-3: saat ekranda; 2B katman, sahnenin ustune
    if (showresults_) {
        drawResultPanel();  // IG-7: bitince acilan sonuc sayfasi
    }
}

void RenderCanvas::keyPressed(int key) {
    // 1..8: simulasyon hizi (IG-2). Her tus kac sim-sn/sn oldugunu belirler.
    // 1 = gercek zamanli, 8 = 8x. Kare hizindan bagimsiz akumulator kullanilir.
    if (key >= '1' && key <= '8') {
        const int hiz = key - '0';
        scene.setStepsPerSecond(hiz);
        std::cout << "[render] sim hizi: " << scene.stepsPerSecond() << "x ("
                  << scene.stepsPerSecond() << " adim/sn)" << std::endl;
        return;
    }
    // T: talep profili (IG-3): sabit -> sabah zirvesi -> aksam zirvesi.
    // Profil varis ZAMANLARINI degistirir; arac sayisi sabittir. Dunya ayni
    // tohumla yeniden kurulur (determinizm korunur).
    if (key == 't' || key == 'T') {
        DemandProfile sonraki = DemandProfile::Uniform;
        switch (scene.demandProfile()) {
            case DemandProfile::Uniform:     sonraki = DemandProfile::MorningPeak; break;
            case DemandProfile::MorningPeak: sonraki = DemandProfile::EveningPeak; break;
            case DemandProfile::EveningPeak: sonraki = DemandProfile::Uniform; break;
        }
        scene.setDemandProfile(sonraki);
        offhorizontal_ = OffsetStats{};
        offvertical_ = OffsetStats{};
        invalidateResults();
        std::cout << "[render] talep profili: " << toString(sonraki)
                  << "  (arac sayisi sabit, varis zamanlari degisir)" << std::endl;
        return;
    }
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
    // M: ofset olcumunu hemen bas (beklemeden). Periyodik log YOK; olcum
    // yalnizca bu tusla uretilir (kullanici istegi).
    if (key == 'm' || key == 'M') {
        measureLaneOffset();
        return;
    }
    // C: kontrolcuyu degistir (sabit -> kural -> Q-learning) ve AYNI tohumla
    // bastan baslat. Boylece uc kosu eslestirilmis olur (IG-7 paneli).
    if (key == 'c' || key == 'C') {
        scene.cycleController();
        offhorizontal_ = OffsetStats{};
        offvertical_ = OffsetStats{};
        invalidateResults();
        return;
    }
    // G: sonuc panelini ac/kapa (IG-7). Kosu bitmeden de acilabilir; o anda
    // panel "o ana kadar" degil, AYNI tohumla TAM kosunun sonucunu gosterir.
    if (key == 'g' || key == 'G') {
        showresults_ = !showresults_;
        if (showresults_ && evalrows_.empty()) {
            startEvaluation();
        }
        return;
    }
    // KAMERA: +/- zoom, oklar kaydir, R gorunumu sifirla.
    if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
        scene3d.zoomBy(kKeyZoomFactor);
        std::cout << "[render] zoom: " << scene3d.zoom() << "x" << std::endl;
        return;
    }
    if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
        scene3d.zoomBy(1.0f / kKeyZoomFactor);
        std::cout << "[render] zoom: " << scene3d.zoom() << "x" << std::endl;
        return;
    }
    if (key == 'r' || key == 'R') {
        scene3d.resetView();
        std::cout << "[render] gorunum sifirlandi (zoom 1x, pan 0)" << std::endl;
        return;
    }
    // Oklar: ekran duzleminde kaydirma. Izometrik kamerada ekran sag/sol
    // ekseni dunya (x,-z), ekran yukari ekseni dunya (-x,-z) yonundedir;
    // bu yuzden pan her iki eksende bilesen alir.
    const float s = kPanStep;
    if (key == GLFW_KEY_LEFT)  { scene3d.panBy(-s,  s); return; }
    if (key == GLFW_KEY_RIGHT) { scene3d.panBy( s, -s); return; }
    if (key == GLFW_KEY_UP)    { scene3d.panBy(-s, -s); return; }
    if (key == GLFW_KEY_DOWN)  { scene3d.panBy( s,  s); return; }
    // HOME: simulasyonu ayni tohumla basa dondur — bitince araclari yeniden
    // gormek ve yon dogrulamasini tekrarlamak icin.
    if (key == GLFW_KEY_HOME) {
        scene.reset();
        offhorizontal_ = OffsetStats{};
        offvertical_ = OffsetStats{};
        invalidateResults();
        std::cout << "[render] simulasyon YENIDEN BASLATILDI (Home)"
                  << std::endl;
        return;
    }
}

void RenderCanvas::onScrollDelta(double dy) {
    if (dy == 0.0) return;
    // pow: kesirli trackpad degerlerini yumusak olcege cevirir. Tekerlek
    // tek "tik"i (|dy|=1) ~%10 zoom demektir.
    scene3d.zoomBy(static_cast<float>(std::pow(kZoomPerScroll, dy)));
}

void RenderCanvas::drawScene3D() {
    scene3d.begin();
    const Network& net = scene.network();
    const World& w = scene.world();

    // YOL KAROLARI: artik HER seridin TAM yolundan (from-dugum -> cells ->
    // to-dugum) doseliyor — ayni kaynak debug mavi cizgisinin kaynagi. Boylece
    // "mavi cizgi var, altinda karo yok" kalmasin (0 hucreli kollar dahil).
    // Ayni hucreyi iki serit paylasir; tekrar dosememek icin set.
    // KAVSAK hucreleri burada ATLANIR: onlar ayri cizilir (tek kez).
    std::set<std::pair<int, int>> junctioncells;
    for (const JunctionId j : net.signalJunctions()) {
        const Vec2i c = net.node(j).cell;
        junctioncells.insert({c.x, c.y});
    }
    std::set<std::pair<int, int>> tiled;
    for (const LaneId lid : net.allLanes()) {
        const std::vector<Vec2d> path = scene.laneCenterPath(lid);
        for (const Vec2d& p : path) {
            // laneCenterPath x+0.5 (hucre merkezi) dondurur; floor ile hucre indeksi.
            const int cx = static_cast<int>(std::floor(p.x));
            const int cy = static_cast<int>(std::floor(p.y));
            if (junctioncells.count({cx, cy}) != 0) continue;
            if (!tiled.insert({cx, cy}).second) continue;
            // KARO YONU: hucrenin ekseni serit geometrisinden TURETILIR
            // (SimScene::roadAxisAt). Dikey kolda karo 90° dondurulur, yoksa
            // yol cizgileri aracin gidis yonune dik kalir (olculen hata).
            const RoadAxis axis = scene.roadAxisAt(Vec2i{cx, cy});
            scene3d.drawRoadTile(Vec2i{cx, cy}, axis == RoadAxis::NorthSouth);
        }
    }
    // Kavsak karolari (kavsak hucresi): sinyalli dugumler. Tek kez cizilir.
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

void RenderCanvas::measureLaneOffset() {
    // Olcum matematigi SimScene'de TEK yerde (cizimle ayni fonksiyonlar).
    // Burada yalnizca yatay/dikey gruplara ayirip raporluyoruz.
    for (const SimScene::VehicleOffset& o : scene.measureVehicleOffsets()) {
        if (o.horizontal) {
            offhorizontal_.add(o.lateralM, o.tileM);
        } else {
            offvertical_.add(o.lateralM, o.tileM);
        }
    }

    const auto printStats = [](const char* ad, const OffsetStats& s) {
        if (s.samples == 0) {
            std::printf("[olcum] %-5s: ornek YOK (o yonde arac cizilmedi)\n", ad);
            return;
        }
        const bool ok = std::abs(s.latMin - kLaneOffsetM) < 0.05 &&
                        std::abs(s.latMax - kLaneOffsetM) < 0.05 &&
                        std::abs(s.tileMin - kLaneOffsetM) < 0.05 &&
                        std::abs(s.tileMax - kLaneOffsetM) < 0.05;
        std::printf("[olcum] %-5s n=%-4d serit merkezi %.3f..%.3f m (ort %.3f) | "
                    "karo ekseni %.3f..%.3f m (ort %.3f) | beklenen %.2f -> %s\n",
                    ad, s.samples,
                    s.latMin, s.latMax, s.latSum / s.samples,
                    s.tileMin, s.tileMax, s.tileSum / s.samples,
                    kLaneOffsetM, ok ? "OK" : "FARKLI");
    };
    printStats("yatay", offhorizontal_);
    printStats("dikey", offvertical_);
}

void RenderCanvas::startEvaluation() {
    evalrows_.clear();
    const WorldConfig& cfg = scene.worldConfig();
    const std::uint64_t seed = scene.seed();

    std::cout << "[sonuc] ayni tohumla uc kontrolcu kosuluyor: tohum " << seed
              << "  (" << cfg.vehicleCount << " arac / " << cfg.spawnInterval
              << " s / profil " << toString(cfg.profile) << ")" << std::endl;

    const ControllerKind kinds[3] = {ControllerKind::FixedTime,
                                     ControllerKind::ThresholdRule,
                                     ControllerKind::QLearning};
    for (const ControllerKind k : kinds) {
        const EvalRow row = runHeadless(seed, k, cfg, scene.qPolicy());
        evalrows_.push_back(row);
        std::printf("[sonuc] %-22s yolculuk %6.2f s  sistemde %6.2f s  varis %5.1f%%"
                    "  (%d/%d)  gridlock %d  carpisma %d  sim-sn %.0f%s\n",
                    row.controller.c_str(), row.averageTravelTime,
                    row.averageTimeInSystem, row.arrivedFraction * 100.0,
                    row.arrived, row.spawned, row.stalledEvents, row.collisions,
                    row.simSeconds,
                    row.finished && !row.timedOut ? "  tamam" : "  ZAMAN ASIMI");
    }
    std::fflush(stdout);
}

void RenderCanvas::invalidateResults() {
    evalrows_.clear();
    showresults_ = false;
    evalautoopened_ = false;
    paneldrawn_ = false;
}

void RenderCanvas::drawResultPanel() {
    if (!hudok_) return;
    if (!paneldrawn_) {
        paneldrawn_ = true;
        std::cout << "[sonuc] panel ciziliyor: " << evalrows_.size()
                  << " satir (tohum " << scene.seed() << ")" << std::endl;
    }

    // Panel: pencere ortasinda. Sabit konum (pencere 1280x720 varsayimi)
    // yerine mevcut boyuta gore ortalanir.
    const float w = 980.0f;
    const float h = 214.0f;
    const float x = (static_cast<float>(getWidth()) - w) * 0.5f;
    const float y = (static_cast<float>(getHeight()) - h) * 0.5f;

    setColor(6, 10, 9, 225);
    gDrawRectangle(x, y, w, h, true);
    setColor(70, 100, 85, 255);
    gDrawRectangle(x, y, w, 2.0f, true);  // ince ust cizgi

    const WorldConfig& cfg = scene.worldConfig();
    char baslik[224];
    std::snprintf(baslik, sizeof baslik,
                  "SONUC  -  tohum %llu  -  %d arac / %.1f s  -  profil %s  -  %s",
                  static_cast<unsigned long long>(scene.seed()),
                  cfg.vehicleCount, cfg.spawnInterval, toString(cfg.profile),
                  scene.controllerName());
    setColor(235, 245, 235);
    hudfont_.drawText(baslik, x + 22.0f, y + 16.0f);

    // Sutunlar sabit x'te: oransal fontta hizalama ancak boyle saglanir.
    const float cx[6] = {x + 22.0f, x + 250.0f, x + 384.0f,
                         x + 518.0f, x + 626.0f, x + 716.0f};
    const float colstatus = x + 826.0f;
    const char* basliklar[6] = {"kontrolcu", "ort. yolculuk", "ort. sistemde",
                                "varis", "gridlock", "sim-sn"};
    setColor(130, 155, 140);
    for (int i = 0; i < 6; ++i) {
        hudfontsmall_.drawText(basliklar[i], cx[i], y + 50.0f);
    }
    hudfontsmall_.drawText("durum", colstatus, y + 50.0f);

    // En kisa SANSURSUZ sure yesil. Neden yolculuk degil: yolculuk yalnizca
    // varanlari sayar; gridlock yapan kontrolcu "daha iyi" gorunur
    // (denetim.md BULGU 2, AGENTS §8.9). Sansursuz metrik takilanlari da
    // sayar.
    double eniyi = -1.0;
    for (const EvalRow& r : evalrows_) {
        if (r.ok && (eniyi < 0.0 || r.averageTimeInSystem < eniyi)) {
            eniyi = r.averageTimeInSystem;
        }
    }

    char hucre[64];
    for (std::size_t i = 0; i < evalrows_.size() && i < 3; ++i) {
        const EvalRow& r = evalrows_[i];
        const float ry = y + 80.0f + static_cast<float>(i) * 30.0f;
        setColor(200, 215, 205);
        hudfontsmall_.drawText(r.controller.c_str(), cx[0], ry);

        if (!r.ok) {
            setColor(230, 120, 110);
            hudfontsmall_.drawText("HATA", cx[1], ry);
            continue;
        }

        std::snprintf(hucre, sizeof hucre, "%.1f s", r.averageTravelTime);
        hudfontsmall_.drawText(hucre, cx[1], ry);

        const bool best = (eniyi >= 0.0 && r.averageTimeInSystem == eniyi);
        if (best) setColor(130, 230, 150);
        else      setColor(200, 215, 205);
        std::snprintf(hucre, sizeof hucre, "%.1f s", r.averageTimeInSystem);
        hudfontsmall_.drawText(hucre, cx[2], ry);
        std::snprintf(hucre, sizeof hucre, "%.0f%%", r.arrivedFraction * 100.0);
        hudfontsmall_.drawText(hucre, cx[3], ry);
        std::snprintf(hucre, sizeof hucre, "%d", r.stalledEvents);
        hudfontsmall_.drawText(hucre, cx[4], ry);
        std::snprintf(hucre, sizeof hucre, "%.0f", r.simSeconds);
        hudfontsmall_.drawText(hucre, cx[5], ry);

        const char* durum = "tamam";
        if (r.collisions > 0)      durum = "CARPISMA";
        else if (r.timedOut)       durum = "zaman asimi";
        else if (r.stalledEvents)  durum = "gridlock";
        if (r.timedOut || r.collisions > 0) setColor(230, 140, 120);
        else if (r.stalledEvents)           setColor(225, 200, 120);
        else                                setColor(130, 210, 150);
        hudfontsmall_.drawText(durum, colstatus, ry);
    }

    if (evalrows_.empty()) {
        setColor(180, 195, 185);
        hudfontsmall_.drawText("hesaplaniyor...", cx[0], y + 84.0f);
    }

    setColor(120, 140, 130);
    hudfontsmall_.drawText(
        "[G] kapat   [C] kontrolcu degistir (ayni tohumla yeni kosu)   [Home] bastan",
        x + 22.0f, y + 182.0f);
    setColor(95, 115, 105);
    hudfontsmall_.drawText(
        "yesil = en kisa ort. sistemde (SANSURSUZ: takilan araclar dahil); varis % ve gridlock ile birlikte oku",
        x + 22.0f, y + 160.0f);
}

void RenderCanvas::drawHud() {
    if (!hudok_) return;
    const World& w = scene.world();
    const Metrics& m = w.metrics();

    int active = 0;
    for (const Vehicle& v : w.vehicles()) {
        if (v.valid()) ++active;
    }

    // Saat sim-core'dan okunur (World::hourOfDay); burada TEKRAR hesaplanmaz.
    const int dakika = static_cast<int>(std::lround(w.hourOfDay() * 60.0)) % (24 * 60);
    char saat[8];
    std::snprintf(saat, sizeof saat, "%02d:%02d", dakika / 60, dakika % 60);

    char satir1[192];
    std::snprintf(satir1, sizeof satir1, "Saat %s   profil %s (x%.2f)   hiz %dx%s",
                  saat, toString(scene.demandProfile()), w.demandMultiplier(),
                  scene.stepsPerSecond(),
                  scene.paused() ? "   DURAKLATILDI" : "");

    char satir2[224];
    std::snprintf(satir2, sizeof satir2,
                  "kontrolcu: %s   arac %d/%d   varan %d   tikanma %d/%d   sim-sn %.0f   FPS %d",
                  scene.controllerName(), active, w.config().vehicleCount,
                  m.arrived, m.stalledNow, m.maxStalled, w.time(),
                  root->getFramerate());

    char durum[224];
    if (w.finished()) {
        std::snprintf(durum, sizeof durum, "SIM BITTI (tum araclar hedefe ulasti)");
    } else if (w.timedOut()) {
        std::snprintf(durum, sizeof durum, "SIM BITTI (zaman asimi)");
    } else {
        std::snprintf(durum, sizeof durum,
                      "[1-8] hiz  [P] duraklat  [T] profil  [C] kontrolcu  [Home] bastan");
    }
    const char* ipucu =
        "[+/-] tekerlek: zoom   [oklar] kaydir   [R] gorunum   [D] debug   [M] olcum";

    // Panel (alfa karisimli) + metinler. Renk renderer'a setColor ile verilir;
    // gFont::drawText bu rengi kullanir.
    setColor(8, 12, 10, 170);
    gDrawRectangle(14.0f, 14.0f, 760.0f, 132.0f, true);

    setColor(235, 245, 235);
    hudfont_.drawText(satir1, 26.0f, 24.0f);
    setColor(190, 205, 195);
    hudfontsmall_.drawText(satir2, 26.0f, 54.0f);
    setColor(140, 160, 150);
    hudfontsmall_.drawText(durum, 26.0f, 78.0f);
    setColor(120, 140, 130);
    hudfontsmall_.drawText(ipucu, 26.0f, 104.0f);
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
