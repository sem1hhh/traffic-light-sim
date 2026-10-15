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
    for (const Entry& e : controllers_) {
        if (!e.controller || !world_->hasSignalAt(e.junction)) {
            continue;
        }
        const SignalObservation obs = observe(world_->intersection(e.junction));
        if (e.controller->decide(obs) == Action::Switch) {
            if (world_->intersection(e.junction).requestSwitch()) {
                ++switchRequests_;
            }
        }
    }
    // 2) SONRA dunyayi ilerlet.
    world_->step();
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
