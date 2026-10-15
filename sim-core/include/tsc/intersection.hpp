#pragma once

#include "tsc/grid.hpp"
#include "tsc/types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Sinyal fazlari
//
// Rapor: "Minimum ve maksimum yesil sure ile sari/temizleme suresi
// ogrenilmeyecek, guvenlik kurali olarak simulasyon tarafindan zorlanacaktir."
//
// Yani bu sinif kontrolcuden BAGIMSIZDIR. Kontrolcu yalnizca "gecsem mi?"
// diye ISER; karari Intersection verir. Boylece IOG-4 ve K1 garanti altindadir:
// kontrolcü ne derse dursesin cakisan yesil olusamaz.
//
// Faz dizisi (2 fazli karsilikli kavsak):
//   Yesil -> Sari -> TamKirmizi(temizleme) -> sonraki fazin Yesil'i
//
// Kol gruplari: {Kuzey, Guney} ve {Dogu, Bati}. Her an YALNIZCA bir grup
// yesildir; iki grup ayni anda yesil olamaz.
// ---------------------------------------------------------------------------

enum class SignalState : std::uint8_t { Green = 0, Yellow = 1, AllRed = 2 };

const char* toString(SignalState s) noexcept;

/// Kolun hangi faz grubunda oldugu.
///   Kuzey/Guney -> 0,  Dogu/Bati -> 1
constexpr std::size_t phaseOfDirection(Dir d) noexcept {
    return (d == Dir::North || d == Dir::South) ? 0u : 1u;
}

/// Bir kolun yesil suresi icin faz grubu icindeki indeksi (0 veya 1).
constexpr std::size_t axisOfDirection(Dir d) noexcept {
    return (d == Dir::North || d == Dir::South) ? 0u : 1u;
}

constexpr std::size_t kPhaseCount = 2;

/// Sinyal zamanlama ayarlari. Tamamı simülasyon tarafından zorlanir.
struct SignalTiming {
    double minGreen = 5.0;    ///< bu sure dolmadan gecis YOK
    double maxGreen = 60.0;   ///< bu sureyi asarsa gecis ZORUNLU
    double yellow = 3.0;      ///< sari suresi
    double allRed = 1.0;      ///< temizleme (tum kirmizi bosalma)

    /// Gecerli mi? minGreen <= maxGreen ve sari/temizleme pozitif olmali.
    bool valid() const noexcept;
};

// ---------------------------------------------------------------------------
// Webster formulu (sabit zamanli kontrol icin)
// ---------------------------------------------------------------------------

/// Optimum sabit zamanli cevrim suresi (saniye).
///
///   C = (Y + R) / (1 + Y / (Y/R))      [Webster, Traffic Signal Timing Manual]
///
/// Burada Y: toplam yesil, R: toplam kirmizi, y: birim kayip sure.
/// Girdi olarak faz basina gelis hizlari (arac/saat) verilir; fazlara
/// akan araclara orantilikla yesil dagitilir.
double websterCycle(const std::vector<double>& flowPerPhase,
                    double lostTime = 4.0,
                    double yellowPerPhase = 3.0,
                    double saturationFlow = 1800.0);

/// Webster'a gore faz basina optimum yesil suresi (cycle ile ayni birimde).
std::vector<double> websterGreenSplit(const std::vector<double>& flowPerPhase,
                                      double cycle,
                                      double lostTime,
                                      double yellowPerPhase);

// ---------------------------------------------------------------------------
// Kavsak
// ---------------------------------------------------------------------------

class Intersection {
public:
    Intersection() = default;

    /// arms: kavsagin kollari (North/East/South/West, bos olabilir).
    Intersection(JunctionId id, std::vector<Dir> arms, SignalTiming timing);

    // --- Zaman ---------------------------------------------------------------

    /// Sabit adim. Faz makinesini ilerletir; gecis kararini OTOMATIK verir
    /// (kontrolcu istemisse ya da maxGreen asildiysa).
    void step(double dt);

    // --- Kontrolcu arayuzu (yalnizca ISTEK) ----------------------------------

    /// Kontrolcunun gecis istemi. Guvenli degilse makine reddeder ve
    /// degeri false doner.
    bool requestSwitch();

    /// Bu an gecis guvenli mi? (yesil suresi minGreen'i gecmis olmali)
    bool canSwitch() const;

    // --- Durum ---------------------------------------------------------------

    JunctionId id() const noexcept { return id_; }
    SignalState state() const noexcept { return state_; }
    std::size_t phase() const noexcept { return phase_; }
    double phaseElapsed() const noexcept { return phaseElapsed_; }
    double greenElapsed() const noexcept { return greenElapsed_; }
    const SignalTiming& timing() const noexcept { return timing_; }
    const std::vector<Dir>& arms() const noexcept { return arms_; }

    /// Bu kavsak iki fazli karsilikli kavsak mi? (K/G ve D/B kollari)
    bool isOpposingTwoPhase() const;

    /// Sinyal atanmis kavsak mi? (kollari olan, aktif faz makinesi)
    bool active() const noexcept { return !arms_.empty(); }

    /// Anlik goruntu: tum faz sayaci ve istek bayragi.
    /// Snapshot ve digest (tekrarlanabilirlik) icin gerekli.
    struct State {
        std::size_t phase = 0;
        SignalState state = SignalState::Green;
        double phaseElapsed = 0.0;
        double greenElapsed = 0.0;
        double stateElapsed = 0.0;
        std::uint64_t switchCount = 0;
        std::uint64_t rejectedSwitches = 0;
        std::uint64_t forcedSwitches = 0;
        bool switchRequested = false;
    };

    State snapshotState() const;
    void restoreState(const State& s);

    /// Bir kol (yaklasma yonu) su anda gecmeye izin veriyor mu?
    bool hasGreen(Dir approachDir) const;

    // --- Kuyruklar (rapordaki durum taniminin girdisi) -----------------------

    void setQueue(Dir approachDir, int vehicleCount);
    int queue(Dir approachDir) const;
    std::array<int, kDirCount> queues() const { return queues_; }
    int totalWaiting() const;

    // --- Guvenlik dogrulamasi (K1) -------------------------------------------

    /// Ayni anda yesil olan CELISEN faz grubu sayisi. Guvenli bir kavsakta
    /// daima 0 olmalidir. Kabul kriteri K1 bunu olcer.
    std::size_t conflictingGreenCount() const;

    /// Yesil suresi maxGreen'i asan faz var mi? (olmamali)
    bool anyPhaseOverrun() const;

    std::uint64_t switchCount() const noexcept { return switchCount_; }
    std::uint64_t rejectedSwitches() const noexcept { return rejectedSwitches_; }
    std::uint64_t forcedSwitches() const noexcept { return forcedSwitches_; }

private:
    bool hasArm(Dir d) const;
    void beginPhase(std::size_t phase);
    void advanceState(double dt);

    JunctionId id_{};
    std::vector<Dir> arms_;
    SignalTiming timing_{};
    std::array<int, kDirCount> queues_{};

    std::size_t phase_ = 0;
    SignalState state_ = SignalState::Green;
    double phaseElapsed_ = 0.0;   ///< bulunulan fazda gecen sure
    double greenElapsed_ = 0.0;   ///< yesilin basindan gecen sure
    double stateElapsed_ = 0.0;   ///< sarili/temizleme basindan gecen sure

    std::uint64_t switchCount_ = 0;
    std::uint64_t rejectedSwitches_ = 0;
    std::uint64_t forcedSwitches_ = 0;
    bool switchRequested_ = false;
};

}  // namespace tsc