#include "tsc/intersection.hpp"

#include "tsc/controller.hpp"
#include "tsc/rng.hpp"
#include "test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

using tsc::Action;
using tsc::Dir;
using tsc::Intersection;
using tsc::JunctionId;
using tsc::SignalState;
using tsc::SignalTiming;

namespace {

Intersection dortYollu() {
    return Intersection{JunctionId::from(0),
                        {Dir::North, Dir::East, Dir::South, Dir::West},
                        SignalTiming{}};
}

Intersection tKavsak() {
    return Intersection{JunctionId::from(1),
                        {Dir::North, Dir::East, Dir::West},
                        SignalTiming{}};
}

SignalTiming hizli() {
    SignalTiming t;
    t.minGreen = 2.0;
    t.maxGreen = 8.0;
    t.yellow = 2.0;
    t.allRed = 1.0;
    return t;
}

/// Kontrolcunun YAPAMAYACAGI her seyi deneyen kotu niyetli kontrolcu:
/// her adimda gecis ister, kuyruk sayisini umursamaz.
class KotuIyimserKontrolcu : public tsc::IController {
public:
    void onReset() override { cagri = 0; }
    tsc::Action decide(const tsc::SignalObservation&) override {
        ++cagri;
        return Action::Switch;   // her zaman gecmek iste
    }
    int cagri = 0;
};

/// Kuyruk verisine bakan kontrolcu: agir kuyruk varsa kalir.
class EsikKontrolcu : public tsc::IController {
public:
    void onReset() override { cagri = 0; }
    tsc::Action decide(const tsc::SignalObservation& o) override {
        ++cagri;
        return (o.totalWaiting() >= esik) ? Action::Stay : Action::Switch;
    }
    int esik = 3;
    int cagri = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Faz gruplari
// ---------------------------------------------------------------------------

TEST_CASE("Kuzey/Guney ayni fazda, Dogu/Bati diger fazda") {
    CHECK_EQ(tsc::phaseOfDirection(Dir::North), 0u);
    CHECK_EQ(tsc::phaseOfDirection(Dir::South), 0u);
    CHECK_EQ(tsc::phaseOfDirection(Dir::East), 1u);
    CHECK_EQ(tsc::phaseOfDirection(Dir::West), 1u);
}

TEST_CASE("Dort yollu kavsak karsilikli iki fazlidir") {
    CHECK(dortYollu().isOpposingTwoPhase());
}

TEST_CASE("T kavsak yalnizca bir ekseni tamdir") {
    CHECK_FALSE(tKavsak().isOpposingTwoPhase());
}

// ---------------------------------------------------------------------------
// Zamanlama dogrulamasi
// ---------------------------------------------------------------------------

TEST_CASE("Varsayilan zamanlama gecerlidir") {
    CHECK(SignalTiming{}.valid());
}

TEST_CASE("Gecersiz zamanlama reddedilir") {
    SignalTiming t;
    t.minGreen = 0.0;
    CHECK_FALSE(t.valid());
    // CHECK_THROWS_AS makrosu virgulden bolundugu icin kurucu cagrisi
    // bir lambda icine sarilir.
    CHECK_THROWS_AS(([&]{ Intersection{JunctionId::from(0), {Dir::North, Dir::South}, t}; }()),
                    std::invalid_argument);

    SignalTiming u;
    u.minGreen = 30.0;
    u.maxGreen = 10.0;   // maxGreen < minGreen
    CHECK_FALSE(u.valid());
}

TEST_CASE("Yeni kavsak yesil fazla baslar") {
    const Intersection j = dortYollu();
    CHECK(eq(j.state(), SignalState::Green));
    CHECK_EQ(j.phase(), 0u);
    CHECK_EQ(j.greenElapsed(), 0.0);
}

// ---------------------------------------------------------------------------
// GUVENLIK: minGreen zorlanir
// ---------------------------------------------------------------------------

TEST_CASE("MinGreen dolmadan gecis yapilmaz - kontrolcu istese bile") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};

    j.step(1.0);
    CHECK_EQ(j.greenElapsed(), 1.0);
    CHECK_FALSE(j.canSwitch());

    // Kontrolcu ister; makine reddeder.
    CHECK_FALSE(j.requestSwitch());
    CHECK(eq(j.state(), SignalState::Green));
    CHECK_EQ(j.phase(), 0u);
    CHECK_GE(j.rejectedSwitches(), 1u);
}

TEST_CASE("MinGreen dolunca gecis yapilabilir") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    for (int i = 0; i < 2; ++i) {
        j.step(1.0);
    }
    CHECK(j.canSwitch());
    CHECK(j.requestSwitch());
    j.step(1.0);
    CHECK(eq(j.state(), SignalState::Yellow));
}

// ---------------------------------------------------------------------------
// GUVENLIK: maxGreen zorlanir
// ---------------------------------------------------------------------------

TEST_CASE("MaxGreen asilirsa gecis KENDILIĞINDEN olur") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    // Hicbir istek yok.
    for (int i = 0; i < 8; ++i) {
        j.step(1.0);
    }
    // maxGreen = 8 -> 8. saniyede sariya gecmis olmali
    CHECK(eq(j.state(), SignalState::Yellow));
    CHECK_GE(j.forcedSwitches(), 1u);
}

TEST_CASE("MaxGreen asimi kontrolcu istemese de olusur - regresyon") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    for (int i = 0; i < 5; ++i) {
        j.step(1.0);
    }
    CHECK(eq(j.state(), SignalState::Green));
    // 8 saniyeye kadar hicbir istek gonderilmiyor
    for (int i = 0; i < 5; ++i) {
        j.step(1.0);
        CHECK_FALSE(j.anyPhaseOverrun());   // sure asimi asla olmaz
    }
}

// ---------------------------------------------------------------------------
// Faz dizisi: yesil -> sari -> tam kirmizi -> sonraki faz
// ---------------------------------------------------------------------------

TEST_CASE("Faz dizisi dogru sirada ilerler") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};

    // Faz 0 yesil -> sari
    j.step(2.0);
    j.requestSwitch();
    j.step(1.0);
    CHECK(eq(j.state(), SignalState::Yellow));

    // Sari suresi bitince tam kirmizi
    j.step(2.0);
    CHECK(eq(j.state(), SignalState::AllRed));

    // Temizleme suresi bitince sonraki faz
    j.step(1.0);
    CHECK(eq(j.state(), SignalState::Green));
    CHECK_EQ(j.phase(), 1u);
}

TEST_CASE("Yesil olmayan durumda gecis istegi reddedilir") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    j.step(2.0);
    j.requestSwitch();
    j.step(1.0);
    CHECK(eq(j.state(), SignalState::Yellow));
    const std::uint64_t onceki = j.rejectedSwitches();
    CHECK_FALSE(j.requestSwitch());          // sariyken istemek
    CHECK_GE(j.rejectedSwitches(), onceki + 1);
}

// ---------------------------------------------------------------------------
// yesil sorgusu
// ---------------------------------------------------------------------------

TEST_CASE("Kuzey yesilken Guney de yesildir") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    CHECK(j.hasGreen(Dir::North));
    CHECK(j.hasGreen(Dir::South));
    CHECK_FALSE(j.hasGreen(Dir::East));
    CHECK_FALSE(j.hasGreen(Dir::West));
}

TEST_CASE("Sari ve tam kirmizida hicbir kol yesil degildir") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    j.step(2.0);
    j.requestSwitch();
    j.step(1.0);
    CHECK(eq(j.state(), SignalState::Yellow));
    CHECK_FALSE(j.hasGreen(Dir::North));
    CHECK_FALSE(j.hasGreen(Dir::South));
    CHECK_FALSE(j.hasGreen(Dir::East));
}

// ---------------------------------------------------------------------------
// Kuyruklar
// ---------------------------------------------------------------------------

TEST_CASE("Kuyruklar kola gore tutulur") {
    Intersection j = dortYollu();
    j.setQueue(Dir::North, 5);
    j.setQueue(Dir::South, 2);
    j.setQueue(Dir::East, 0);
    j.setQueue(Dir::West, 1);
    CHECK_EQ(j.queue(Dir::North), 5);
    CHECK_EQ(j.totalWaiting(), 8);
}

TEST_CASE("Negatif kuyruk 0 olur") {
    Intersection j = dortYollu();
    j.setQueue(Dir::North, -7);
    CHECK_EQ(j.queue(Dir::North), 0);
}

// ---------------------------------------------------------------------------
// Webster
// ---------------------------------------------------------------------------

TEST_CASE("Webster bos veya hatasiz girdide hesaplanir") {
    const double c = tsc::websterCycle({400.0, 400.0});   // dengeli akis
    CHECK_GT(c, 0.0);
    CHECK_LT(c, 200.0);
}

TEST_CASE("Cevrim suresi TOPLAM akisa baglidir, dagilima degil") {
    // Webster: C = (L + l) / (1 - Y/S). Y toplam kritik akidir.
    // Bu yuzden ayni toplam akida dagilim ne olursa olsun cevrim aynidir;
    // dengesizlik yalnizca YESIL PAYLASIMINI degistirir.
    const double dengeli = tsc::websterCycle({450.0, 450.0});
    const double dengesiz = tsc::websterCycle({700.0, 200.0});
    CHECK_EQ(dengesiz, dengeli);

    const std::vector<double> akisDengeli{450.0, 450.0};
    const std::vector<double> akisDengesiz{700.0, 200.0};
    const auto gDengeli = tsc::websterGreenSplit(akisDengeli, dengeli, 4.0, 3.0);
    const auto gDengesiz = tsc::websterGreenSplit(akisDengesiz, dengesiz, 4.0, 3.0);
    CHECK_EQ(gDengeli[0], gDengeli[1]);      // esit akis -> esit yesil
    CHECK_GT(gDengesiz[0], gDengesiz[1]);    // agir faz daha cok yesil
}

TEST_CASE("Toplam akis artarsa cevrim uzar") {
    const double az = tsc::websterCycle({200.0, 200.0});    // toplam 400
    const double cok = tsc::websterCycle({600.0, 600.0});  // toplam 1200
    CHECK_GT(cok, az);
}

TEST_CASE("Webster yesil paylasimi akisla orantili") {
    const std::vector<double> akis{600.0, 400.0};
    const double c = tsc::websterCycle(akis);
    const auto green = tsc::websterGreenSplit(akis, c, 4.0, 3.0);

    CHECK_EQ(green.size(), 2u);
    CHECK_GT(green[0], green[1]);        // agir faz daha cok yesil alir
    // Paylanan yesil, kayip sureler dusulmus cevrimden kucuk olmali
    CHECK_LT(green[0] + green[1], c);
}

TEST_CASE("Esit akis esit yesil paylasir") {
    const std::vector<double> akis{500.0, 500.0};
    const double c = tsc::websterCycle(akis);
    const auto green = tsc::websterGreenSplit(akis, c, 4.0, 3.0);
    CHECK_EQ(green[0], green[1]);
}

TEST_CASE("Webster hatali girdiyi reddeder") {
    CHECK_THROWS_AS(tsc::websterCycle({}), std::invalid_argument);
    CHECK_THROWS_AS(tsc::websterCycle({0.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(tsc::websterCycle({-100.0, 50.0}), std::invalid_argument);
    // Doygunluk akisi toplam gecisten kucukse anlamsiz
    CHECK_THROWS_AS(tsc::websterCycle({900.0, 900.0}, 4.0, 3.0, 1000.0),
                    std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Kontrolcu arayuzu
// ---------------------------------------------------------------------------

TEST_CASE("Gozlem raporun durum tanimiyla birebir ortusur") {
    Intersection j{JunctionId::from(3), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    j.setQueue(Dir::North, 4);
    j.setQueue(Dir::East, 1);
    j.step(3.0);

    const auto o = tsc::observe(j);
    CHECK(eq(o.junction, JunctionId::from(3)));
    CHECK_EQ(o.phase, j.phase());
    CHECK(eq(o.state, j.state()));
    CHECK_EQ(o.phaseElapsed, j.phaseElapsed());
    CHECK_EQ(o.greenElapsed, j.greenElapsed());
    CHECK_EQ(o.queues[0], 4);            // North
    CHECK_EQ(o.totalWaiting(), 5);
}

TEST_CASE("Kontrolcu arayuzu uygulanabilir ve guvenligi asamaz") {
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    KotuIyimserKontrolcu c;
    c.onReset();

    // Her adim gecmek ister, hicbir kuyruk vermez.
    int kacDefaReddedildi = 0;
    for (int adim = 0; adim < 40; ++adim) {
        const auto o = tsc::observe(j);
        if (c.decide(o) == Action::Switch && !j.requestSwitch()) {
            ++kacDefaReddedildi;
        }
        j.step(1.0);
    }
    // Kontrolcu surekli istedi, makine defalarca reddetti
    CHECK_GE(kacDefaReddedildi, 1);
    CHECK_EQ(j.conflictingGreenCount(), 0u);
    CHECK_FALSE(j.anyPhaseOverrun());
}

// ---------------------------------------------------------------------------
// K1 KABUL KRITERI: 1000 rastgele kosuda cakisan yesil SIFIR
// ---------------------------------------------------------------------------

TEST_CASE("K1: 1000 rastgele kosuda cakisan yesil sayisi sifirdir") {
    int toplamCeliski = 0;
    int toplamZamanAsimi = 0;

    for (int kosu = 0; kosu < 1000; ++kosu) {
        tsc::Rng rng{static_cast<std::uint64_t>(kosu)};

        SignalTiming t;
        t.minGreen = 1.0 + rng.rangeD(0.0, 9.0);
        t.maxGreen = t.minGreen + rng.rangeD(1.0, 40.0);
        t.yellow = rng.rangeD(1.0, 5.0);
        t.allRed = rng.rangeD(0.0, 3.0);
        if (!t.valid()) {
            continue;
        }

        const bool dortYol = (kosu % 2 == 0);
        Intersection j{
            JunctionId::from(static_cast<std::uint32_t>(kosu)),
            dortYol ? std::vector<Dir>{Dir::North, Dir::East, Dir::South, Dir::West}
                    : std::vector<Dir>{Dir::North, Dir::East, Dir::West},
            t};

        // Rastgele kuyruk verisi
        for (const Dir d : tsc::kAllDirs) {
            j.setQueue(d, rng.below(20));
        }

        KotuIyimserKontrolcu kotu;
        kotu.onReset();

        for (int adim = 0; adim < 300; ++adim) {
            // Kontrolcu rastgele karar verir.
            if (rng.chance(0.4)) {
                const auto o = tsc::observe(j);
                if (kotu.decide(o) == Action::Switch) {
                    j.requestSwitch();   // istek, reddedilebilir
                }
            }
            j.step(1.0);

            toplamCeliski += static_cast<int>(j.conflictingGreenCount());
            if (j.anyPhaseOverrun()) {
                ++toplamZamanAsimi;
            }
        }
    }

    CHECK_EQ(toplamCeliski, 0);
    CHECK_EQ(toplamZamanAsimi, 0);
}

TEST_CASE("K1: rastgele kuyruk + esik kontrolcu ile de celiski olusmaz") {
    int toplamCeliski = 0;
    for (int kosu = 0; kosu < 500; ++kosu) {
        tsc::Rng rng{10'000u + static_cast<std::uint64_t>(kosu)};
        Intersection j{JunctionId::from(static_cast<std::uint32_t>(kosu)),
                       {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};

        EsikKontrolcu c;
        c.esik = 1 + static_cast<int>(rng.below(6));
        c.onReset();

        for (int adim = 0; adim < 200; ++adim) {
            for (const Dir d : tsc::kAllDirs) {
                j.setQueue(d, static_cast<int>(rng.below(15)));
            }
            if (c.decide(tsc::observe(j)) == Action::Switch) {
                j.requestSwitch();
            }
            j.step(1.0);
            toplamCeliski += static_cast<int>(j.conflictingGreenCount());
        }
    }
    CHECK_EQ(toplamCeliski, 0);
}

TEST_CASE("Celiski sayaci kavramsal olarak her zaman sifirdir") {
    // Iki fazli karsilikli kavsakta ayni anda yesil olan iki grup OLABILMEZ:
    // faz degisince yesil devreden cikar. Bu test bunu dogrudan dogrular.
    Intersection j{JunctionId::from(0), {Dir::North, Dir::East, Dir::South, Dir::West}, hizli()};
    bool faz0Goruldu = false;
    bool faz1Goruldu = false;
    for (int adim = 0; adim < 100; ++adim) {
        j.step(1.0);
        if (j.state() == SignalState::Green) {
            if (j.phase() == 0) {
                CHECK_FALSE(j.hasGreen(Dir::East));
                CHECK_FALSE(j.hasGreen(Dir::West));
                faz0Goruldu = true;
            } else {
                CHECK_FALSE(j.hasGreen(Dir::North));
                CHECK_FALSE(j.hasGreen(Dir::South));
                faz1Goruldu = true;
            }
        }
    }
    CHECK(faz0Goruldu);
    CHECK(faz1Goruldu);
}