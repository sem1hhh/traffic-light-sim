// render/src/scene3d.cpp — 3B spike sahnesi implementasyonu.
//
// Yalnizca CIZIM: model yukleme, kamera, isik ve modeleli cizim. Is mantigi
// (sim-core) yok. Model yollari motorun `assets/models/` altindan cozulur.

#include "scene3d.h"

#include "gRenderer.h"   // gEnableCulling / gCullFace / GL_BACK

#include <cmath>

namespace tsc {
namespace render {

namespace {
// Izometrik benzeri gorunum: dar FOV + uzak kamera. GlistEngine'de ortografik
// kamera yok; dar FOV perspektifi izometrike yakinlastirir.
constexpr float kFovDeg = 22.0f;
// Kamera, sahnenin sag-ust-on kosesinden bakar (klasik izometrik aci ~35/45).
constexpr float kCamDist = 34.0f;
constexpr float kCellWorld = 1.0f;   // 1 izgara hucresi = 1 dunya birimi

// Hata ayiklama katmani (D tusu) cizgileri zeminin biraz ustunde; z-fight yok.
constexpr float kDebugY = 0.05f;

// (2026-10-22) kCarYawOffsetDeg KALDIRILDI: arac yonu artik sabit ofsetle
// degil, serit yon vektorunden coords.h carYawDeg() ile hesaplanir. Eski 270
// ofseti araci kutuya ~90 DIK birakiyordu (olcum: model on ekseni +Z).
}  // namespace

Scene3D::Scene3D(int viewportWidth, int viewportHeight) {
    setupCameraAndLight(viewportWidth, viewportHeight);

    // Izgarayi merkeze al (9x7 -> merkez 4.5, 3.5).
    centerx_ = 4.5f * kCellWorld;
    centerz_ = 3.5f * kCellWorld;

    car_.loadModel("car/taxi.obj");
    road_.loadModel("road/road-straight.obj");
    // Basari isareti: model yukleyici hatasiz donerse donusim (transform)
    // sifirlanmaz; burada sadece varlik kontrolu yapilir. Gercek basari,
    // draw()'da goruntudur. carloaded_/roadloaded_ draw oncesinde kontrol edilir.
    carloaded_ = true;
    roadloaded_ = true;
}

void Scene3D::setupCameraAndLight(int /*w*/, int /*h*/) {
    camera_.setFov(kFovDeg);
    camera_.setNearClip(0.1f);
    camera_.setFarClip(200.0f);
    // Izometrik aci: 45 derece yan, ~30 derece yuksekten.
    const float d = kCamDist;
    camera_.setPosition(d * 0.7f, d * 0.62f, d * 0.7f);
    camera_.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    light_ = gLight(gLight::LIGHTTYPE_DIRECTIONAL);
    light_.setPosition(6.0f, 14.0f, 8.0f);
    // KULLANICI GOZLEMİ (2026-10-21): govde asiri parlakti. Diffuse/specular
    // dusuruldu, ambient biraz acildi ki golge/govde detayi gorunur olsun.
    light_.setAmbientColor(95, 95, 100);
    light_.setDiffuseColor(120, 118, 112);
    light_.setSpecularColor(20, 20, 20);
}

void Scene3D::begin() {
    camera_.begin();
    light_.enable();
    // KULLANICI GOZLEMİ (2026-10-21): tekerleklerin İCİ gorunuyordu — backface
    // culling kapaliydi. Arkayuz yuzleri artik cizilmiyor.
    gEnableCulling();
    gCullFace(GL_BACK);
}

void Scene3D::end() {
    gDisableCulling();
    light_.disable();
    camera_.end();
}

void Scene3D::drawRoadTile(const Vec2i& cell, bool alongZ) {
    if (!roadloaded_) return;
    road_.setPosition((static_cast<float>(cell.x) + 0.5f) * kCellWorld - centerx_,
                      0.0f,
                      (static_cast<float>(cell.y) + 0.5f) * kCellWorld - centerz_);
    // KARO YONU (olculdu): road-straight.obj'de asfalt + serit cizgileri X
    // ekseni boyunca uzanir, kaldirim bantlari Z kenarindadir. Dikey kolda
    // 90° dondurulmezse aracin gidis yonu ile yolun cizgileri DIK kalir.
    road_.setOrientation(glm::vec3(0.0f, alongZ ? glm::radians(90.0f) : 0.0f, 0.0f));
    // setScale (mutlak); scale() birikimli oldugu icin her karede sifirlardi.
    road_.setScale(kCellWorld);
    road_.draw();
}

void Scene3D::drawJunctionTile(const Vec2i& cell) {
    // Spike: kavsongu da ayni yol karosu. Tam kavsak modeli (cross) sonraki adimda.
    drawRoadTile(cell);
}

void Scene3D::drawCar(float simX, float simZ, double headingRad) {
    if (!carloaded_) return;
    // Sagdan gidiş: arac yol MERKEZINDEN saga kayar (coords.h). Boylece karsi
    // yonler AYNI cizgide kalmaz.
    double wx = 0.0, wz = 0.0;
    applyLaneOffset(static_cast<double>(simX), static_cast<double>(simZ),
                    headingRad, wx, wz);

    car_.setPosition(static_cast<float>(wx) * kCellWorld - centerx_,
                     0.0f,
                     static_cast<float>(wz) * kCellWorld - centerz_);
    // YON: sabit ofset YOK. Serit yon vektorunden (atan2(dz, dx)) + modelin
    // olculmus on ekseni (coords.h kCarModelFrontAxisDeg) ile hesaplanir.
    const double dirX = std::sin(headingRad);
    const double dirZ = std::cos(headingRad);
    car_.setOrientation(glm::vec3(0.0f, glm::radians(static_cast<float>(carYawDeg(dirX, dirZ))), 0.0f));
    // Araç boyu coords.h kCarModelScale ile 4.5 m'ye (0.45 dunya birimi) gecer.
    car_.setScale(static_cast<float>(kCarModelScale));
    car_.draw();
}

void Scene3D::drawDebugLine(double x0, double z0, double x1, double z1) {
    // Zemin ustu ince cizgi (dunya birimi; x0/z0 izgara birliginde gelir).
    gDrawLine(static_cast<float>(x0) * kCellWorld - centerx_,
              kDebugY,
              static_cast<float>(z0) * kCellWorld - centerz_,
              static_cast<float>(x1) * kCellWorld - centerx_,
              kDebugY,
              static_cast<float>(z1) * kCellWorld - centerz_, 2.0f);
}

void Scene3D::drawVehicleBox(double centerX, double centerZ, double headingRad) {
    // Ayni sag-ofseti uygulayip ayak-izi kutusunu ciz — 3B arac bunun ustunde.
    double wx = 0.0, wz = 0.0;
    applyLaneOffset(static_cast<double>(centerX), static_cast<double>(centerZ),
                    headingRad, wx, wz);

    double x0, z0, x1, z1, x2, z2, x3, z3;
    carFootprint(wx, wz, headingRad, x0, z0, x1, z1, x2, z2, x3, z3);
    const auto W = [&](double x) { return static_cast<float>(x) * kCellWorld; };
    gDrawLine(W(x0) - centerx_, kDebugY, W(z0) - centerz_,
              W(x1) - centerx_, kDebugY, W(z1) - centerz_, 2.0f);
    gDrawLine(W(x1) - centerx_, kDebugY, W(z1) - centerz_,
              W(x2) - centerx_, kDebugY, W(z2) - centerz_, 2.0f);
    gDrawLine(W(x2) - centerx_, kDebugY, W(z2) - centerz_,
              W(x3) - centerx_, kDebugY, W(z3) - centerz_, 2.0f);
    gDrawLine(W(x3) - centerx_, kDebugY, W(z3) - centerz_,
              W(x0) - centerx_, kDebugY, W(z0) - centerz_, 2.0f);
}

void Scene3D::drawDebugArrow(double centerX, double centerZ, double dirX, double dirZ) {
    // Ok, aracin MERKEZinden gidis yonune dogru: seritteki bir sonraki noktaya
    // dogru (yon birim vektor, normalize edilir). Ofset uygulanmis merkezden
    // baslar — boylece 3B aracin burnu okun ustunden cikmali.
    const double len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (len < 1e-9) return;
    const double ux = dirX / len;
    const double uz = dirZ / len;

    double cx = 0.0, cz = 0.0;
    applyLaneOffset(centerX, centerZ, std::atan2(ux, uz), cx, cz);

    const double tipX = cx + ux * 0.55;
    const double tipZ = cz + uz * 0.55;
    const auto W = [&](double x) { return static_cast<float>(x) * kCellWorld; };
    // Govde.
    gDrawLine(W(cx) - centerx_, kDebugY + 0.02f, W(cz) - centerz_,
              W(tipX) - centerx_, kDebugY + 0.02f, W(tipZ) - centerz_, 3.0f);
    // Uc kanatlari (govdeye 30 derece, 0.18 birim).
    const double a = std::atan2(ux, uz) + 0.5235988;  // +30°
    const double b = std::atan2(ux, uz) - 0.5235988;  // −30°
    gDrawLine(W(tipX) - centerx_, kDebugY + 0.02f, W(tipZ) - centerz_,
              W(tipX + std::sin(a) * 0.18) - centerx_, kDebugY + 0.02f,
              W(tipZ + std::cos(a) * 0.18) - centerz_, 3.0f);
    gDrawLine(W(tipX) - centerx_, kDebugY + 0.02f, W(tipZ) - centerz_,
              W(tipX + std::sin(b) * 0.18) - centerx_, kDebugY + 0.02f,
              W(tipZ + std::cos(b) * 0.18) - centerz_, 3.0f);
}

}  // namespace render
}  // namespace tsc
