#pragma once

#include "tsc/controller.hpp"
#include "tsc/intersection.hpp"
#include "tsc/types.hpp"
#include "tsc/world.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Kontrolcu surucusu
//
// World kontrolcu KURMAZ; World saf bir simulasyon durumudur. Kararlar
// disaridan verilir. Bu ayrim sayesinde:
//   - World kopyalanabilir ve deterministik kalir (snapshot, tekrarlanabilirlik)
//   - Ayni World, farkli kontrolculerle ayni kosularda calistirilabilir
//     (K3: eslestirilmis karsilastirma)
//
// Kullanim:
//   for (...) {
//       surucu.step();      // once karar ver, sonra dunyayi ilerlet
//   }
// ---------------------------------------------------------------------------

class SignalDriver {
public:
    /// Her kavsak icin ayri kontrolcu uretir (faz sayaci kavsaga ozgudur).
    using ControllerFactory = std::function<std::unique_ptr<IController>(JunctionId)>;

    SignalDriver(World& world, ControllerFactory factory);

    /// Bir saniyelik adim: tum kavsaklarda karar ver, sonra World'i ilerlet.
    void step();

    /// Bir kavsagin kontrolcusu (yaratma sirasinda resetlenir).
    IController& controllerAt(JunctionId j);
    const IController& controllerAt(JunctionId j) const;

    /// Toplam gecis istegi sayaci (ret edilenler dahil degildir).
    std::uint64_t switchRequests() const noexcept { return switchRequests_; }

    /// Odul geri bildirimi yapilan adim sayisi (odulle ogrenen kontrolcuer
    /// icin). Bir kontrolcu odul almiyorsa bu sayac artmaz.
    std::uint64_t rewardCallbacks() const noexcept { return rewardCallbacks_; }

    std::size_t controllerCount() const noexcept { return controllers_.size(); }

private:
    struct Entry {
        JunctionId junction;
        std::unique_ptr<IController> controller;
    };

    /// Bir adimda alinan karar ve GERCEKTEN uygulanan eylem.
    struct ActionDecision {
        JunctionId junction;
        Action executed;
    };
    std::vector<ActionDecision> kararlar_;

    World* world_;
    std::vector<Entry> controllers_;
    std::uint64_t switchRequests_ = 0;
    std::uint64_t rewardCallbacks_ = 0;
};

}  // namespace tsc