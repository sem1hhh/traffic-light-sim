// render/src/render_app.h — uygulama kok sinifi (gisImage orneginin kalibi).
//
// Canvas'i olusturup engine'e teslim eder; baska is yapmaz.

#pragma once

#include "gBaseApp.h"

namespace tsc {
namespace render {

class RenderApp : public gBaseApp {
public:
    RenderApp();
    RenderApp(int argc, char** argv);
    ~RenderApp();

    void setup() override;
    void update() override;
};

}  // namespace render
}  // namespace tsc
