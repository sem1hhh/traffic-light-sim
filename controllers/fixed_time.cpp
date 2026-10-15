#include "fixed_time.hpp"

#include <algorithm>
#include <stdexcept>

namespace tsc {

void FixedTimeController::onReset() {
    elapsed_ = 0.0;
    haveTiming_ = false;
}

double FixedTimeController::cycleSeconds(const std::vector<double>& flowPerPhase) const {
    return websterCycle(flowPerPhase, params_.lostTime, params_.yellowPerPhase,
                        params_.saturationFlow);
}

std::vector<double> FixedTimeController::greenSplit(const std::vector<double>& flowPerPhase) const {
    const double c = cycleSeconds(flowPerPhase);
    return websterGreenSplit(flowPerPhase, c, params_.lostTime, params_.yellowPerPhase);
}

Action FixedTimeController::decide(const SignalObservation& obs) {
    junction_ = obs.junction;

    // Webster zamanlamasini BIR KEZ hesapla. GIRDI o anki kuyruk DEGIL,
    // onceden belirlenmis tasarim akimidir: sabit zamanli kontrol plani
    // trafige bakmaz. Kuyruktan okunursa kosu t=0'da akis sifir cikar ve
    // plan anlamsizlasir.
    if (!haveTiming_) {
        try {
            const std::vector<double> akis{params_.plannedFlowA, params_.plannedFlowB};
            const auto pay = greenSplit(akis);
            greenTime_ = pay.at(0);
            phaseOneTime_ = pay.at(1);
        } catch (const std::exception&) {
            // Tasarim akisi sifir/gecersizse guvenli varsayilan.
            greenTime_ = obs.timing.maxGreen;
            phaseOneTime_ = obs.timing.maxGreen;
        }
        haveTiming_ = true;
        elapsed_ = 0.0;
        return Action::Stay;
    }

    elapsed_ += 1.0;   // 1 adim = 1 saniye (IG-2)

    // Bu fazin yesil suresi doldu -> gec. Kuyruga BAKMAZ: sabit zamanli budur.
    const double hedef = (obs.phase == 0) ? greenTime_ : phaseOneTime_;
    if (elapsed_ < hedef) {
        return Action::Stay;
    }

    // ISTEK REDDEDILIRSE sayaci GERI AL. Aksi halde sayac ilerlemeye devam
    // eder ve kontrolcu olmadigi yerde gecis ister (Intersection minGreen
    // veya sarı nedeniyle reddedebilir).
    elapsed_ -= 1.0;
    if (obs.canSwitch() && obs.greenElapsed >= obs.timing.minGreen) {
        elapsed_ = 0.0;
        return Action::Switch;
    }
    return Action::Stay;
}

}  // namespace tsc
