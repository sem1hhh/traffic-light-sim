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
#include "trainer.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

    /// GLFW kaydirma (trackpad iki parmak / fare tekerlegi) koprusu.
    /// Motor bu olayi `int`e kirptigi icin trackpad'in kesirli degerleri
    /// KAYBOLUYORDU (0'a yuvarlanip zoom hic tetiklenmiyordu). Bu yuzden
    /// GLFW callback'i dogrudan `double` alir (bkz. .cpp scrollThunk).
    void onScrollDelta(double dy);

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

    // --- IG-7 sonuc paneli -------------------------------------------------
    /// Ayni tohum + ayni senaryoyla uc kontrolcuyu GUI'siz kosar ve tabloyu
    /// doldurur. Yaklasik 3 x 5 ms surer (olculdu); kareyi kasmaz.
    void startEvaluation();
    /// Tohum/senaryo degisti: panel kapanir, eski sonuclar gecersiz olur.
    void invalidateResults();
    void drawResultPanel();

    /// Son kosunun uc kontrolcu ozeti (bos: henuz hesaplanmadi).
    std::vector<EvalRow> evalrows_;
    bool showresults_ = false;      ///< panel acik mi (G)
    bool evalautoopened_ = false;   ///< bu kosu icin otomatik acildi mi
    bool paneldrawn_ = false;       ///< ilk cizimde bir kez log (kanit)

    // --- IG-6 egitim (arka plan) -------------------------------------------
    /// E tusu: egitimi worker thread'inde baslatir. Ogrenme TEK is
    /// parcaciginda (İOG-3); arayuz yalnizca ilerleme kopyasini okur.
    void startTraining(int episodes);
    /// Her karede: ilerleme logu + biten egitimin politikasini devral.
    void pollTraining();
    /// Alt soldaki egitim paneli + ilerleme cubugu.
    void drawTrainPanel();

    BackgroundTrainer trainer_;
    int trainlogged_ = 0;  ///< son loglanan bolum (periyodik log icin)
    bool trainfaillogged_ = false;  ///< hata satiri bir kez basilir
    bool trainpaneldrawn_ = false;  ///< panelin ilk cizimi bir kez log (kanit)
    int frames_ = 0;                ///< ilk kareler isinma payi (olcum disi)

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
    // Ofset olcumu birikimli istatistikleri (yatay / dikey). Yalnizca M
    // tusuyla basilir — periyodik log YOK (kullanici istegi).
    OffsetStats offhorizontal_;
    OffsetStats offvertical_;

    // FPS logu.
    int framecounter = 0;
    double fpssum = 0.0;
};

}  // namespace render
}  // namespace tsc
