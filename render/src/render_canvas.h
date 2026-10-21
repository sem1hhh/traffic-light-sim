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
#include "render_app.h"
#include "scene3d.h"
#include "sim_scene.h"

namespace tsc {
namespace render {

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

    RenderApp* root;
    SimScene scene;
    Scene3D scene3d;

    // Hata ayiklama katmani acik mi? (D tusu ile acilir/kapanir.)
    bool debuglayer = false;
    // Yon logu sayaci (debug katmaninda ~2 sn'de bir omurga/kol acisi basar).
    int yonlogcounter_ = 0;

    // FPS logu.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
