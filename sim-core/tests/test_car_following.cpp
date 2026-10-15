#include "tsc/car_following.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <cmath>

using tsc::IdmParams;

namespace {
IdmParams normal() { return IdmParams{}; }
}

TEST_CASE("Varsayilan parametreler gecerlidir") {
    CHECK(normal().valid());
}

TEST_CASE("Gecersiz parametreler reddedilir") {
    IdmParams p;
    p.desiredSpeed = 0.0;
    CHECK_FALSE(p.valid());
    p = IdmParams{};
    p.maxAccel = 0.0;
    CHECK_FALSE(p.valid());
    p = IdmParams{};
    p.comfortableDecel = -1.0;
    CHECK_FALSE(p.valid());
    p = IdmParams{};
    p.timeHeadway = 0.0;
    CHECK_FALSE(p.valid());
}

TEST_CASE("Bos yolda araç hedef hiza kadar hizlanir") {
    const IdmParams p = normal();
    // Engel yok -> serbest akis
    const double a = tsc::idmAcceleration(p, 0.0, 1.0e9, 0.0);
    CHECK_GT(a, 0.0);
    CHECK_LE(a, p.maxAccel);
}

TEST_CASE("Hedef hizda ivme sifira yaklasir") {
    const IdmParams p = normal();
    CHECK_LT(tsc::idmFreeAcceleration(p, p.desiredSpeed), 0.01);
    CHECK(std::abs(tsc::idmFreeAcceleration(p, p.desiredSpeed)) < 0.01);
}

TEST_CASE("Hedef hizi asan araç yavaslar") {
    const IdmParams p = normal();
    CHECK_LT(tsc::idmFreeAcceleration(p, p.desiredSpeed * 1.5), 0.0);
}

TEST_CASE("Hareketli engel varken ivme azalir") {
    const IdmParams p = normal();
    const double bosluk = tsc::idmAcceleration(p, 10.0, 1.0e9, 10.0);   // onunde yuksek hizli arac
    const double yakin  = tsc::idmAcceleration(p, 10.0, 20.0, 10.0);   // onunde yavas arac
    CHECK_LT(yakin, bosluk);
}

TEST_CASE("Ondeki arac yavaslayinca ivme negatife doner") {
    const IdmParams p = normal();
    const double a = tsc::idmAcceleration(p, 13.0, 6.0, 0.0);  // ondeki duruyor
    CHECK_LT(a, 0.0);
}

TEST_CASE("Bosluk kapandikça fren gecerlilir") {
    const IdmParams p = normal();
    const double uzakta = tsc::idmAcceleration(p, 13.0, 100.0, 0.0);
    const double yakinda = tsc::idmAcceleration(p, 13.0, 5.0, 0.0);
    CHECK_LT(yakinda, uzakta);
}

TEST_CASE("Sifira yakin bosluk son derece sert fren uretir") {
    const IdmParams p = normal();
    const double a = tsc::idmAcceleration(p, 13.0, 0.0, 0.0);
    CHECK_LT(a, -p.maxAccel * 10.0);
}

TEST_CASE("Negatif girdiler guvenli sekilde islenir") {
    const IdmParams p = normal();
    // Negatif hiz / mesafe patlama yapmamali
    const double a = tsc::idmAcceleration(p, -5.0, -10.0, -3.0);
    CHECK(std::isfinite(a));
}

TEST_CASE("Hiz guncellemesi konforlu frenle sinirla") {
    const IdmParams p = normal();
    // Onunde duran arac, cok yakin
    double v = 13.0;
    for (int i = 0; i < 20; ++i) {
        v = tsc::idmSpeedAfterStep(p, v, 3.0, 0.0, 1.0, p.desiredSpeed);
        REQUIRE(v >= 0.0);
    }
    CHECK_LT(v, 13.0);
    CHECK_GE(v, 0.0);
    // Mesafe SABIT 3.0 m tutuldugu icin araç tam durmaz; IDM denge
    // hizinda biraktir (s0=2.0 < 3.0). Onemli olan: belirgin sekilde yavaslamis.
    CHECK_LT(v, 2.0);
}

TEST_CASE("Bos yolda hiz artar ve hedefi asimaz") {
    const IdmParams p = normal();
    double v = 0.0;
    for (int i = 0; i < 300; ++i) {
        v = tsc::idmSpeedAfterStep(p, v, 1.0e9, 0.0, 1.0, p.desiredSpeed);
        REQUIRE(v >= 0.0);
    }
    CHECK_GT(v, p.desiredSpeed * 0.9);
    CHECK_LE(v, p.desiredSpeed * 1.01);
}

TEST_CASE("Arac durmaya yaklasir ve neredeyse durur") {
    const IdmParams p = normal();
    double v = 13.0;
    for (int i = 0; i < 60; ++i) {
        v = tsc::idmSpeedAfterStep(p, v, 4.0, 0.0, 1.0, p.desiredSpeed);
    }
    // Sabit 4 m mesafede tam durmaz; denge hizina iner.
    CHECK_LT(v, 2.5);
    CHECK_GE(v, 0.0);
}

TEST_CASE("Mesafe kapaninca araç tamamen durur") {
    // Gercek davranis: arac ilerledikce onundeki aracla arasi mesafe
    // kapanir; mesafe s0'a dustugunde hiz sifira iner.
    const IdmParams p = normal();
    double v = 13.0;
    double mesafe = 4.0;
    for (int i = 0; i < 30; ++i) {
        v = tsc::idmSpeedAfterStep(p, v, mesafe, 0.0, 1.0, p.desiredSpeed);
        mesafe -= v;   // ondeki arac sabit duruyor
        if (mesafe < p.minGap) {
            mesafe = p.minGap;
        }
    }
    CHECK_LT(v, 0.5);
    CHECK_GE(v, 0.0);
}

TEST_CASE("clampSpeed konforlu frenle sinirlar") {
    CHECK_EQ(tsc::clampSpeed(10.0, 3.0, 1.0), 7.0);
    CHECK_EQ(tsc::clampSpeed(10.0, 3.0, 10.0), 0.0);
    CHECK_EQ(tsc::clampSpeed(5.0, 3.0, 0.0), 5.0);
    CHECK_EQ(tsc::clampSpeed(5.0, 0.0, 1.0), 5.0);
}

TEST_CASE("Fren mesafesi fiziksel olarak makul") {
    // 50 km/s'ten konforlu frenle (3 m/s^2) durmak ~ (13.9/3)^2/2 = 21.4 m.
    const IdmParams p = normal();
    double v = p.desiredSpeed;
    double mesafe = 0.0;
    double t = 0.0;
    while (v > 0.05 && t < 60.0) {
        const double newV = tsc::clampSpeed(v, p.comfortableDecel, 1.0);
        mesafe += (v + newV) * 0.5 * 1.0;
        v = newV;
        t += 1.0;
    }
    CHECK_GT(mesafe, 15.0);
    CHECK_LT(mesafe, 40.0);
    CHECK_LT(t, 15.0);
}

TEST_CASE("Determinizm: ayni girdi ayni ivmeyi verir") {
    const IdmParams p = normal();
    for (int i = 0; i < 100; ++i) {
        const double gap = 5.0 + static_cast<double>(i);
        CHECK_EQ(tsc::idmAcceleration(p, 8.0, gap, 3.0),
                 tsc::idmAcceleration(p, 8.0, gap, 3.0));
    }
}