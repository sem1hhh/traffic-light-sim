#include "stats.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <cmath>

using tsc::formatP;
using tsc::pairedTTest;
using tsc::studentTCdf;
using tsc::tCritical95;

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

TEST_CASE("tCritical95 bilinen tablo degerlerini verir") {
    // Guven araligi normal kuantili ile DEGIL, Student t ile hesaplaniyor
    // (BULGU 3). Bu bilinen degerler, quantile'ın kendisini dogrular.
    CHECK_NEAR_T(tCritical95(1), 12.706205, 1e-4);   // Cauchy
    CHECK_NEAR_T(tCritical95(2), 4.302653, 1e-4);
    CHECK_NEAR_T(tCritical95(4), 2.776445, 1e-4);    // n=5 -> df=4
    CHECK_NEAR_T(tCritical95(29), 2.045230, 1e-4);   // n=30
    CHECK_NEAR_T(tCritical95(149), 1.975999, 1e-4);  // n=150
}

TEST_CASE("t kuantili, normal kuantilden GENISTIR — aralik artik dar degil") {
    // Eski kurgu araligi normal kuantili (1.9599) ile hesapliyordu.
    // Dogru kritik deger her zaman daha BUYUKTUR; yani eski aralik her
    // zaman DAR idi. n=5'te fark en belirgin.
    CHECK_GT(tCritical95(4), 1.9599);
    CHECK_GT(tCritical95(29), 1.9599);
    // n=5'te ~%41 daha genis (2.776 / 1.96 = 1.416)
    CHECK_NEAR_T(tCritical95(4) / 1.959963984540054, 1.416, 0.01);
}

TEST_CASE("studentTQuantile, studentTCdf ile TERSIDIR") {
    // Buldugumuz kritik deger, cdf ile geri kontrol edilebilmeli.
    for (const std::size_t df : {2u, 5u, 30u, 100u}) {
        CAPTURE(df);
        const double t = tCritical95(df);
        CHECK_NEAR_T(studentTCdf(t, df), 0.975, 1e-9);
    }
}

TEST_CASE("SIFIR VARYANS: istatistik TANIMDIR — anlamli iddia kurulamaz") {
    // Her fark tam olarak ayni: orneklem varyansi 0, yani varyans TAHMINI
    // yok. Eski kurgu burada tStat = 1.0e6 yazip "cok anlamlı" basiyordu;
    // bu sihirli sayi, sayisal bir artefakti da anlamli gosterebilirdi.
    //
    // Artik: t, p ve guven araligi NaN'dir ve `degenerate` bayragi kurulur.
    const std::vector<double> a{10.0, 10.0, 10.0, 10.0};
    const std::vector<double> b{12.0, 12.0, 12.0, 12.0};
    const auto r = pairedTTest(a, b);

    CHECK_NEAR_T(r.meanDiff, -2.0, 1e-12);   // fark gorunur
    CHECK_NEAR_T(r.sdDiff, 0.0, 1e-12);
    CHECK(r.degenerate);
    CHECK(std::isnan(r.tStat));
    CHECK(std::isnan(r.pTwoSided));
    CHECK(std::isnan(r.ciLow));
    CHECK(std::isnan(r.ciHigh));
    // "Fark yok" DIYEMEZ — varyans tahmini olmadan hicbir iddia kurulamaz.
    CHECK_FALSE(r.significant());
    // Ama "fark yok" da degildir: fark gorunur, sadece olcum YAPILAMAZ.
    CHECK_NE(r.meanDiff, 0.0);
}

TEST_CASE("Degenerate durumda raporlayici 'anlamli' DIYEMEZ, 'n/a' BASAR") {
    // "Sifir fark VE sifir varyans": fark da 0, varyans da 0.
    const std::vector<double> x{5.0, 5.0, 5.0, 5.0};
    const auto r = pairedTTest(x, x);
    CHECK_NEAR_T(r.meanDiff, 0.0, 1e-12);
    CHECK(r.degenerate);
    CHECK_FALSE(r.significant());
    CHECK_EQ(formatP(r.pTwoSided), "n/a");   // 0.000 ya da <0.001 DEGIL
}

TEST_CASE("Sifirdan kucuk ama tutarli fark: anlamli") {
    // VARYANSLI veri: farklarin tumu ayni DEGIL. (Tum farklarin ayni oldugu
    // veride varyans 0 olur ve istatistik tanimsizlasir — bu ayri testte
    // belgelenmistir. Buradaki amac, GERCEKTEN anlamlı bir farkin hala
    // "anlamli" dondugunu kanitlamak.)
    const std::vector<double> a{10.0, 10.2, 9.8, 10.1, 9.9, 10.15, 9.85};
    const std::vector<double> b{12.0, 12.2, 11.8, 12.1, 11.9, 12.05, 11.95};
    const auto r = pairedTTest(a, b);
    CHECK_LT(r.meanDiff, 0.0);          // A daha iyi (kucuk sure)
    CHECK_FALSE(r.degenerate);          // varyans VAR
    CHECK(r.significant());
    CHECK_LT(r.pTwoSided, 0.001);
    // Guven araligi da tumu negatif (tum aralik sifirin altinda)
    CHECK_LT(r.ciHigh, 0.0);
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

TEST_CASE("Guven araligi t kritigi ile hesaplanir — normal kuantiliyle DEGIL") {
    // BULGU 3'un cekirdek iddiasi. Ayni orneklem uzerinde ESKI (z) ve YENI (t)
    // araliklarini hesaplayip, verilen degerin t olanla ES oldugunu ve z
    // olanla FARKLI oldugunu dogruluyoruz. Boylece "t'ye gectik" sozune
    // kod uzerunden de gercekten konulmus olur.
    const std::vector<double> a{10.0, 12.0, 9.0, 13.0, 11.0};
    const std::vector<double> b{11.0, 13.5, 9.5, 14.0, 12.5};
    const auto r = pairedTTest(a, b);
    REQUIRE_EQ(r.n, 5u);
    REQUIRE_EQ(r.df, 4u);

    const double se = r.stdError;
    const double tCrit = tCritical95(4);
    const double zCrit = 1.959963984540054;   // eski (normal) kritik

    CHECK_NEAR_T(r.ciLow, r.meanDiff - tCrit * se, 1e-12);
    CHECK_NEAR_T(r.ciHigh, r.meanDiff + tCrit * se, 1e-12);
    // Eski aralikla BIREBIR AYNI olmamali — degisiklik gorunur olmali.
    CHECK(r.ciHigh > r.meanDiff + zCrit * se);
    // n=5'te ~%41 daha genis.
    CHECK_NEAR_T((r.ciHigh - r.ciLow) / (2.0 * zCrit * se), 1.416, 0.01);
}
