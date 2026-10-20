// render/src/main.cpp — GlistEngine giris noktasi (Faz 7).
//
// Bu dosya yalnizca motoru baslatir. Tum is mantigi sim-core'dadir; burasi
// sadece World durumunu okur ve ekrana cizer (AGENTS.md: render is mantigi
// yeniden yazmaz).

#include "gAppManager.h"
#include "render_app.h"

int main(int argc, char** argv) {
    gStartEngine(new tsc::render::RenderApp(argc, argv), "TSC Traffic Sim",
                 G_WINDOWMODE_APP, 1280, 720);
    return 0;
}
