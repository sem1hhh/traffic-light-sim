// render/src/scene3d.h — 3B spike sahnesi (Faz 7 / İOG-6).
//
// Amaç (yarım gun siniri): Kenney (CC0) Car Kit'ten tek bir ARABA modelini ve
// City Kit (Roads)'ten tek bir YOL parcayasini OBJ+MTL+texture olarak yukleyip
// izometrik kamerayla cizmek. Motorun assimp tabanli gModel yukleyicisi MTL'deki
// `map_Kd Textures/colormap.png` yolunu kendi cozer.
//
// Cizim kodu "aracar / yol / isik NASIL cizilir" diye AYRI fonksiyonlarda
// toplandi (AGENTS.md: render is mantigi yeniden yazmaz; burada sadece cizim
// var). Boylece spike basarili olursa bu fonksiyonlarin ici tam 3B modele
// gecirilebilir; basarisiz olursa2D donulur.
//
// KAMERA: GlistEngine'de ortografik kamera YOK (yalnizca glm::perspective).
// "Izometrik" gorunum, dar FOV + uzak kamera ile taklit edilir (gercek
// izometrik icin ortho gerekir; bu spike dar FOV'un yeterli olup olmadigini
// olcer).

#pragma once

#include "tsc/network.hpp"
#include "tsc/types.hpp"
#include "tsc/world.hpp"

#include "gCamera.h"
#include "gLight.h"
#include "gModel.h"

#include <vector>

namespace tsc {
namespace render {

/// Izometrik benzeri (dar FOV + uzak) kamera + isik + Kenney modelleri.
class Scene3D {
public:
    /// Modelleri ve kamerayi kurar. Model dosyalar assets/models/{car,road}/.
    /// Basarisiz yukleme log'a yazilir (throw ETMEZ; spike dayanikli olmali).
    Scene3D(int viewportWidth, int viewportHeight);

    /// Kamerayi ve isigi baslat (camera.begin + light.enable). Cizim fonksiyonlari
    /// bunlarin ARASINDA cagirilir.
    void begin();
    /// Kamerayi/isiigi bitir.
    void end();

    bool modelsLoaded() const noexcept { return carloaded_ && roadloaded_; }

    // --- Cizim fonksiyonlari (her biri tek is yapar) -------------------------
    /// Bir yol hucresini (izgara cell) 3B zemin karosu olarak ciz.
    void drawRoadTile(const Vec2i& cell);
    /// Bir kavsongu (cell) 3B karo olarak ciz (spike: yol karosu ile ayni).
    void drawJunctionTile(const Vec2i& cell);
    /// Bir arac konumunu (izgara biriminde kayan-nokta) donmus olarak ciz.
    /// `headingRad` yon acisi; araba modeli -Z'ye bakar varsayilir.
    void drawCar(float simX, float simZ, double headingRad);

private:
    void setupCameraAndLight(int w, int h);

    gCamera camera_;
    gLight light_;
    gModel car_;
    gModel road_;
    bool carloaded_ = false;
    bool roadloaded_ = false;

    // Izgara -> dunya merkez kaymasi (izgarayi ekranda ortala).
    float centerx_ = 0.0f;
    float centerz_ = 0.0f;
};

}  // namespace render
}  // namespace tsc
