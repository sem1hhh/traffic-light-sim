#pragma once

#include "tsc/controller.hpp"
#include "tsc/intersection.hpp"

namespace tsc {

// ---------------------------------------------------------------------------
// Kural tabanli dinamik kontrol (EK REFERANS)
///
/// Basit ve seffaf kural:
///   "Simdiki fazin kuyrugu ESIGIN ALTINDA ve sıradaki fazin kuyrugu
///    ESIGIN USTUNDE ise gec."
///
/// Gercek hayattaki en yaygin basit yaklasimdir. Bu yuzden Q-learning'in
/// GECMESI GEREKEN referans budur (rapor: "Kural tabanli dinamik kontrolle
/// arasindaki farkin daha kucuk olabilecegi ongorulmektedir").
///
/// Q-learning ile ayni bilgiyi (kuyruk duzeyleri) kullanir; tek fark,
// ozel bir kural yerine deger fonksiyonu ogrenmesidir.
///
/// !! AZAMI YESIL KURALI ZORUNLUDUR !!
/// Esik kurali TEK BASINA bir "değirmen" uretir: kuyruk hicbir zaman esigi
/// asmadigi surece faz DEGISTIRILMEZ, yolcular bir faza hapsolur. Ilk
/// kosuda sabit zamanli kontrolcuye %5 KAYBEDEN kural kontrolcusu goruldu;
/// sebebi buydu. Gercek sistemlerde de bir azami yesil suresi vardir; onsuz
/// kurulan referans, Q-learning'i karsilastirmak icin ADALETSIZ bir tabandir.
// ---------------------------------------------------------------------------
class ThresholdRuleController : public IController {
public:
    struct Params {
        int switchThreshold = 1;      ///< alti: mevcut faz "bos" sayilir
        int urgentThreshold = 4;      ///< ustu: sıradaki faz "acik" sayilir
        int pressureMargin = 3;       ///< kuyruk farki bu kadarı büyükse gec

        /// Azami yesil suresi. 0 ise DEGER KULLANILMAZ; bunun yerine
        /// kavsagin kendi `maxGreen` suresinin `maxGreenFactor` katı uygulanir.
        ///
        /// NEDEN BÖYLE: Azami yesil, iki katmanin ayni kurali olmasidir —
        /// Intersection'ta da vardir ve orada ZORUNLUDUR. Kontrolcu 25 s,
        /// Intersection 20 s ise kavsak 20'de gecer ve kontrolcunun kurali
        /// HICBIR KOSULDA TETIKLENMEZ; olcumde "azami yesilli esik
        /// kontrolcusu" denmesi yanlislik olurdu. (Faz 5'te boyle bir
        /// parametre vardi ve bench kosularinda olu koddu.)
        ///
        /// Fakturun 1'in altinda olmasi bilincli: gecis kararini kontrolcu
        /// vermelidir, Intersection'in zorlamasina birakilmamalidir.
        double maxGreen = 0.0;
        double maxGreenFactor = 0.85;
    };

    ThresholdRuleController() = default;
    explicit ThresholdRuleController(Params params) : params_(params) {}

    void onReset() override { cagri_ = 0; }

    Action decide(const SignalObservation& obs) override;

    const Params& params() const noexcept { return params_; }

    /// Bu kavsak icin gecerli azami yesil suresi. KAMU alaninda: kural
    /// katsayinin ne anlama geldigi disaridan da gorunmelidir.
    double effectiveMaxGreen(const SignalObservation& obs) const;

    std::uint64_t callCount() const noexcept { return cagri_; }

private:
    /// Sıradaki (diger eksende) kolun kuyrugu.
    /// Faz 0 = {K,G}, Faz 1 = {D,B}. Sıradaki faz, diger eksenin kuyrugudur.
    static int nextPhaseQueue(const SignalObservation& obs);

    Params params_;
    std::uint64_t cagri_ = 0;
};

}  // namespace tsc
