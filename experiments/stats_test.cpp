#include "stats.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <cmath>

using tsc::pairedTTest;
using tsc::studentTCdf;

TEST_CASE("t dagilimi simetrik ve 0'da 0.5") {
    CHECK_NEAR_T(studentTCdf(0.0, 10), 0.5, 1e-9);
    CHECK_NEAR_T(studentTCdf(1.0, 10), 1.0 - studentTCdf(-1.0, 10), 1e-9);
}

TEST_CASE("t dagilimi monoton ARTAN (CDF tanimi)") {
    // BURADA BIR KEZ TEST YANLIS YAZILDI: onceki oturumda "monoton azalan"
    // denmis ve kirmizi geldi. Kumulatif fonksiyonun TANI MI AZALANDIR:
    // P(T <= t) t arttikca ARTAR. Azalan sey hayatta kalma fonksiyonudur.
    // (AGENTS.md §8.8: once "model mi yanlis, test mi yanlis?" sor.)
    double onceki = 0.0;
    for (double t = 0.0; t < 5.0; t += 0.25) {
        const double c = studentTCdf(t, 20);
        CHECK_GT(c, onceki);
        CHECK_LE(c, 1.0);
        onceki = c;
    }
    // Hayatta kalma fonksiyonu AZALIR ve 1 - CDF ile birebir ortusur.
    double hayatta = 1.0;
    for (double t = 0.0; t < 5.0; t += 0.25) {
        const double s2 = 1.0 - studentTCdf(t, 20);
        CHECK_LT(s2, hayatta);
        hayatta = s2;
    }
}

TEST_CASE("Bilinen kritik degerler") {
    // df=30 icin iki kuyruklu p=0.05 olan |t| ~ 2.042
    const double t = 2.042272;
    const double p = 2.0 * (1.0 - studentTCdf(t, 30));
    CHECK_NEAR_T(p, 0.05, 0.001);

    // df=30, |t|=3.0 -> p ~ 0.005
    const double p2 = 2.0 * (1.0 - studentTCdf(3.0, 30));
    CHECK_LT(p2, 0.01);
    CHECK_GT(p2, 0.001);
}

TEST_CASE("df=1 icin Cauchy dagilimi") {
    // Cauchy: P(|T| > 1) = 0.5
    CHECK_NEAR_T(2.0 * (1.0 - studentTCdf(1.0, 1)), 0.5, 1e-6);
}

TEST_CASE("Esit ornekler: fark sifir, anlamli degil") {
    const std::vector<double> a{10, 12, 14, 11, 13};
    const std::vector<double> b{10, 12, 14, 11, 13};
    const auto r = pairedTTest(a, b);
    CHECK_EQ(r.n, 5u);
    CHECK_EQ(r.df, 4u);
    CHECK_NEAR_T(r.meanDiff, 0.0, 1e-12);
    CHECK_NEAR_T(r.tStat, 0.0, 1e-9);
    CHECK_FALSE(r.significant());
}

TEST_CASE("Sifirdan kucuk ama tutarli fark: anlamli") {
    const std::vector<double> a{10.0, 10.2, 9.8, 10.1, 9.9};
    const std::vector<double> b{12.0, 12.2, 11.8, 12.1, 11.9};
    const auto r = pairedTTest(a, b);
    CHECK_LT(r.meanDiff, 0.0);          // A daha iyi (kucuk sure)
    CHECK(r.significant());
    CHECK_LT(r.pTwoSided, 0.001);
}

TEST_CASE("Buyuk ama gurultulu fark: anlamli olmayabilir") {
    const std::vector<double> a{10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0};
    const std::vector<double> b{5.0, 80.0, 12.0, 90.0, 20.0, 75.0, 25.0, 85.0};
    const auto r = pairedTTest(a, b);
    CHECK_GT(std::abs(r.meanDiff), 0.0);
    CHECK_FALSE(r.significant());   // varyans cok buyuk
}

TEST_CASE("Guc araligi ortalamayi icerir ve farkin isaretini korur") {
    // VARYANSLI veri: farklarin hepsi ayni degil. (Tum farklarin ayni
    // oldugu veride sapma 0 olur ve aralik [ort, ort]'a cokur — bu ayri
    // bir testte belgelenmistir.)
    const std::vector<double> a{10.0, 11.5, 9.5, 12.2, 8.8, 13.0, 9.9};
    const std::vector<double> b{12.0, 12.5, 11.5, 13.2, 11.8, 12.9, 12.1};
    const auto r = pairedTTest(a, b);
    CHECK_LT(r.ciLow, r.meanDiff);
    CHECK_GT(r.ciHigh, r.meanDiff);
    CHECK_LT(r.ciHigh, 0.0);       // fark negatif: tum aralik sifirin altinda
    CHECK_LT(r.ciHigh - r.ciLow, 3.0);
}

TEST_CASE("Sifir varyans: guven arligi ortalamaya cokur") {
    // Her fark tam olarak ayni: orneklem varyansi 0. Bu bir HATA DEGILDIR;
    // boyle bir deneyde aralik matematiksel olarak noktadir. p degeri 0'a
    // yaklasir (fark sifirdan farkli oldugu icin).
    const std::vector<double> a{10.0, 10.0, 10.0, 10.0};
    const std::vector<double> b{12.0, 12.0, 12.0, 12.0};
    const auto r = pairedTTest(a, b);
    CHECK_NEAR_T(r.meanDiff, -2.0, 1e-12);
    CHECK_NEAR_T(r.sdDiff, 0.0, 1e-12);
    CHECK_NEAR_T(r.ciLow, -2.0, 1e-12);
    CHECK_NEAR_T(r.ciHigh, -2.0, 1e-12);
    CHECK_LT(r.pTwoSided, 0.001);
}

TEST_CASE("Sifir fark VE sifir varyans: anlamli degil") {
    // Ayni veri: hem fark 0 hem varyans 0. t = 0, p = 1 olmali.
    // (onceki oturumda t = 1e6 atanarak "anlamli" ilan ediliyordu; bu
    // YANLIS: hicbir fark yokken anlamlilik iddiasi uydurmak hatadir.)
    const std::vector<double> x{5.0, 5.0, 5.0, 5.0};
    const auto r = pairedTTest(x, x);
    CHECK_NEAR_T(r.meanDiff, 0.0, 1e-12);
    CHECK_NEAR_T(r.tStat, 0.0, 1e-12);
    CHECK_FALSE(r.significant());
}

TEST_CASE("Ortalamalar raporlanir") {
    const std::vector<double> a{2.0, 4.0, 6.0};
    const std::vector<double> b{1.0, 1.0, 1.0};
    const auto r = pairedTTest(a, b);
    CHECK_NEAR_T(r.meanA, 4.0, 1e-12);
    CHECK_NEAR_T(r.meanB, 1.0, 1e-12);
    CHECK_NEAR_T(r.meanDiff, 3.0, 1e-12);
}

TEST_CASE("describe ortalama ve sapma") {
    const auto d = tsc::describe({2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0});
    CHECK_NEAR_T(d.mean, 5.0, 1e-12);
    CHECK_NEAR_T(d.sd, 2.13808993, 1e-6);   // orneklem sapmasi
    CHECK_EQ(d.n, 8u);
}

TEST_CASE("Girdi dogrulamasi") {
    CHECK_THROWS_AS(pairedTTest({1.0, 2.0}, {1.0}), std::invalid_argument);
    CHECK_THROWS_AS(pairedTTest({1.0}, {2.0}), std::invalid_argument);
    CHECK_THROWS_AS(pairedTTest({}, {}), std::invalid_argument);
}

TEST_CASE("p degeri bicimi") {
    CHECK_EQ(tsc::formatP(0.0001), "<0.001");
    CHECK_EQ(tsc::formatP(0.0421), "0.042");
}
