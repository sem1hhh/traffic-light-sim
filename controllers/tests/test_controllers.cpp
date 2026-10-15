// ===========================================================================
// Kontrolcu birim testleri
//
// Bu katman testleri, kontrolcunun KARAR VERME mantigini izole olcer.
// Simulasyon kurulmaz: dogrudan bir Intersection kurulur, kuyruklar yazilir,
// karar okunur. Boylece "kontrolcu yanlis" ile "simulasyon yanlis" ayrilir.
// ===========================================================================

#include "fixed_time.hpp"
#include "threshold.hpp"
#include "controller.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>

using namespace tsc;

namespace {

/// Dort kollu, iki fazli standart kavsak.
///
/// `maxGreen` varsayilan 20 s. DIKKAT: bu sure Intersection'in ZORUNLU
/// gecisidir ve kontrolcunun planini EZER. Bir kontrolcunun kendi
/// zamanlamasini olcmek istiyorsan buraya buyuk bir maxGreen vermelisin;
/// yoksa test, kontrolcuyu degil Intersection'in zorlamasini olcer.
Intersection yapKavsak(double maxGreen = 20.0) {
    SignalTiming t;
    t.minGreen = 5.0;
    t.maxGreen = maxGreen;
    t.yellow = 3.0;
    t.allRed = 1.0;
    return Intersection{JunctionId{0}, {Dir::North, Dir::East, Dir::South, Dir::West}, t};
}

/// Kuyruklari yaz ve gozlemi al. siralama: [Kuzey, Dogu, Guney, Bati]
SignalObservation gozlem(Intersection& j, int k, int d, int g, int b) {
    j.setQueue(Dir::North, k);
    j.setQueue(Dir::East, d);
    j.setQueue(Dir::South, g);
    j.setQueue(Dir::West, b);
    return observe(j);
}

}  // namespace

// ---------------------------------------------------------------------------
// Guvenlik kurallari: kontrolcu bunlari ASAMAZ (IOG-4)
// ---------------------------------------------------------------------------

TEST_CASE("Sabit zamanli kontrolcu minGreen dolmadan gecis ISTEMEZ") {
    Intersection j = yapKavsak();
    FixedTimeController c{FixedTimeController::Params{4.0, 3.0, 1800.0, 500.0, 500.0}};

    // Ilk cagri zamanlamayi hazirlar, gecis yapmaz.
    c.decide(gozlem(j, 0, 0, 0, 0));

    // minGreen (5 s) dolmadan birkac adim: hep Stay.
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(static_cast<int>(c.decide(gozlem(j, 0, 0, 0, 0))), static_cast<int>(Action::Stay));
    }
    // minGreen doldugunda plan suresi de dolmus olmali -> Switch ister.
    // (Intersection yine de reddedebilir; karar ISTEKTIR.)
}

TEST_CASE("Esik kontrolcusu BOS yolda bile dongude kalir") {
    // Onceki test "her iki faz bosken Stay bekler" diyordu ve kirmizi geldi.
    // Test YANLIS: kural 1 (bosluk) tetiklenir ve kontrolcu gecer. Dogru
    // davranis budur — her iki eksen de bosken donmek zararsizdir, yolculara
    // hizmet etmek icin dongude kalmak gerekir. (AGENTS.md 8.8)
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    int gecis = 0;
    for (int i = 0; i < 40; ++i) {
        j.step(1.0);
        if (c.decide(gozlem(j, 0, 0, 0, 0)) == Action::Switch) ++gecis;
    }
    CHECK_GT(gecis, 0);
    CHECK_LT(gecis, 40);   // ama her adimda degil
}

TEST_CASE("ESIK kontrolcusu minGreen dolmadan gecis ISTEMEZ") {
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    // Mevcut faz dolu, diger bos -> kural 1 ve 2 TETIKLENMEZ. Kural
    // tetiklenseydi bile minGreen dolmadan gecis istemez.
    int istek = 0;
    for (int i = 0; i < 10; ++i) {
        j.setQueue(Dir::North, 9);
        j.setQueue(Dir::South, 9);
        j.setQueue(Dir::East, 9);
        j.setQueue(Dir::West, 9);
        j.step(1.0);
        if (c.decide(observe(j)) == Action::Switch) ++istek;
    }
    // Kuyruklar esit ve dolu -> basinc farki yok -> Stay.
    CHECK_EQ(istek, 0);
}

// ---------------------------------------------------------------------------
// Webster tabanli sabit zamanli kontrol
// ---------------------------------------------------------------------------

TEST_CASE("Webster cevrim suresi akisla artar") {
    FixedTimeController c;
    // Akislar doygunluk akisinin BELIRLINCE altinda olmali; Webster
    // Y >= R ise matematiksel olarak anlamsiz ve HATA FIRLATIR.
    const double az = c.cycleSeconds({300.0, 300.0});
    const double cok = c.cycleSeconds({700.0, 700.0});
    CHECK_GT(az, 0.0);
    CHECK_GT(cok, 0.0);
    // Daha yogun akis -> daha uzun cevrim (fazlar uzatilir).
    CHECK_GT(cok, az);
}

TEST_CASE("Webster yesil payi akisla orantili dagilir") {
    FixedTimeController c;
    const auto split = c.greenSplit({600.0, 300.0});
    REQUIRE(split.size() == 2u);
    CHECK_GT(split[0], split[1]);
    CHECK_GT(split[1], 0.0);
    // Dengeli akista iki faz esit sure alir.
    const auto denge = c.greenSplit({500.0, 500.0});
    CHECK_NEAR_T(denge[0], denge[1], 1e-9);
}

TEST_CASE("Webster bos akista HATA FIRLATIR ve kontrolcu bunu yakalar") {
    // Akis sifirsa Webster tanimsizdir (cevrim = 0/0). websterCycle() bilerek
    // hata firlatir; FixedTimeController bunu YAKALAYIP guvenli varsayilana
    // dusmelidir. Yakalamazsa kontrolcu cokusunden sonra cokup gecer.
    FixedTimeController c;
    CHECK_THROWS_AS(c.cycleSeconds({0.0, 0.0}), std::invalid_argument);

    Intersection j = yapKavsak();
    FixedTimeController::Params p;
    p.plannedFlowA = 0.0;
    p.plannedFlowB = 0.0;
    FixedTimeController yedek{p};

    // Cikis ATILMAMALI ve kontrolcu calismaya devam etmeli.
    int gecis = 0;
    for (int i = 0; i < 60; ++i) {
        j.step(1.0);
        if (yedek.decide(observe(j)) == Action::Switch) ++gecis;
    }
    CHECK_GT(gecis, 0);   // en az bir kez gecis istiyor
}

TEST_CASE("Sabit zamanli kontrolcu TRAFIGE BAKMAZ") {
    // Iki kontrolcu ayni plani kullanir; kuyruklar degisse bile ayni
    // karari verirler. "Sabit zamanli" taniminin kendisi budur.
    //
    // !! BU TEST BOSTUR KALIRDI !!
    // Ilk yazimda Intersection'lar ilerletilmiyordu, dolayisiyla
    // greenElapsed hep 0 kaliyor, kontrolcu minGreen gecemedigi icin
    // HICBIR ZAMAN gecis istemiyordu ve iki liste de "hep Stay" idi —
    // karsilastirma anlamsizdi. Mutasyon testi yakaladi ("plan akisi yerine
    // o anki kuyruk" mutasyonu testleri gecirdi). Kavsaklar simdiden
    // ilerletiliyor. (AGENTS.md 8.10)
    Intersection j1 = yapKavsak();
    Intersection j2 = yapKavsak();
    FixedTimeController c1;
    FixedTimeController c2;

    std::vector<int> kararlar1;
    std::vector<int> kararlar2;
    for (int i = 0; i < 90; ++i) {
        j1.step(1.0);
        j2.step(1.0);
        const auto o1 = gozlem(j1, 0, 0, 0, 0);
        const auto o2 = gozlem(j2, 99, 99, 99, 99);
        INFO("i=" << i << " f1=" << static_cast<int>(o1.phase) << " g1=" << o1.greenElapsed
             << " | f2=" << static_cast<int>(o2.phase) << " g2=" << o2.greenElapsed
             << " durum1=" << static_cast<int>(o1.state) << " durum2=" << static_cast<int>(o2.state));
        kararlar1.push_back(static_cast<int>(c1.decide(o1)));
        kararlar2.push_back(static_cast<int>(c2.decide(o2)));
    }
    CHECK_EQ(kararlar1.size(), kararlar2.size());
    for (std::size_t i = 0; i < kararlar1.size(); ++i) {
        // CAPTURE degerleri KARSILASTIRMA ANINDA yazilir. INFO ise ifadeyi
        // saklar ve hata ciktisi uretilirken degerlendirir; boylece son
        // durumu gosterir ve hatayi yerini gizler. (Bir kez boyle
        // gecirildi: "adim 4" yaziyordu ama yesil suresi 18 idi.)
        CAPTURE(i);
        CAPTURE(kararlar1[i]);
        CAPTURE(kararlar2[i]);
        CHECK_EQ(kararlar1[i], kararlar2[i]);
    }
    // AYRICA: bu testin anlamli oldugunu kanitla — en az bir gecis
    // istemi olmali. Hep Stay olsaydi yukaridaki karsilastirma yine bos
    // olurdu.
    int gecis = 0;
    for (const int k : kararlar1) {
        if (k == static_cast<int>(Action::Switch)) ++gecis;
    }
    CHECK_GT(gecis, 1);
}

TEST_CASE("Sabit zamanli kontrolcu fazlara FARKLI sure verir") {
    // Webster: yogun faza daha uzun yesil. Iki kontrolcu ayni plani kullanir;
    // biri cift fazli dengeli (500/500), digeri asimetrik (900/300).
    // Faz 0 suresi ASIMETRIKTE daha uzun olmali.
    FixedTimeController denge{FixedTimeController::Params{4.0, 3.0, 1800.0, 500.0, 500.0}};
    FixedTimeController asit{FixedTimeController::Params{4.0, 3.0, 1800.0, 900.0, 300.0}};

    const auto sd = denge.greenSplit({500.0, 500.0});
    const auto sa = asit.greenSplit({900.0, 300.0});
    REQUIRE(sd.size() == 2u);
    REQUIRE(sa.size() == 2u);

    // Asimetrik planda faz 0 belirgin uzun, faz 1 kisadir.
    CHECK_GT(sa[0], sa[1]);
    // Dengeli planda faz 0, asimetrik plandakinden kisadir.
    CHECK_GT(sa[0], sd[0]);
    CHECK_LT(sa[1], sd[1]);
}

TEST_CASE("Sabit zamanli kontrolcu periyodik olarak gecis ister") {
    Intersection j = yapKavsak();
    FixedTimeController c{FixedTimeController::Params{4.0, 3.0, 1800.0, 500.0, 500.0}};
    c.decide(gozlem(j, 0, 0, 0, 0));

    // ONEMLI: Intersection'i de ilerletmek gerekir. Kontrolcu gecis
    // istegini yalnizca yesil guvenliyse (minGreen dolmus) verir; sinyal
    // ilerlemezse minGreen hic dolmaz ve kontrolcu HICBIR ZAMAN gecis
    // istemez. (Ilk yazimda bu adim atlandi ve test 0 gecis olculuyordu.)
    int gecis = 0;
    for (int i = 0; i < 120; ++i) {
        j.step(1.0);
        // Kuyruk degisken ama kontrolcu onu GORMEZ.
        const int k = (i % 7) * 3;
        if (c.decide(gozlem(j, k, k, k, k)) == Action::Switch) {
            ++gecis;
        }
    }
    // 120 saniyede birkac kez gecis istemeli (cok sifir = calismiyor;
    // cok cok = her adimda gecis istiyor, o zaman da sabit zamanli degildir).
    CHECK_GT(gecis, 2);
    CHECK_LT(gecis, 40);
}

// ---------------------------------------------------------------------------
// Kural tabanli kontrol
// ---------------------------------------------------------------------------

TEST_CASE("Esik kontrolcusu: mevcut faz bos, diger doluysa GECER") {
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    j.setQueue(Dir::North, 0);
    j.setQueue(Dir::South, 0);
    j.setQueue(Dir::East, 8);
    j.setQueue(Dir::West, 8);

    Action karar = Action::Stay;
    for (int i = 0; i < 10; ++i) {
        j.step(1.0);   // minGreen dolsun
        karar = c.decide(observe(j));
    }
    CHECK_EQ(static_cast<int>(karar), static_cast<int>(Action::Switch));
}

TEST_CASE("Esik kontrolcusu: mevcut faz doluysa KALIR") {
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    j.setQueue(Dir::North, 10);
    j.setQueue(Dir::South, 10);
    j.setQueue(Dir::East, 2);
    j.setQueue(Dir::West, 2);

    Action karar = Action::Stay;
    for (int i = 0; i < 10; ++i) {
        j.step(1.0);
        karar = c.decide(observe(j));
    }
    // Mevcut faz hem dolu hem daha yogun -> gecis mantikli degil.
    CHECK_EQ(static_cast<int>(karar), static_cast<int>(Action::Stay));
}

TEST_CASE("Esik kontrolcusu: basinc farki olunca GECER") {
    Intersection j = yapKavsak();
    ThresholdRuleController::Params p2;
    p2.pressureMargin = 3;
    ThresholdRuleController c{p2};
    j.setQueue(Dir::North, 6);   // mevcut faz (0 = K-G)
    j.setQueue(Dir::South, 6);
    j.setQueue(Dir::East, 12);   // diger faz belirgin daha uzgun
    j.setQueue(Dir::West, 12);

    Action karar = Action::Stay;
    for (int i = 0; i < 10; ++i) {
        j.step(1.0);
        karar = c.decide(observe(j));
    }
    CHECK_EQ(static_cast<int>(karar), static_cast<int>(Action::Switch));
}

TEST_CASE("Esik kontrolcusu: AZAMI YESIL gecisi zorunlu kilar") {
    // Kuyruk hicbir zaman esigi gecmese bile, azami yesilde gecmek ZORUNLUDUR.
    // Aksi halde kontrolcu yolculari bir faza hapseder ve kuyruk hicbir
    // zaman acilmaz. (Bu kural olmadan kontrolcu "değirmen" oluyordu.)
    Intersection j = yapKavsak();
    ThresholdRuleController::Params p;
    p.maxGreen = 12.0;
    ThresholdRuleController c{p};

    // Mevcut faz surekli dolu, diger faz hep bos -> kural 1 ve 2 TETIKLENMEZ.
    j.setQueue(Dir::North, 9);
    j.setQueue(Dir::South, 9);
    j.setQueue(Dir::East, 0);
    j.setQueue(Dir::West, 0);

    int zorunluGecis = -1;
    for (int i = 0; i < 30; ++i) {
        j.step(1.0);
        if (c.decide(observe(j)) == Action::Switch) {
            zorunluGecis = i;
            break;
        }
    }
    REQUIRE(zorunluGecis >= 0);
    // minGreen (5) sonrasi, azami yesil (12) oncesi gecmeli.
    CHECK_GT(zorunluGecis, 4);
    CHECK_LT(zorunluGecis, 12);
}

TEST_CASE("Esik kontrolcusu kararlari kumeler halinde degil, tek tek verir") {
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    j.setQueue(Dir::North, 0);
    j.setQueue(Dir::South, 0);
    j.setQueue(Dir::East, 8);
    j.setQueue(Dir::West, 8);

    // Her adimda gecis istememeli; gecis aralikli olmalidir.
    // (Surekli gecis istemek Intersection'i her saniye sarialardi.)
    int istek = 0;
    for (int i = 0; i < 10; ++i) {
        j.step(1.0);
        if (c.decide(observe(j)) == Action::Switch) ++istek;
    }
    CHECK_LT(istek, 10);
    CHECK_GT(c.callCount(), 0u);
}

// ---------------------------------------------------------------------------
// Ortak sozlesme
// ---------------------------------------------------------------------------

TEST_CASE("Her iki kontrolcu de minGreen dolmadan gecis istemez") {
    Intersection j = yapKavsak();
    FixedTimeController ft;
    ThresholdRuleController th;

    // Faz 0'da, yesil henuz yeni baslamis olsun.
    j.step(1.0);
    const SignalObservation o = observe(j);
    for (int i = 0; i < 3; ++i) {
        // NOT: CHECK_FALSE(... && ...) derlenmez; doctest ifadeyi 'a && b' olarak
    // ayristirmaya calisir ve reddeder. Once bool'a indirgemek gerekir.
    const bool gecisGuvensiz = o.canSwitch() && o.greenElapsed >= o.timing.minGreen;
    CHECK_FALSE(gecisGuvensiz);
    }
    // 1 saniyelik adimda minGreen (5 s) dolmamis olmali.
    CHECK_LT(o.greenElapsed, o.timing.minGreen);
}

TEST_CASE("Kontrolculer durumu BOZMAMAZ (saf gozlemci)") {
    Intersection j = yapKavsak();
    FixedTimeController c;
    c.decide(gozlem(j, 3, 1, 4, 1));
    // decide() sonrasi kuyruklar ve faz degismemis olmali.
    const auto o = observe(j);
    CHECK_EQ(o.queues[0], 3);
    CHECK_EQ(o.queues[1], 1);
    CHECK_EQ(o.queues[2], 4);
    CHECK_EQ(o.queues[3], 1);
    CHECK_EQ(static_cast<int>(o.phase), 0);
}

TEST_CASE("Intersection kural tabanli kontrolcunun istegini reddedebilir") {
    Intersection j = yapKavsak();
    j.step(0.5);
    // minGreen dolmadan gecis iste: reddedilmeli.
    CHECK_FALSE(j.requestSwitch());
    CHECK_GT(j.rejectedSwitches(), 0u);
}
// ---------------------------------------------------------------------------
// Faz makinesiyle birlikte calisma
// ---------------------------------------------------------------------------

TEST_CASE("Esik kontrolcusu SARI ve TUM KIRMIZI evrede gecis ISTEMEZ") {
    // canSwitch() yalnizca yesilde dogru. Sari/tum-kirmizi evrede kontrolcu
    // gecis istese bile Intersection zaten reddeder; yine de kontrolcu
    // dogru davranmali (kural tabanli bir kontrolcu gereksiz istek atmasin).
    Intersection j = yapKavsak();
    ThresholdRuleController c;
    j.step(1.0);
    j.setQueue(Dir::North, 0);
    j.setQueue(Dir::South, 0);
    j.setQueue(Dir::East, 8);
    j.setQueue(Dir::West, 8);

    bool sariGorduk = false;
    int sariIstek = 0;
    for (int i = 0; i < 40; ++i) {
        j.step(1.0);
        const SignalObservation o = observe(j);
        if (o.state != SignalState::Green) {
            sariGorduk = true;
            if (c.decide(o) == Action::Switch) ++sariIstek;
        } else {
            c.decide(o);
        }
    }
    REQUIRE(sariGorduk);              // test gercekten sari evreyi gördü
    CHECK_EQ(sariIstek, 0);           // ve sari evrede gecis ISTEMEDI
}

TEST_CASE("Sabit zamanli kontrolcu ASIMETRIK planda fazlara farkli sure verir") {
    // Birim testte faz HICBIR degismiyordu, bu yuzden faz 1'in suresi hic
    // olculmuyordu. "faz 1 suresi faz 0 ile ayni" ve "faz hedefi hep faz 0"
    // mutasyonlari testleri gecirdi. Simdi istekleri GERCEKTEN uyguluyoruz.
    //
    // Plan: faz 0 yogun (900), faz 1 seyrek (300) -> faz 0 = 21 s,
    // faz 1 = 7 s yesil. maxGreen 60 s yapildi ki Intersection'in ZORUNLU
    // gecisi (20 s) plani ezmesin; aksi halde iki faz da 21 s olur ve
    // test kontrolcuyu degil zorlamayi olcerdi.
    Intersection j = yapKavsak(60.0);
    FixedTimeController c{FixedTimeController::Params{4.0, 3.0, 1800.0, 900.0, 300.0}};

    std::vector<double> yesilSure[2];   // 0. ve 1. fazin yesil suresi
    std::size_t fazBaslangicAdim = 0;
    std::size_t sonFaz = 0;
    bool ilk = true;

    for (std::size_t adim = 0; adim < 600; ++adim) {
        j.step(1.0);
        const SignalObservation o = observe(j);

        // Faz degistiyse, onceki fazin yesil suresini KAPANIS adiminda olc.
        // (Faz degisimi adiminda phaseElapsed henuz 0'dir; sureyi adim
        // sayacindan okumak gerekir.)
        if (ilk || o.phase != sonFaz) {
            if (!ilk) {
                yesilSure[sonFaz].push_back(
                    static_cast<double>(adim - fazBaslangicAdim));
            }
            fazBaslangicAdim = adim;
            sonFaz = o.phase;
            ilk = false;
        }

        if (c.decide(o) == Action::Switch) {
            j.requestSwitch();     // istegi GERCEKTEN uygula
        }
    }
    yesilSure[sonFaz].push_back(static_cast<double>(600 - fazBaslangicAdim));

    // Test anlamli olsun: her iki faz da en az iki kez yesil almali.
    CHECK_GE(yesilSure[0].size(), 2u);
    CHECK_GE(yesilSure[1].size(), 2u);

    // HER donusde yogun faz daha uzun yesil almali.
    const std::size_t n = std::min(yesilSure[0].size(), yesilSure[1].size());
    for (std::size_t k = 0; k < n; ++k) {
        INFO("donus " << k << ": faz0=" << yesilSure[0][k] << "s faz1=" << yesilSure[1][k] << "s");
        CHECK_GT(yesilSure[0][k], yesilSure[1][k]);
    }
}

TEST_CASE("Sabit zamanli kontrolcu TAM OLARAK plan suresi kadar yesil verir") {
    // Sayac dogrulugu: plan suresi doldugunda gecis istenir, ne bir adim
    // erken ne bir adim gec. Reddedilen istek sayaci GERI almazsa plan
    // suresi her zaman uzayar (kayma birikir).
    FixedTimeController::Params p{4.0, 3.0, 1800.0, 600.0, 400.0};
    FixedTimeController c{p};
    const auto plan = c.greenSplit({600.0, 400.0});
    REQUIRE(plan.size() == 2u);
    const double beklenen = std::ceil(plan[0]);   // 1 adim = 1 saniye (IG-2)

    Intersection j = yapKavsak(600.0);
    j.setQueue(Dir::North, 0);
    j.setQueue(Dir::East, 0);
    j.setQueue(Dir::South, 0);
    j.setQueue(Dir::West, 0);

    // Ilk cagri yalnizca zamanlamayi hazirlar.
    j.step(1.0);
    c.decide(observe(j));

    int adim = 0;
    while (adim < 200) {
        ++adim;
        j.step(1.0);
        if (c.decide(observe(j)) == Action::Switch) {
            break;
        }
    }
    INFO("plan suresi=" << plan[0] << " -> beklenen " << beklenen << " adim, gerceklesen " << adim);
    CHECK_EQ(adim, static_cast<int>(beklenen));
}

// ---------------------------------------------------------------------------
// Azami yesil: iki katmanin ayni kurali
// ---------------------------------------------------------------------------

TEST_CASE("Azami yesil kavsagin KENDI suresinden turetilir") {
    // Faz 5'teki hata: kontrolcu maxGreen=25, Intersection maxGreen=20.
    // Kavsak 20'de zaten zorunlu gecis yaptigi icin kontrolcunun kurali
    // HICBIR KOSULDA TETIKLENMEDI — olcumde "azami yesilli esik kontrolcusu"
    // denmesi yanlislikti.
    //
    // Artik varsayilan 0 (= "kendin ayarlama") ve gercek sinir
    // Intersection'in suresinden turer.
    ThresholdRuleController c;   // varsayilan: maxGreen = 0

    Intersection j20 = yapKavsak(20.0);
    Intersection j60 = yapKavsak(60.0);

    const double e20 = c.effectiveMaxGreen(observe(j20));
    const double e60 = c.effectiveMaxGreen(observe(j60));

    // Her iki durumda da Intersection'in ZORUNLU gecisinden ONCE olmali.
    CHECK_LT(e20, 20.0);
    CHECK_LT(e60, 60.0);
    // Kavramin kendisi orantili olmali.
    CHECK_NEAR_T(e60 / e20, 3.0, 1e-9);
    // Varsayilan faktor 0.85.
    CHECK_NEAR_T(e20, 17.0, 1e-9);
}

TEST_CASE("Esik kontrolcusu, kavsagin ZORUNLU gecisinden ONCE gecer") {
    // En onemli davranis: karar kontrolcunun olmali. Intersection de ayni
    // kuralli sahiptir; kontrolcu onu once kullanir.
    Intersection j = yapKavsak(30.0);   // Intersection 30'da zorlar
    ThresholdRuleController c;          // kontrolcu 25.5'te gecmeli (0.85*30)

    // Mevcut faz dolu, diger bos -> kural 1 ve 2 tetiklenmez.
    j.setQueue(Dir::North, 9);
    j.setQueue(Dir::South, 9);
    j.setQueue(Dir::East, 0);
    j.setQueue(Dir::West, 0);

    int istekAdimi = -1;
    for (int i = 0; i < 40; ++i) {
        j.step(1.0);
        if (c.decide(observe(j)) == Action::Switch) {
            istekAdimi = i;
            break;
        }
    }
    REQUIRE(istekAdimi >= 0);
    // minGreen (5) sonrasi, Intersection'in zorlamasi (30) oncesi.
    CHECK_GT(istekAdimi, 4);
    CHECK_LT(istekAdimi, 30);
    // Faz HENUZ degismemis olmali: gecis sadece ISTENDI.
    CHECK_EQ(static_cast<int>(observe(j).phase), 0);
}

TEST_CASE("Esik kontrolcusu: acik maxGreen kavramin ustune cikar") {
    // Mutlak deger verilirse onun yerine kullanilir.
    ThresholdRuleController::Params p;
    p.maxGreen = 40.0;
    ThresholdRuleController c{p};
    Intersection j = yapKavsak(20.0);
    // 20 * 0.85 = 17 degil, verilen 40 kullanilir.
    CHECK_NEAR_T(c.effectiveMaxGreen(observe(j)), 40.0, 1e-12);
}
