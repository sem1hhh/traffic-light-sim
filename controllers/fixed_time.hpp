#pragma once

#include "tsc/controller.hpp"
#include "tsc/intersection.hpp"

namespace tsc {

// ---------------------------------------------------------------------------
// Sabit zamanli kontrol (KONTROL GRUBU)
///
/// Webster ile hesaplanan sabit cevrim suresini kullanir. Trafigi GORMEZ:
// kuyruk bos da olsa, dolu da olsa fazlar sabit surede degisir.
///
/// Rapor: "Sabit zamanli kontrol sistemlerinde cevrim ve faz sureleri onceden
/// belirlenmis oldugundan, gunun belirli saatlerinde aniden olusan trafik
/// dalgalanmalarina yanit verilememektedir."
/// Bu tam olarak bu kontrolcunun zayifligidir ve deneyde gosterilecektir.
//
// Guvenlik kurallari (minYesil/sari/temizleme) Intersection tarafindan
/// zorlanir; bu kontrolcu onlari ASAMAZ.
// ---------------------------------------------------------------------------
class FixedTimeController : public IController {
public:
    /// Webster parametreleri. Varsayilanlar orta olcu bir kavsak icindir.
    struct Params {
        double lostTime = 4.0;         ///< faz basina kayip sure (s)
        double yellowPerPhase = 3.0;    ///< faz basina sari suresi (s)
        double saturationFlow = 1800.0; ///< doygunluk akisi (arac/saat)

        // --- Plan girdisi ---------------------------------------------------
        //
        // Sabit zamanli kontrol ONCEDEN belirlenmis bir plan uygular; o anki
        // trafige BAKMAZ. Bu yuzden plan t=0 anindaki kuyruklardan degil,
        // ONCEDEN BILINEN tasarim akimindan turetilir.
        //
        // (Ilk yazimda akis kuyruklardan okunuyordu. Kosu t=0'da basladigi
        // icin tum kuyruklar 0 idi, Webster hesabi anlamsiz kaldi ve sure
        // minGreen'e dustu: kontrolcu Webster degil, 5 saniyede bir gecen
        // bir kontrolcuydu. Yanlis taban.)
        double plannedFlowA = 500.0;    ///< 0. faz (K-G) tasarim akisi (arac/saat)
        double plannedFlowB = 500.0;    ///< 1. faz (D-B) tasarim akisi (arac/saat)
    };

    FixedTimeController() = default;
    explicit FixedTimeController(Params params) : params_(params) {}

    void onReset() override;

    Action decide(const SignalObservation& obs) override;

    /// Webster cevrim suresi (saniye). Gecis hizina gore degisir.
    double cycleSeconds(const std::vector<double>& flowPerPhase) const;

    /// Faz basina optimum yesil suresi.
    std::vector<double> greenSplit(const std::vector<double>& flowPerPhase) const;

    const Params& params() const noexcept { return params_; }

private:
    Params params_;
    JunctionId junction_{};
    double elapsed_ = 0.0;
    double greenTime_ = 30.0;      ///< 0. faz (K-G) yesil suresi
    double phaseOneTime_ = 30.0;   ///< 1. faz (D-B) yesil suresi
    bool haveTiming_ = false;
};

}  // namespace tsc
