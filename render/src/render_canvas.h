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
    // Izgara hucre cizgisine piksel cevirici. Grid 9x7; hucresel olcekleme.
    void drawRoads();
    void drawJunctions();
    void drawVehicles();

    RenderApp* root;
    SimScene scene;

    // FPS logu.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
