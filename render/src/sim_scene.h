// render/src/sim_scene.h — render katmaninda simulasyon sahiplenicisi.
//
// Bu sinif sim-core'un World/SignalDriver/Intersection siniflarini SAHIPLINIR
// ve onlari kare hizindan BAGIMSIZ bir tempoda ilerletir. Cizim icin yalnizca
// OKUMA erisimi verir (const World& / const Network&). Is mantigi yeniden
// yazilmaz; hepsi sim-core'adir (AGENTS.md: render is mantigi yeniden yazmaz).
//
// Neden ayri bir Sinif: "her karede world.step()" YANLISTIR. Kare hizi 60 iken
// bu 60 sim-sn/saniye olur; 30'a duserse simulasyon yavaslar. Burada bir
// AKUMULATOR var: gecen GERCEK sureyi toplar, her 1 sim-sn'de bir adim atar.
// Boylece simulasyon hizi FPS'ten bagimsizdir.

#pragma once

#include "tsc/network.hpp"
#include "tsc/types.hpp"
#include "tsc/world.hpp"
#include "controller.hpp"       // SignalDriver
#include "fixed_time.hpp"       // FixedTimeController

#include <cstdint>
#include <vector>

namespace tsc {
namespace render {

/// Kayan noktali 2B nokta. sim-core sadece Vec2i (tamsayi) tutar; ama arac
/// konumu serit uzerinde KAYAN NOKTA mesafedir, yani cizimde ara konum
/// gerekir. Bu tip yalnizca render katmanindadir; sim-core'a dokunulmaz.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

/// Render katmaninin izgara senaryosu (uc kavsakli T ag).
///
/// Bu, experiments/bench'in buildScenarioGrid fonksiyonunun birebir aynisidir.
/// Cogaltildi cunku experiments/ bir kutuphane hedefi DEGIL (executabledir)
//   ve kural gereği experiments/ dosyalarina DOKUNULMAZ. Cizim/demo
/// iskeletidir; asil izgara editoru (IG-1) bunun yerini alacak.
Grid buildRenderGrid();

class SimScene {
public:
    /// Kurar ve world'u verilen tohumla baslatir.
    /// `stepsPerSecond`: GERCEK saniyede kac simulasyon adimi atilacagi.
    ///   1.0 = gercek zamanli, 2.0 = 2x hizli.
    SimScene(std::uint64_t seed, int stepsPerSecond);

    /// Gecen GERCEK sureyi biriktirir ve gereken kadar simulasyon adimi atar.
    /// `dtSeconds` kareler arasi gercek suredir (deltaTime).
    void advance(double dtSeconds);

    /// Simulasyon durdu mu (tum araclar vardi ya da zaman asimi/tikanma)?
    bool stopped() const;

    // --- Cizim icin OKUMA erisimi (asla yazma) -------------------------------
    const World& world() const noexcept { return world_; }
    const Network& network() const noexcept { return net_; }

    int stepsPerSecond() const noexcept { return stepsPerSecond_; }
    void setStepsPerSecond(int n);

    /// Bir serit uzerinde `distMeters` mesafesindeki noktanin IZGARA koordinati.
    /// Cizim piksele cevirmeden once bunu kullanir. Serit disina tasarsa
    /// sinirlar (clamped).
    Vec2d lanePointAt(LaneId lane, double distMeters) const;

private:
    void stepOnce();

    Network net_;
    World world_;
    SignalDriver driver_;

    int stepsPerSecond_ = 1;
    double accumulator_ = 0.0;   ///< toplanan simulasyon-saniyesi (0 <= x < 1)

    /// Bir karede atilacak azami adim (spiral of death onlemi).
    static constexpr int kMaxStepsPerFrame = 8;
};

}  // namespace render
}  // namespace tsc
