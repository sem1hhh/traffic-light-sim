// render/src/render_canvas.h — simulasyonu cizen canvas (Faz 7).
//
// Bu canvas bir SimScene sahiplenir ve GERCEK zamandan bagimsiz bir tempoda
// (saniyede N simulasyon adimi) ilerletir. Her karede dunya durumunu OKUR:
//   - yol hucreleri ve kavsaklar
//   - trafik isiklari (fazla senkron renkler — IG-2)
//   - araclar (IDM konumlari)
// Is mantigi yok; hepsi sim-core (AGENTS.md: render is mantigi yazmaz).

#pragma once

#include "gBaseCanvas.h"
#include "gFont.h"
#include "render_app.h"
#include "scene3d.h"
#include "sim_scene.h"

namespace tsc {
namespace render {

/// Bir yon grubunun (yatay/dikey) ofset olcumleri. min/max, "ortalama 1.75
/// cikti" demenin gizleyebilecegi sapmalari gorunur kilar: karo ekseni yanlissa
/// deger 0'a duser, ortalama ise bunu yumusatir.
struct OffsetStats {
    int samples = 0;
    double latMin = 0.0, latMax = 0.0, latSum = 0.0;   ///< serit merkezine dik uzaklik (m)
    double tileMin = 0.0, tileMax = 0.0, tileSum = 0.0; ///< cizilen KARO eksenine dik uzaklik (m)

    void add(double latM, double tileM);
};

class RenderCanvas : public gBaseCanvas {
public:
    explicit RenderCanvas(RenderApp* root);
    ~RenderCanvas() override;

    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // 3B spike cizimi (Kenney modelleri + izometrik kamera). Basarisiz olursa
    // false doner ve cizim2D'ye dusur.
    void drawScene3D();
    // Hata ayiklama katmani (D tusu): serit merkez cizgileri + arac kutulari.
    // 3B arabalar bu kutularin ustunde durmali.
    void drawDebugLayer();
    //2D yedek cizim (spike oncesi).
    void drawRoads();
    void drawJunctions();
    void drawVehicles();

    // --- HUD (IG-3: saat ekranda) -----------------------------------------
    /// Saat + profil + hiz + arac sayaclari. Sim-core'u YALNIZCA okur.
    void drawHud();
    /// Serit merkezine ve CIZILEN KARO eksenine dik uzakligi olcer (log).
    /// Beklenen: her iki yonde de 1.75 m (kLaneOffsetM). Tolerans disi -> FAIL.
    void measureLaneOffset();

    RenderApp* root;
    SimScene scene;
    Scene3D scene3d;

    // HUD fontlari (assets/fonts). Yuklenemezse HUD cizilmez (cokme yok).
    gFont hudfont_;
    gFont hudfontsmall_;
    bool hudok_ = false;

    // Hata ayiklama katmani acik mi? (D tusu ile acilir/kapanir.)
    bool debuglayer = false;
    // Yon logu sayaci (debug katmaninda ~2 sn'de bir omurga/kol acisi basar).
    int yonlogcounter_ = 0;
    // Ofset olcumu sayaci + birikimli istatistikler (yatay / dikey).
    int offcounter_ = 0;
    OffsetStats offhorizontal_;
    OffsetStats offvertical_;

    // FPS logu.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
