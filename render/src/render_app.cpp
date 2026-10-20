// render/src/render_app.cpp — canvas kurulumu.

#include "render_app.h"
#include "render_canvas.h"

namespace tsc {
namespace render {

RenderApp::RenderApp() {
}

RenderApp::RenderApp(int argc, char** argv) : gBaseApp(argc, argv) {
}

RenderApp::~RenderApp() {
}

void RenderApp::setup() {
    RenderCanvas* cnv = new RenderCanvas(this);
    appmanager->setCurrentCanvas(cnv);
}

void RenderApp::update() {
}

}  // namespace render
}  // namespace tsc
