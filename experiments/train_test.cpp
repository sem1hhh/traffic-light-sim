// ===========================================================================
// Egitim sureci testleri
//
// Buradaki testler, "olcum guvenilir mi" sorusunu yanitlar. Bunlar olcum
// ARACLARINI test eder: dogru sayiyi uretiyorlar mi, dogru sayiyi OLCEMEYE
// calisiyorlar mi? Bir deneyin sonucu ne olursa olsun bu zincirin saglam
// olmasi gerekir.
//
// Uc kutu vardi, ucu de bozuktu:
//   1. Egitim ve degerlendirme tohumlari CAKISIYORDU (veri sizintisi)
//   2. Degerlendirme kosusu POLITIKAYI DEGISTIRIYORDU
//   3. Geri yazmada sadece SON kavsagin ogrenmesi kaliyordu
// ===========================================================================

#include "train.hpp"
#include "stats.hpp"

#include "bench.hpp"
#include "controller.hpp"
#include "q_learning.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <set>

using namespace tsc;
using namespace tsc::bench;

namespace {

/// mergeFrom'un "taze toplayici" sozlesmesini ifade eden tur.
using QLearningCollector = QLearningController;

std::size_t o0_kod(const SignalObservation& o) {
    return QLearningController{}.encode(o);
}

SignalObservation go_(int n) {
    SignalObservation o;
    o.queues = {n, n, n, n};
    o.phase = 0;
    o.greenElapsed = 6.0;
    o.state = SignalState::Green;
    o.timing.minGreen = 5.0;
    o.timing.maxGreen = 20.0;
    return o;
}

Scenario testScenario() {
    Scenario s;
    s.vehicleCount = 30;
    s.spawnInterval = 4.0;
    s.timeLimit = 300.0;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Veri sizintisi
// ---------------------------------------------------------------------------

TEST_CASE("Egitim ve degerlendirme tohum araliklari KESISMEZ") {
    // Eski kurguda: egitim 1..400, degerlendirme 100..249 -> 150 tohumun
    // HEPSI egitimde kullanilmis. Politika o gerceklesimlere asiri uyum
    // sagliyor ve sonuc IYIMSER cikiyor.
    TrainConfig cfg;
    cfg.episodes = 400;
    cfg.firstSeed = 1;
    cfg.evalFirstSeed = 9001;
    cfg.evalSeeds = 5;

    std::set<std::uint64_t> egitim;
    for (int ep = 0; ep < cfg.episodes; ++ep) {
        egitim.insert(cfg.firstSeed + static_cast<std::uint64_t>(ep));
    }
    std::set<std::uint64_t> degerlendirme;
    for (int k = 0; k < cfg.evalSeeds; ++k) {
        degerlendirme.insert(cfg.evalFirstSeed + static_cast<std::uint64_t>(k));
    }

    std::size_t kesisim = 0;
    for (const std::uint64_t s : degerlendirme) {
        if (egitim.count(s) != 0) ++kesisim;
    }
    CHECK_EQ(kesisim, 0u);
    // Araliklar gercekten ayri mi (sadece tesadufen mi)?
    CHECK_GT(cfg.evalFirstSeed, cfg.firstSeed + static_cast<std::uint64_t>(cfg.episodes));
}

TEST_CASE("Varsayilan yapilandirma sizintili DEGIL") {
    // Kullanici hicbir sey vermediginde de sizinti olmamali.
    const TrainConfig varsayilan;
    CHECK_GT(varsayilan.evalFirstSeed,
             varsayilan.firstSeed + static_cast<std::uint64_t>(varsayilan.episodes));
}

// ---------------------------------------------------------------------------
// 2. Degerlendirme politikayi degistirmemeli
// ---------------------------------------------------------------------------

TEST_CASE("Kesif KAPALI bir kosuda Q tablosu YINE DE guncellenir") {
    // Bu bir "dogru calisiyor" testi: eger bu TEST gecmezse
    // degerlendirmeyi dondurma onlemi gereksiz sayilirdi.
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 30;
    cfg.spawnInterval = 4.0;
    cfg.timeLimit = 300.0;
    World w{net, cfg, 7};

    QLearningController::Params p;
    p.epsilon = 0.0;   // kesif KAPALI
    QLearningController model{p};
    SignalDriver d{w, [&model](JunctionId) {
        return std::unique_ptr<IController>(new QLearningController(model));
    }};

    const auto ilkKavsak = net.signalJunctions()[0];
    const double once =
        static_cast<const QLearningController&>(d.controllerAt(ilkKavsak)).meanAbsQ();

    while (!w.finished() && !w.timedOut() && w.time() < 300.0) {
        d.step();
    }

    const auto& c = static_cast<const QLearningController&>(d.controllerAt(ilkKavsak));
    CHECK_GT(d.rewardCallbacks(), 0u);          // odul akisi calisiyor
    CHECK_NE(c.meanAbsQ(), once);              // ve Q guncelleniyor
    CHECK_GT(c.knownStates(), 0u);
}

TEST_CASE("runEpisode writeBack=false politikayi HIC degistirmez") {
    // Duzeltmenin KENDISI. once yazma acik, sonra kapali calistir ve
    // modelin degistigini/degismedigini olc.
    const Scenario sc = testScenario();

    QLearningController::Params p;
    p.epsilon = 0.0;
    QLearningController a{p};
    const double qA0 = a.meanAbsQ();
    (void)runEpisode(sc, a, 9001, true);
    const double qA1 = a.meanAbsQ();
    CHECK_NE(qA1, qA0);          // yazma acikken model degisti

    QLearningController b{p};
    const double qB0 = b.meanAbsQ();
    const std::size_t nB0 = b.knownStates();
    (void)runEpisode(sc, b, 9001, false);
    CHECK_NEAR_T(b.meanAbsQ(), qB0, 1e-15);     // yazma kapaliyken DEGISMEDI
    CHECK_EQ(b.knownStates(), nB0);
}

TEST_CASE("runEpisode writeBack=true kavsaklari BIRLASTIRIR") {
    // Geri yazma "ilk kavsagi al" degil, ORTALAMA olmali. Bunu olcmek icin
    // kavsaklari elle kurup birlestirilmis modelle karsilastiriyoruz.
    //
    // Dogrudan dogrusu: ayni kosuyu iki kez calistirip her kavsagin
    // tablosunu al, sonra bunlarin ortalamasi ile modeli karsilastir.
    const Scenario sc = testScenario();
    const Network net = buildNetwork(buildScenarioGrid(sc));

    QLearningController::Params p;
    p.epsilon = 0.2;
    QLearningController model{p};

    // Ayni kosuyu elle kurup kavsak tablolarini TOPLA.
    WorldConfig cfg;
    cfg.vehicleCount = sc.vehicleCount;
    cfg.spawnInterval = sc.spawnInterval;
    cfg.timeLimit = sc.timeLimit;
    World w{net, cfg, 5};
    SignalDriver d{w, [&model](JunctionId) {
        return std::unique_ptr<IController>(new QLearningController(model));
    }};
    while (!w.finished() && !w.timedOut() && w.time() < sc.timeLimit) {
        d.step();
    }

    QLearningController beklenen;   // sifirlar
    for (const JunctionId j : net.signalJunctions()) {
        beklenen.mergeFrom(
            static_cast<const QLearningController&>(d.controllerAt(j)));
    }
    const double beklenenQ = beklenen.meanAbsQ();

    // Simdi ayni kosuyu runEpisode ile (writeBack=true) tekrarla.
    QLearningController gercek{p};
    (void)runEpisode(sc, gercek, 5, true);

    // Ortalamaya yakin olmali. (Deterministik oldugu icin birebir de
    // olabilir; asil mesele "ilk kavsagin tablosu" degil "ortalamadir".)
    const double ilkKavsakQ =
        static_cast<const QLearningController&>(d.controllerAt(net.signalJunctions()[0]))
            .meanAbsQ();
    CHECK_GT(beklenenQ, 0.0);
    CHECK_NE(gercek.meanAbsQ(), ilkKavsakQ);   // ilk kavsagin tablosu DEGIL
    CHECK_NEAR_T(gercek.meanAbsQ(), beklenenQ, 1e-9);
}

TEST_CASE("Egitim ve degerlendirme tohumlari GERCEKTEN AYRI (fonksiyon uzerinden)") {
    // Onceki test elle sayiyordu; mutasyon testi bunu gecmisti.
    // Simdi train()'in KULLANDIGI fonksiyonlar dogrudan dogrulanir.
    TrainConfig cfg;
    cfg.episodes = 400;
    cfg.firstSeed = 1;
    cfg.evalFirstSeed = 9001;
    cfg.evalSeeds = 5;

    const auto egitim = trainingSeeds(cfg);
    const auto degerlendirme = evaluationSeeds(cfg);

    CHECK_EQ(egitim.size(), 400u);
    CHECK_EQ(degerlendirme.size(), 5u);
    CHECK_EQ(*egitim.begin(), 1u);
    CHECK_EQ(*degerlendirme.begin(), 9001u);

    std::set<std::uint64_t> e(egitim.begin(), egitim.end());
    for (const std::uint64_t s : degerlendirme) {
        CHECK_EQ(e.count(s), 0u);
    }
}

TEST_CASE("train() degerlendirme sirasinda modeli dondurur") {
    // train() icindeki degerlendirme writeBack=false kullanir. Bunu
    // DOLAYLI olarak olcuyoruz: egitim bolum sayisi 1 iken politika,
    // yalnizca TEK egitim kosusundan etkilenmis olmali.
    const Scenario sc = testScenario();
    TrainConfig cfg;
    cfg.episodes = 1;
    cfg.evalSeeds = 3;
    cfg.firstSeed = 101;
    cfg.evalFirstSeed = 9001;

    const TrainResult r = train(sc, cfg, false);
    REQUIRE(r.history.size() == 1u);
    CHECK_GT(r.policy.knownStates(), 0u);
    CHECK_GT(r.policy.meanAbsQ(), 0.0);

    // KARSILASTIRMA: train() icindeki TEK egitim kosusunu elle, dogrudan
    // cagirip ayni modeli elde ediyoruz. Degerlendirme modeli dondurdugu
    // icin bu ikisi BIREBIR ayni olmali.
    //
    // (Onceki deneme train()'i train() ile karsilastiriyordu; ikisi de
    //  degerlendirme yaptigi icin fark yakalanamiyordu. Mutasyon testi
    //  "degerlendirme yine geri yazar" mutasyonunu gecmisti.)
    QLearningController::Params p;
    p.epsilon = cfg.epsilonStart;      // 1. bolumde epsilon = epsilonStart
    QLearningController elle{p};
    (void)runEpisode(sc, elle, cfg.firstSeed, true);

    CHECK_EQ(r.policy.knownStates(), elle.knownStates());
    CHECK_NEAR_T(r.policy.meanAbsQ(), elle.meanAbsQ(), 1e-12);
}

TEST_CASE("Kavsak tablolari FARKLIDIR (birlestirme gerekli)") {
    // Bu, duzeltmenin gerekcesidir. Tablolar ayniysa birlestirmeye
    // gerek yoktur ve "ilk kavsagi al" yeterli olurdu.
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 30;
    cfg.spawnInterval = 4.0;
    cfg.timeLimit = 300.0;
    World w{net, cfg, 3};

    QLearningController::Params p;
    p.epsilon = 0.2;
    QLearningController model{p};
    SignalDriver d{w, [&model](JunctionId) {
        return std::unique_ptr<IController>(new QLearningController(model));
    }};

    while (!w.finished() && !w.timedOut() && w.time() < 300.0) {
        d.step();
    }

    std::vector<double> degerler;
    for (const JunctionId j : net.signalJunctions()) {
        degerler.push_back(
            static_cast<const QLearningController&>(d.controllerAt(j)).meanAbsQ());
    }
    REQUIRE(degerler.size() >= 2u);
    CHECK_GT(degerler[0], 0.0);
    // Her kavsagin ogrenmis olmasi gerekir.
    for (const JunctionId j : net.signalJunctions()) {
        CHECK_GT(static_cast<const QLearningController&>(d.controllerAt(j)).knownStates(),
                 0u);
    }
    // Ve en az biri farkli olmali (hepsi ayni olsaydi birlestirme gerekmezdi).
    bool farkli = false;
    for (std::size_t i = 1; i < degerler.size(); ++i) {
        if (std::abs(degerler[i] - degerler[0]) > 1e-12) farkli = true;
    }
    CHECK(farkli);
}

TEST_CASE("mergeFrom N kaynagin ESIT agirlikli ortalamasini uretir") {
    // Sozlesme: taze bir toplayici uzerinde birlesim. Duz `(t+o)/2`
    // yazilsaydi uc kaynagin agirliklari 1/8, 1/4, 1/2 olurdu — yani
    // "ortalama" son kaynaga agirlig verirdi. (Bu hata yasandi.)
    const auto o = go_(3);
    const std::size_t s = o0_kod(o);

    // Uc kaynak: ayni durum icin BILINEN farkli degerler.
    // setQ kullanilir ki hangi eylemin secildigine bagli olmasin.
    const double degerler[3] = {-2.0, -6.0, -12.0};
    QLearningCollector toplayici;
    for (int i = 0; i < 3; ++i) {
        QLearningCollector k;
        k.setQ(s, Action::Stay, degerler[i]);
        k.setQ(s, Action::Switch, degerler[i] - 1.0);
        toplayici.mergeFrom(k);
    }

    const double beklenen = (-2.0 - 6.0 - 12.0) / 3.0;
    CHECK_NEAR_T(toplayici.q(s, Action::Stay), beklenen, 1e-9);
    CHECK_NEAR_T(toplayici.q(s, Action::Switch), beklenen - 1.0, 1e-9);

    // Iki kaynak verseydi: (-2 + -6)/2 = -4
    QLearningCollector iki;
    for (const double d : {-2.0, -6.0}) {
        QLearningCollector k;
        k.setQ(s, Action::Stay, d);
        iki.mergeFrom(k);
    }
    CHECK_NEAR_T(iki.q(s, Action::Stay), -4.0, 1e-9);
}

TEST_CASE("mergeFrom tek kaynakta toplayiciyi doldurur") {
    QLearningCollector kaynak;
    kaynak.setMinVisits(1);
    const auto o = go_(3);
    Action a = kaynak.decide(o);
    kaynak.onReward(go_(6), a);

    QLearningCollector toplayici;
    toplayici.mergeFrom(kaynak);
    const std::size_t s = kaynak.encode(o);
    CHECK_NEAR_T(toplayici.q(s, a), kaynak.q(s, a), 1e-12);
}

TEST_CASE("mergeFrom bilinen durumu KAYBETMEZ") {
    // YON ONEMLI: birlestirme "bilinmiyor" bilgisiyle ESKISI EZMEMELI.
    // Maskede atama yapilirsa (OR yerine), bir taraf bildigi durumu diger
    // taraf bilmedigi icin KAYBEDER — ve o durum yeniden "tahmin edici"
    // moda duser. Politika sessizce gerilesiye kadar kimse fark etmez.
    QLearningController::Params p;
    p.epsilon = 0.0;
    QLearningController bilen{p};
    QLearningController bilmeyen{p};
    // minVisits = 0 -> kesif HIC calismaz ("0 kez denendi" = yeterince
    // denendi) ve kontrolcu hep ayni eylemi secer; hicbir durum "bilinen"
    // olmaz. 1 gerekiyor.
    bilen.setMinVisits(1);
    bilmeyen.setMinVisits(1);

    const auto o = go_(2);
    // "bilen" bu durumda HER IKI eylemi degerlendirir -> bilinen olur.
    Action a1 = bilen.decide(o);
    bilen.onReward(go_(6), a1);
    Action a2 = bilen.decide(o);
    bilen.onReward(go_(6), a2);
    const std::size_t s = bilen.encode(o);
    REQUIRE(bilen.isKnown(s));

    // "bilmeyen" bu durumu hic gormez -> bilinmemeli.
    (void)bilmeyen.decide(go_(9));
    (void)bilmeyen.onReward(go_(6), bilmeyen.decide(go_(9)));
    CHECK_FALSE(bilmeyen.isKnown(s));

    // Birlestir: bilinen durum KAYBOLMAMALI.
    bilen.mergeFrom(bilmeyen);
    CHECK(bilen.isKnown(s));
}

TEST_CASE("mergeFrom farkli boyutlarda TIKANMAZ") {
    QLearningController a;
    QLearningController::Params p;
    p.epsilon = 0.1;
    QLearningController b{p};
    a.mergeFrom(b);   // boyutlar ayni; cokmemeli
    CHECK_EQ(a.tableSize(), b.tableSize());
}

// ---------------------------------------------------------------------------
// 4. Surec bütünlüğü
// ---------------------------------------------------------------------------

TEST_CASE("train() deterministik: ayni tohum -> ayni politika") {
    const Scenario sc = testScenario();
    TrainConfig cfg;
    cfg.episodes = 5;
    cfg.evalSeeds = 2;

    const TrainResult a = train(sc, cfg, false);
    const TrainResult b = train(sc, cfg, false);

    CHECK_EQ(a.history.size(), b.history.size());
    CHECK_EQ(a.policy.knownStates(), b.policy.knownStates());
    CHECK_NEAR_T(a.policy.meanAbsQ(), b.policy.meanAbsQ(), 1e-15);
    for (std::size_t i = 0; i < a.evalCurve.size(); ++i) {
        CHECK_NEAR_T(a.evalCurve[i], b.evalCurve[i], 1e-12);
    }
}

TEST_CASE("Degerlendirme egrisi SABIT tohumda oldugu icin karsilastirilabilir") {
    // Eger her bolumde farkli degerlendirme tohumu kullanilsaydi, egri
    // taban gurultusunu gosterirdi. Sabit tohum + coklu tohum ORTALAMASI
    // gereklidir.
    const Scenario sc = testScenario();
    TrainConfig cfg;
    cfg.episodes = 4;
    cfg.evalSeeds = 3;

    const TrainResult r = train(sc, cfg, false);
    CHECK_EQ(r.evalCurve.size(), 4u);
    // Degerlendirmede kesif KAPALI olmali (kesif aciksa "politika"
    // degil "rastgele davranis" olculur).
    CHECK_NEAR_T(r.policy.params().epsilon, 0.0, 1e-12);
}

TEST_CASE("Politika dosyasi turun sonunda kesif KAPALI disa birakilir") {
    const Scenario sc = testScenario();
    TrainConfig cfg;
    cfg.episodes = 2;
    cfg.evalSeeds = 2;
    const TrainResult r = train(sc, cfg, false);
    // Kaydedilen politika "ogrenilmis politika" olmali: kesif yok.
    CHECK_NEAR_T(r.policy.params().epsilon, 0.0, 1e-12);
}