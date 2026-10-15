#pragma once

#include "tsc/intersection.hpp"
#include "tsc/types.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>

namespace tsc {

// ---------------------------------------------------------------------------
// Kontrolcu arayuzu
//
// Rapor: "Kontrolcü arayuzu IP4 ile birlikte belirlenecektir."
// Rapor: "Durum; kavsak kollarindaki kuyruk duzeylerinden, o anki fazdan ve
// fazda gecen sureden olusan yerel bilgidir; eylem, mevcut fazi surdurmek
// veya sonraki faza gecmektir."
//
// SignalObservation tam olarak bu durum tanimidir.
// Action ise "kalsin / gecsin" ikilisiyle sinirlidir.
//
// ONEMLI: decide() state MUTASYONU YAPMAZ ve guvenlik kurallarini ASAMAZ.
// Interface yalnizca "gecsem mi?" ister; karari Intersection verir.
// Bu, IOG-4 ve K1'i yapısal olarak garanti eder.
// ---------------------------------------------------------------------------

/// Kontrolcunun gördüğü yerel durum (rapordaki tanimin birebir karsiligi).
struct SignalObservation {
    JunctionId junction{};

    std::size_t phase = 0;              ///< o anki faz
    SignalState state = SignalState::Green;
    double phaseElapsed = 0.0;          ///< fazda gecen sure
    double greenElapsed = 0.0;          ///< yesilin basindan gecen sure

    /// Her kolun kuyrugu. Dizilim sirasi Dir enum sirasiyla ayni:
    /// [North, East, South, West]
    std::array<int, kDirCount> queues{};

    SignalTiming timing{};

    int totalWaiting() const noexcept;
    bool canSwitch() const noexcept { return state == SignalState::Green; }
};

/// Kontrolcunun karari. Yalnizca iki secenek (rapordaki eylem uzayi).
enum class Action : std::uint8_t { Stay = 0, Switch = 1 };

const char* toString(Action a) noexcept;

/// Intersection durumunu kontrolcunun gozlem formatina cevirir.
inline SignalObservation observe(const Intersection& j) {
    SignalObservation o;
    o.junction = j.id();
    o.phase = j.phase();
    o.state = j.state();
    o.phaseElapsed = j.phaseElapsed();
    o.greenElapsed = j.greenElapsed();
    o.queues = j.queues();
    o.timing = j.timing();
    return o;
}

// ---------------------------------------------------------------------------
// Kontrolcu arayuzu
// ---------------------------------------------------------------------------
class IController {
public:
    virtual ~IController() = default;

    /// Bir kosu baslamadan once cagrilir.
    virtual void onReset() = 0;

    /// Mevcut duruma gore karar verir. STATE DEGISTIRMEZ.
    virtual Action decide(const SignalObservation& obs) = 0;

    /// Ozel durumlar icin: model kaydet/yukle (or. Q tablosu). Varsayilan bos.
    virtual void serialize(std::ostream&) const {}
    virtual void deserialize(std::istream&) {}
};

}  // namespace tsc