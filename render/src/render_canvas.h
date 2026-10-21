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

private:
    // 3B spike cizimi (Kenney modelleri + izometrik kamera). Basarisiz olursa
    // false doner ve cizim2D'ye dusur.
    void drawScene3D();
    //2D yedek cizim (spike oncesi).
    void drawRoads();
    void drawJunctions();
    void drawVehicles();

    RenderApp* root;
    SimScene scene;
    Scene3D scene3d;

    // FPS logu.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
