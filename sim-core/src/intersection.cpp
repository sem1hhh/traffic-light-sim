#include "tsc/intersection.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tsc {

const char* toString(SignalState s) noexcept {
    switch (s) {
        case SignalState::Green:   return "yesil";
        case SignalState::Yellow:  return "sari";
        case SignalState::AllRed:  return "tam-kirmizi";
    }
    return "?";
}

bool SignalTiming::valid() const noexcept {
    return minGreen > 0.0 && maxGreen >= minGreen && yellow > 0.0 && allRed >= 0.0;
}

// ---------------------------------------------------------------------------
// Webster (FHWA Traffic Signal Timing Manual)
//
//   C = (L + l) / (1 - Y/S)
//
//     L = cevrim basina kayip sure   (l / faz)
//     l = arasi yesil (intergreen) = sari + tam kirmizi
//     Y = toplam kritik akis (arac/saat)
//     S = doygunluk akisi (arac/saat), varsayilan 1800
//
//   Yesil paylasimi:
//     g_i = (Y_i / Y) * (C - L - l)
// ---------------------------------------------------------------------------

namespace {

double totalFlow(const std::vector<double>& flowPerPhase) {
    double toplam = 0.0;
    for (const double f : flowPerPhase) {
        if (f < 0.0) {
            throw std::invalid_argument("Webster: gecis hizi negatif olamaz");
        }
        toplam += f;
    }
    return toplam;
}

}  // namespace

double websterCycle(const std::vector<double>& flowPerPhase,
                    double lostTime,
                    double yellowPerPhase,
                    double saturationFlow) {
    if (flowPerPhase.empty()) {
        throw std::invalid_argument("Webster: en az bir faz gerekir");
    }
    const double Y = totalFlow(flowPerPhase);
    if (Y <= 0.0) {
        throw std::invalid_argument("Webster: toplam gecis hizi sifir");
    }
    if (saturationFlow <= Y) {
        // Doygunluk akisi toplam akistan kucukse matematiksel olarak anlamsiz.
        throw std::invalid_argument("Webster: doygunluk akisi toplam gecisten kucuk olmali");
    }

    const double L = lostTime * static_cast<double>(flowPerPhase.size());
    const double l = yellowPerPhase * static_cast<double>(flowPerPhase.size());
    return (L + l) / (1.0 - (Y / saturationFlow));
}

std::vector<double> websterGreenSplit(const std::vector<double>& flowPerPhase,
                                      double cycle,
                                      double lostTime,
                                      double yellowPerPhase) {
    if (flowPerPhase.empty()) {
        throw std::invalid_argument("Webster: en az bir faz gerekir");
    }
    const double Y = totalFlow(flowPerPhase);
    const double L = lostTime * static_cast<double>(flowPerPhase.size());
    const double l = yellowPerPhase * static_cast<double>(flowPerPhase.size());

    const double kullanilabilir = cycle - L - l;
    if (kullanilabilir <= 0.0) {
        throw std::invalid_argument("Webster: cevrim, kayip sureler icin yetersiz");
    }

    std::vector<double> green(flowPerPhase.size(), 0.0);
    if (Y <= 0.0) {
        // Akis yoksa esit paylas.
        for (double& g : green) {
            g = kullanilabilir / static_cast<double>(flowPerPhase.size());
        }
        return green;
    }

    for (std::size_t i = 0; i < flowPerPhase.size(); ++i) {
        green[i] = (flowPerPhase[i] / Y) * kullanilabilir;
    }
    return green;
}

// ---------------------------------------------------------------------------
// Intersection
// ---------------------------------------------------------------------------

Intersection::Intersection(JunctionId id, std::vector<Dir> arms, SignalTiming timing)
    : id_(id), arms_(std::move(arms)), timing_(timing) {
    if (!timing_.valid()) {
        throw std::invalid_argument("Intersection: gecersiz sinyal zamanlamasi");
    }
    beginPhase(0);
}

bool Intersection::isOpposingTwoPhase() const {
    const bool kuzeyGunes = hasArm(Dir::North) && hasArm(Dir::South);
    const bool doguBati = hasArm(Dir::East) && hasArm(Dir::West);
    // Iki eksen de tam degilse, en az bir kolda eksik olan kavsak karisilir
    // (or. bir T kavsakta yalnizca Dogu-Bati cifti tamdir).
    return kuzeyGunes && doguBati;
}

void Intersection::beginPhase(std::size_t phase) {
    phase_ = phase % kPhaseCount;
    state_ = SignalState::Green;
    phaseElapsed_ = 0.0;
    greenElapsed_ = 0.0;
    stateElapsed_ = 0.0;
    switchRequested_ = false;
    ++switchCount_;
}

void Intersection::advanceState(double dt) {
    phaseElapsed_ += dt;
    stateElapsed_ += dt;

    switch (state_) {
        case SignalState::Green: {
            greenElapsed_ += dt;
            const bool talepEdildi = switchRequested_;
            const bool zamanAsimi = greenElapsed_ >= timing_.maxGreen;
            const bool zamanDoldu = greenElapsed_ >= timing_.minGreen;

            // Guvenlik kurali: gecis ancak minGreen dolduysa YAPILIR.
            // maxGreen asildiysa gecis zorunludur; kontrolcu ne derse dursesin
            // zaman asimi her zaman gecise yol acar (yoksa araçlar sonsuza
            // kadar bekler).
            const bool gecmeyeIzinVeren = zamanDoldu;
            if (gecmeyeIzinVeren && (talepEdildi || zamanAsimi)) {
                state_ = SignalState::Yellow;
                stateElapsed_ = 0.0;
                if (!talepEdildi) {
                    ++forcedSwitches_;   // kontrolcu istemedi, sure doldu
                }
            } else if (talepEdildi) {
                ++rejectedSwitches_;    // minGreen dolmadi -> reddedildi
            }
            switchRequested_ = false;
            break;
        }

        case SignalState::Yellow: {
            if (stateElapsed_ >= timing_.yellow) {
                state_ = SignalState::AllRed;
                stateElapsed_ = 0.0;
            }
            break;
        }

        case SignalState::AllRed: {
            if (stateElapsed_ >= timing_.allRed) {
                beginPhase(phase_ + 1);   // sonraki faz
            }
            break;
        }
    }
}

void Intersection::step(double dt) { advanceState(dt); }

bool Intersection::requestSwitch() {
    if (state_ != SignalState::Green) {
        ++rejectedSwitches_;   // yesil degilken gecis istenemez
        return false;
    }
    if (greenElapsed_ < timing_.minGreen) {
        ++rejectedSwitches_;
        return false;
    }
    switchRequested_ = true;
    return true;
}

bool Intersection::canSwitch() const {
    return state_ == SignalState::Green && greenElapsed_ >= timing_.minGreen;
}

bool Intersection::hasGreen(Dir approachDir) const {
    if (state_ != SignalState::Green) {
        return false;
    }
    return phaseOfDirection(approachDir) == phase_;
}

bool Intersection::hasArm(Dir d) const {
    return std::find(arms_.begin(), arms_.end(), d) != arms_.end();
}

void Intersection::setQueue(Dir approachDir, int vehicleCount) {
    queues_[static_cast<std::size_t>(approachDir)] = vehicleCount < 0 ? 0 : vehicleCount;
}

int Intersection::queue(Dir approachDir) const {
    return queues_[static_cast<std::size_t>(approachDir)];
}

int Intersection::totalWaiting() const {
    int toplam = 0;
    for (const int q : queues_) {
        toplam += q;
    }
    return toplam;
}

std::size_t Intersection::conflictingGreenCount() const {
    if (state_ != SignalState::Green) {
        return 0;   // yesil yoksa celiski olamaz
    }
    // Yalnizca TEK bir faz grubu yesil olabilir; iki grup ayni anda yesilse
    // celiski sayisi 1'dir.
    const bool eksen0Yesil = phase_ == 0;
    const bool eksen1Yesil = phase_ == 1;
    return (eksen0Yesil && eksen1Yesil) ? 1u : 0u;
}

bool Intersection::anyPhaseOverrun() const {
    return state_ == SignalState::Green && greenElapsed_ > timing_.maxGreen;
}

}  // namespace tsc