#include "threshold.hpp"

namespace tsc {

int ThresholdRuleController::nextPhaseQueue(const SignalObservation& obs) {
    // Faz gruplari: 0 = {Kuzey, Guney}, 1 = {Dogu, Bati}
    // Siranlanmis dizilim: [North, East, South, West]
    const int diger = obs.phase == 0 ? (obs.queues[1] + obs.queues[3])
                                     : (obs.queues[0] + obs.queues[2]);
    return diger;
}

Action ThresholdRuleController::decide(const SignalObservation& obs) {
    ++cagri_;

    // Guvenlik: yalnizca yesilde ve minYesil dolduysa gecis iste.
    // (Intersection yine de reddedebilir; istek sadece bir temennidir.)
    if (!obs.canSwitch()) {
        return Action::Stay;
    }
    if (obs.greenElapsed < obs.timing.minGreen) {
        return Action::Stay;
    }

    // Azami yesil asildiysa gecis ZORUNLUDUR: kurala bakmaz.
    // (Intersection yine de guvenlik kurallarina uyacaktir.)
    if (obs.greenElapsed >= params_.maxGreen) {
        return Action::Switch;
    }

    const int mevcut = obs.phase == 0 ? (obs.queues[0] + obs.queues[2])
                                      : (obs.queues[1] + obs.queues[3]);
    const int diger = nextPhaseQueue(obs);

    // KURAL 1 — bosluk: mevcut fazda kuyruk yoksa yolu ver.
    // (Trafik basligi yokken yesilde beklemenin anlami yok.)
    if (mevcut < params_.switchThreshold) {
        return Action::Switch;
    }
    // KURAL 2 — basinc farki: diger faz belirgin daha uzgunsa gec.
    //
    // Neden mutlak esik degil de FARK? Ilk yazimda "mevcut < 3 VE diger > 5"
    // yazilmisti; bu kosul cogu zaman yanlislikla SAGLANMIYOR ve kontrolcu
    // yalnizca Intersection'in zorunlu maxGreen gecisine dusuyordu. Yani
    // "kural tabanli" degil, sabit zamanli bir kontrolcuydu — ve sabit
    // zamanlidan DAHA KOTU sonuc veriyordu. FARK kriteri her iki fazi
    // ayni anda karsilastirir ve gercek kural tabanli davranis uretir.
    if (diger >= mevcut + params_.pressureMargin && diger > params_.urgentThreshold) {
        return Action::Switch;
    }
    return Action::Stay;
}

}  // namespace tsc
