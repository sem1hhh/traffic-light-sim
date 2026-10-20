// render/src/render_canvas.h — izgara cizimi + FPS logu (Faz 7 ilk adimi).
//
// Bu canvas SIMULASYONU CALISTIRMAZ. Su anlik gorevi: (1) izgara cizgileri
// cizmek — izgara editörünün (İG-1) gorsel tohumu, (2) FPS'i olcmek ve
// loglamak — İOG-2'nin ilk kaniti. Simulasyon dongusu Faz 7'nin sonraki
// adiminda sim-core World'u baglaninca eklenecek.

#pragma once

#include "gBaseCanvas.h"
#include "render_app.h"

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
    RenderApp* root;

    // FPS logu: her 60 karede bir ortalama yaz.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
