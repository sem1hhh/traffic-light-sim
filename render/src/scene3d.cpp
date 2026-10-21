// render/src/scene3d.cpp — 3B spike sahnesi implementasyonu.
//
// Yalnizca CIZIM: model yukleme, kamera, isik ve modeleli cizim. Is mantigi
// (sim-core) yok. Model yollari motorun `assets/models/` altindan cozulur.

#include "scene3d.h"

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

// Arac yonu: Kenney arabalari -Z'ye bakar; sim yon acisindan donmeye cevirir.
constexpr float kCarYawOffsetDeg = 180.0f;
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
    light_.setAmbientColor(70, 70, 78);
    light_.setDiffuseColor(190, 188, 180);
    light_.setSpecularColor(40, 40, 40);
}

void Scene3D::begin() {
    camera_.begin();
    light_.enable();
}

void Scene3D::end() {
    light_.disable();
    camera_.end();
}

void Scene3D::drawRoadTile(const Vec2i& cell) {
    if (!roadloaded_) return;
    road_.setPosition((static_cast<float>(cell.x) + 0.5f) * kCellWorld - centerx_,
                      0.0f,
                      (static_cast<float>(cell.y) + 0.5f) * kCellWorld - centerz_);
    road_.setOrientation(glm::vec3(0.0f));
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
    car_.setPosition(simX * kCellWorld - centerx_,
                     0.0f,
                     simZ * kCellWorld - centerz_);
    // Sim yon acisi -> derece. Araba modeli -Z'ye bakar (kCarYawOffsetDeg).
    const float yawDeg = static_cast<float>(headingRad * 180.0 / 3.14159265358979) +
                         kCarYawOffsetDeg;
    car_.setOrientation(glm::vec3(0.0f, glm::radians(yawDeg), 0.0f));
    car_.setScale(0.6f);
    car_.draw();
}

}  // namespace render
}  // namespace tsc
