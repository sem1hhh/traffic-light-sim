// render/src/render_canvas.cpp — izgara + FPS logu.
//
// Cizim API'si: global gDrawLine/gDrawRectangle + setColor (core/gRenderer.h).
// FPS: root->getFramerate() — motorun kendi olcumu.

#include "render_canvas.h"

#include <iostream>

namespace tsc {
namespace render {

namespace {
// Izgara: 20 x 12 hucre, hucre basina 60 piksel.
constexpr int kCellSize = 60;
constexpr int kGridCols = 20;
constexpr int kGridRows = 12;
constexpr int kFpsLogInterval = 60;  // kare
}  // namespace

RenderCanvas::RenderCanvas(RenderApp* root) : gBaseCanvas(root) {
    this->root = root;
}

RenderCanvas::~RenderCanvas() {
}

void RenderCanvas::setup() {
    std::cout << "[render] canvas setup: " << getWidth() << "x" << getHeight()
              << "  grid " << kGridCols << "x" << kGridRows
              << "  hucre=" << kCellSize << "px" << std::endl;
}

void RenderCanvas::update() {
    // FPS topla ve periyodik logla (İOG-2 kaniti).
    fpssum += static_cast<double>(root->getFramerate());
    ++framecounter;
    if (framecounter >= kFpsLogInterval) {
        const double avg = fpssum / static_cast<double>(framecounter);
        std::cout << "[render] FPS ortalamasi (" << framecounter
                  << " kare): " << avg << std::endl;
        framecounter = 0;
        fpssum = 0.0;
    }
}

void RenderCanvas::draw() {
    // Arka plan.
    clearColor(18, 24, 22);

    // Izgara cizgileri — İG-1 izgara editörünün gorsel tohumu.
    setColor(60, 80, 70);
    const int w = kGridCols * kCellSize;
    const int h = kGridRows * kCellSize;
    for (int c = 0; c <= kGridCols; ++c) {
        gDrawLine(static_cast<float>(c * kCellSize), 0.0f,
                  static_cast<float>(c * kCellSize), static_cast<float>(h));
    }
    for (int r = 0; r <= kGridRows; ++r) {
        gDrawLine(0.0f, static_cast<float>(r * kCellSize),
                  static_cast<float>(w), static_cast<float>(r * kCellSize));
    }

    // Kenar: izgaranin disini biraz karart (odak).
    setColor(18, 24, 22);
    gDrawRectangle(static_cast<float>(w), 0.0f,
                   static_cast<float>(getWidth() - w),
                   static_cast<float>(getHeight()), true);
    gDrawRectangle(0.0f, static_cast<float>(h),
                   static_cast<float>(getWidth()),
                   static_cast<float>(getHeight() - h), true);
}

}  // namespace render
}  // namespace tsc
