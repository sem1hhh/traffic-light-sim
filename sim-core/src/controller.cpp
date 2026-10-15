#include "tsc/controller.hpp"

namespace tsc {

int SignalObservation::totalWaiting() const noexcept {
    int toplam = 0;
    for (const int q : queues) {
        toplam += q;
    }
    return toplam;
}

const char* toString(Action a) noexcept {
    switch (a) {
        case Action::Stay:   return "kal";
        case Action::Switch: return "gec";
    }
    return "?";
}

}  // namespace tsc