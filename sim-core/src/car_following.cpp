#include "tsc/car_following.hpp"

#include <algorithm>
#include <cmath>

namespace tsc {
namespace {

constexpr double kEps = 1e-9;

}  // namespace

bool IdmParams::valid() const noexcept {
    return desiredSpeed > 0.0 && minGap >= 0.0 && timeHeadway > 0.0 &&
           maxAccel > 0.0 && comfortableDecel > 0.0;
}

double idmAcceleration(const IdmParams& p, double speed, double gap, double leadSpeed) {
    if (!p.valid()) {
        return 0.0;
    }

    // Negatif degerler guvenli tarafa cekilir.
    speed = std::max(0.0, speed);
    leadSpeed = std::max(0.0, leadSpeed);
    gap = std::max(0.0, gap);

    // Engel yoksa serbest akis: yalnizca hedef hiza yaklas.
    if (gap > 1.0e6) {
        return idmFreeAcceleration(p, speed);
    }

    const double dv = speed - leadSpeed;

    // Tercih edilen bosluk: s0 + hiz * T + fren payi
    const double frenPayi = (dv > 0.0)
        ? (dv * dv) / (2.0 * std::sqrt(p.maxAccel * p.comfortableDecel))
        : 0.0;
    const double yildiz = p.minGap + speed * p.timeHeadway + frenPayi;

    const double hizTerim = (p.desiredSpeed > kEps) ? std::pow(speed / p.desiredSpeed, 4.0) : 0.0;

    // Gap sifira yakinken terim patlar; ivme sert fren olur.
    const double boslukTerim = (gap > kEps) ? std::pow(yildiz / gap, 2.0) : 1.0e6;

    return p.maxAccel * (1.0 - hizTerim - boslukTerim);
}

double idmFreeAcceleration(const IdmParams& p, double speed) {
    if (!p.valid()) {
        return 0.0;
    }
    speed = std::max(0.0, speed);
    const double r = (p.desiredSpeed > kEps) ? (speed / p.desiredSpeed) : 0.0;
    return p.maxAccel * (1.0 - r * r * r * r);
}

double idmSpeedAfterStep(const IdmParams& p, double speed, double gap,
                         double leadSpeed, double dt, double speedLimit) {
    const double a = idmAcceleration(p, speed, gap, leadSpeed);
    double newSpeed = speed + a * dt;

    // IDM fiziksel olarak olmayan bir frenleme uretebilir; konforlu frenle
    // sinirla (akmak gercekci degildir, garanti de vermez).
    // DIKKAT: bu kirpma SADECE YAVASLAMA sirasinda uygulanir. Her zaman
    // uygulanirsa ivmelenen aracin hizi de dusurulur ve hicbir arac
    // hizlanamaz (bu hata once yapildi, testler yakaladi).
    if (newSpeed < speed) {
        newSpeed = clampSpeed(newSpeed, p.comfortableDecel, dt);
    }

    // Hiz siniri (or. kavsak ici daha dusuk limit): asilamaz.
    if (speedLimit > 0.0) {
        newSpeed = std::min(newSpeed, speedLimit);
    }
    return std::max(0.0, newSpeed);
}

double clampSpeed(double speed, double maxDecel, double dt) {
    if (dt <= 0.0 || maxDecel <= 0.0) {
        return speed;
    }
    const double enFazlaDusus = maxDecel * dt;
    return std::max(speed - enFazlaDusus, 0.0);
}

}  // namespace tsc