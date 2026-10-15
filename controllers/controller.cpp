#include "controller.hpp"

#include "tsc/world.hpp"

#include <algorithm>
#include <functional>

namespace tsc {

SignalDriver::SignalDriver(World& world, ControllerFactory factory)
    : world_(&world) {
    for (const JunctionId j : world.network().signalJunctions()) {
        Entry e;
        e.junction = j;
        e.controller = factory(j);
        if (e.controller) {
            e.controller->onReset();
        }
        controllers_.push_back(std::move(e));
    }
}

void SignalDriver::step() {
    if (world_ == nullptr) {
        return;
    }
    // 1) ONCE karar ver. requestSwitch() yalnizca istek birakir; guvenli
    //    degilse Intersection reddeder (IOG-4).
    //
    //    GERCEKTEN UYGULANAN eylem burada belirlenir ve odul cagrisinda
    //    kontrolcuye bildirilir. "Istenen" ile "yapilan" ayni sey degildir:
    //    minGreen, sari veya cakisma nedeniyle istek reddedilebilir.
    //
    // !! Bu blok bir kez YAZILDI AMA UYGULANMADI (python ile yazilan
    //    cift tirnakli metinde "kavsak" / "kavşak" uyusmazligi vardi).
    //    Derleme yine de basariliydi: tanimlanan ama kullanilmayan uyeler
    //    uyari uretmez. Sonuc: onReward HICBIR CAGRILMADI, Q tablosu
    //    bos kaldi ve "Q-learning ogrenmiyor" goruntusu cikti. Kod
    //    okunmadan "derleniyor" diye gecmek bu hatayi gizler.
    kararlar_.clear();
    for (const Entry& e : controllers_) {
        if (!e.controller || !world_->hasSignalAt(e.junction)) {
            continue;
        }
        const SignalObservation obs = observe(world_->intersection(e.junction));
        Action executed = Action::Stay;
        if (e.controller->decide(obs) == Action::Switch) {
            if (world_->intersection(e.junction).requestSwitch()) {
                executed = Action::Switch;
                ++switchRequests_;
            }
        }
        kararlar_.push_back(ActionDecision{e.junction, executed});
    }

    // 2) SONRA dunyayi ilerlet.
    world_->step();

    // 3) Odul: YENI durumu bildir. Odul T aninda hesaplanamaz — kuyruk
    //    ancak T+1'de gorunur.
    for (const ActionDecision& d : kararlar_) {
        for (const Entry& e : controllers_) {
            if (e.junction == d.junction && e.controller) {
                e.controller->onReward(observe(world_->intersection(e.junction)), d.executed);
                ++rewardCallbacks_;
                break;
            }
        }
    }
}

IController& SignalDriver::controllerAt(JunctionId j) {
    for (Entry& e : controllers_) {
        if (e.junction == j) {
            return *e.controller;
        }
    }
    throw std::out_of_range("SignalDriver::controllerAt: bilinmeyen kavsak");
}

const IController& SignalDriver::controllerAt(JunctionId j) const {
    for (const Entry& e : controllers_) {
        if (e.junction == j) {
            return *e.controller;
        }
    }
    throw std::out_of_range("SignalDriver::controllerAt: bilinmeyen kavsak");
}

}  // namespace tsc
