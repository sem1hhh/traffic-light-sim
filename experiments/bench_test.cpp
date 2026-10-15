// ===========================================================================
// Deney motoru testleri
//
// stats_test.cpp'in yamadigi yer: kosu motoru (bench.cpp) ve kontrolcu
// surucusu (SignalDriver). Bu ikisi olcum zincirinin TAMAMINI; testi
// olmayan bir olcum araci sessizce yanlis olcer.
//
// Buradaki testler uc seyi hedefler:
//   1. SignalDriver karar -> istek -> Intersection akisini dogru kuruyor mu?
//   2. runOnce/runMany ayni tohumda ayni sonucu uretiyor mu? (K2'nin
//      determinizm maddesi deney katmaninda da gecerlidir)
//   3. Metrikler kosunun gercekten olcmekle olustugunu mu, sifir mu
//      dondugunu mu? (0'a bolme ve bos olcum tuzagi)
// ===========================================================================

#include "bench.hpp"
#include "stats.hpp"

#include "controller.hpp"
#include "fixed_time.hpp"
#include "threshold.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <stdexcept>

using namespace tsc;
using namespace tsc::bench;

namespace {

/// Test senaryosu: kucuk ve hizli, ama kuyruklarin olusabildigi kadar.
Scenario testScenario(int arac = 40, double aralik = 3.0) {
    Scenario s;
    s.vehicleCount = arac;
    s.spawnInterval = aralik;
    s.timeLimit = 600.0;
    return s;
}

ControllerFactory sabitFabrika() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<FixedTimeController>();
    };
}

ControllerFactory kuralFabrika() {
    return [](JunctionId) -> std::unique_ptr<IController> {
        return std::make_unique<ThresholdRuleController>();
    };
}

/// Ortak sayaci paylasan kontrolcu: SignalDriver her kavsaga AYRI nesne
/// verir, ama bu nesneler ayni sayaci paylasir. Boylece "her kavsak goruldu
/// mu" sorusu tek bir yerden sorulabilir.
class PaylasimliSayac {
public:
    void goruldu(JunctionId j) {
        ++cagri;
        kavsaklar.insert(j.value);
    }
    int cagri = 0;
    std::set<std::uint32_t> kavsaklar;
};

class GeriBildirimKontrolcu : public IController {
public:
    explicit GeriBildirimKontrolcu(std::shared_ptr<PaylasimliSayac> s) : paylasimli_(std::move(s)) {}
    void onReset() override {}
    Action decide(const SignalObservation& o) override {
        paylasimli_->goruldu(o.junction);
        return Action::Stay;
    }
    std::shared_ptr<PaylasimliSayac> paylasimli_;
};

/// Hicbir zaman gecis istemeyen kontrolcu: surucunun "istek -> islenme"
/// yolunu izole test etmek icin.
class SessizKontrolcu : public IController {
public:
    Action decide(const SignalObservation&) override {
        ++cagri;
        return Action::Stay;
    }
    void onReset() override { cagri = 0; }
    int cagri = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// SignalDriver
// ---------------------------------------------------------------------------

TEST_CASE("SignalDriver her kavsak icin ayri kontrolcu olusturur") {
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 10;
    cfg.timeLimit = 100.0;
    World w{net, cfg, 1};

    int uretilen = 0;
    SignalDriver d{w, [&uretilen](JunctionId) {
        ++uretilen;
        return std::make_unique<SessizKontrolcu>();
    }};

    CHECK_EQ(uretilen, net.signalJunctions().size());
    CHECK_EQ(d.controllerCount(), net.signalJunctions().size());
    // uretilen sayisi kavsak sayisina ESIT olmali; daha azsa bazi kavsaklar
    // kontrolcusuz kalir ve trafik doner.
    CHECK_GT(net.signalJunctions().size(), 1u);
}

TEST_CASE("SignalDriver kontrolcu olmazsa da calisir (guvenli geri donus)") {
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 5;
    cfg.timeLimit = 50.0;
    World w{net, cfg, 1};

    SignalDriver d{w, [](JunctionId) { return std::unique_ptr<IController>(); }};
    // nullptr kontrolcu: d.step() cokmemeli.
    for (int i = 0; i < 20 && !w.finished(); ++i) {
        d.step();
    }
    CHECK_EQ(d.switchRequests(), 0u);
}

TEST_CASE("Sessiz kontrolcu HICBIR gecis istemez") {
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 30;
    cfg.timeLimit = 300.0;
    World w{net, cfg, 7};
    SignalDriver d{w, [](JunctionId) {
        return std::unique_ptr<IController>(new SessizKontrolcu());
    }};

    for (int i = 0; i < 200 && !w.finished(); ++i) {
        d.step();
    }
    CHECK_EQ(d.switchRequests(), 0u);
    // Kontrolcu gercekten cagrildi mi? Yoksa test yine bos gecer.
    int cagriToplam = 0;
    for (const JunctionId j : net.signalJunctions()) {
        cagriToplam += dynamic_cast<SessizKontrolcu&>(d.controllerAt(j)).cagri;
    }
    CHECK_GT(cagriToplam, 0);
}

TEST_CASE("SignalDriver gecis isteklerini SAYAR") {
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 30;
    cfg.timeLimit = 600.0;
    World w{net, cfg, 11};
    SignalDriver d{w, sabitFabrika()};

    for (int i = 0; i < 300 && !w.finished(); ++i) {
        d.step();
    }
    // Sabit zamanli kontrolcu periyodik gecis istiyor olmali. 300 saniyede
    // sifir istek, surucunun karari kavsağa HIRASMEDIGI anlamina gelir.
    CHECK_GT(d.switchRequests(), 0u);
}

TEST_CASE("SignalDriver HER kavsakta karar ister (tek kavsakta degil)") {
    // Karsi kavsaklarin biri icin karar vermek, trafigi dondurur. Sadece
    // "gecis sayisi sifirdan buyuk" diye bakmak yetmez: surucu yalnizca ilk
    // kavsakta calisiyor olabilirdi ve sayac yine artardi.
    auto paylasilan = std::make_shared<PaylasimliSayac>();

    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    cfg.vehicleCount = 20;
    cfg.timeLimit = 200.0;
    World w{net, cfg, 3};

    SignalDriver d{w, [paylasilan](JunctionId) {
        return std::unique_ptr<IController>(new GeriBildirimKontrolcu(paylasilan));
    }};

    for (int i = 0; i < 100 && !w.finished(); ++i) {
        d.step();
    }

    // Her kavsak gorulmus olmali.
    CHECK_EQ(paylasilan->kavsaklar.size(), net.signalJunctions().size());
    CHECK_GT(paylasilan->cagri, 0u);
}

TEST_CASE("SignalDriver bilinmeyen kavsakta istisna atar") {
    const Network net = buildNetwork(buildScenarioGrid(testScenario()));
    WorldConfig cfg;
    World w{net, cfg, 1};
    SignalDriver d{w, sabitFabrika()};
    CHECK_THROWS_AS(d.controllerAt(JunctionId{9999}), std::out_of_range);
}

// ---------------------------------------------------------------------------
// bench: kosu uretimi
// ---------------------------------------------------------------------------

TEST_CASE("runOnce doldurulmus metrikler dondurur") {
    const RunResult r = runOnce(testScenario(), sabitFabrika(), 1);
    CHECK_EQ(r.seed, 1u);
    CHECK_GT(r.spawned, 0);
    CHECK_GT(r.arrived, 0);
    CHECK_GT(r.averageTravelTime, 0.0);
    CHECK_GT(r.simSeconds, 0.0);
    CHECK_GT(r.averageSystemCount, 0.0);
    CHECK_GE(r.maxQueue, 0);
    CHECK_GT(r.switchRequests, 0u);
}

TEST_CASE("runOnce bekleme suresini OLCEBILIR (-1 degil)") {
    // averageWaitTime = -1 "olculmedi" demek. Rapor tablosunda bunu
    // ayirt etmek, "0" ile ayni oldugunu sanmaktan onemlidir (§8.5).
    const RunResult r = runOnce(testScenario(), sabitFabrika(), 1);
    CHECK_NE(r.averageWaitTime, -1.0);
    CHECK_GT(r.averageWaitTime, 0.0);
}

TEST_CASE("AYNI tohum -> BIT-BIT ayni sonuc (deney katmaninda determinizm)") {
    const RunResult a = runOnce(testScenario(), sabitFabrika(), 4242);
    const RunResult b = runOnce(testScenario(), sabitFabrika(), 4242);
    CHECK_EQ(a.digest, b.digest);
    CHECK_NEAR_T(a.averageTravelTime, b.averageTravelTime, 0.0);
    CHECK_EQ(a.arrived, b.arrived);
    CHECK_EQ(a.switchRequests, b.switchRequests);
}

TEST_CASE("FARKLI tohum -> FARKLI sonuc (tohum gercekten kullaniliyor)") {
    const RunResult a = runOnce(testScenario(), sabitFabrika(), 1);
    const RunResult b = runOnce(testScenario(), sabitFabrika(), 2);
    CHECK_NE(a.digest, b.digest);
}

TEST_CASE("FARKLI kontrolcu -> FARKLI davranis (gecis sayisi degisir)") {
    const RunResult sabit = runOnce(testScenario(), sabitFabrika(), 5);
    const RunResult kural = runOnce(testScenario(), kuralFabrika(), 5);
    // Her ikisi de calisiyor olmali.
    CHECK_GT(sabit.switchRequests, 0u);
    CHECK_GT(kural.switchRequests, 0u);
    // Kural tabanli kontrolcu kuyruga bakar; ayni kosuda ayni sayida
    // gecis istemesi beklenmez.
    CHECK_NE(sabit.digest, kural.digest);
}

TEST_CASE("BENCH KATMANI: tohum DOGRUDAN World'a ulasir (gercek referans)") {
    // runOnce'in icindeki `World world{net, cfg, seed};` satirinda seed
    // yanlislikla kaydirilirsa ("seed + 1") testler cogu zaman GEcer:
    // iki farkli tohum yine farkli sonuc verir. Bu yuzden runOnce'in
    // ciktisini, ayni seed ile ELLE kurulmus bir World ile karsilastiriyoruz.
    // Bu tek gercek referanstir.
    const Scenario sc = testScenario();
    const RunResult r = runOnce(sc, sabitFabrika(), 77);

    // Ayni senaryoyu elle kur.
    const Network net = buildNetwork(buildScenarioGrid(sc));
    WorldConfig cfg;
    cfg.vehicleCount = sc.vehicleCount;
    cfg.spawnInterval = sc.spawnInterval;
    cfg.timeLimit = sc.timeLimit;
    cfg.maxSpeed = sc.maxSpeed;
    cfg.profile = sc.profile;
    cfg.startHour = sc.startHour;
    cfg.signal.minGreen = sc.minGreen;
    cfg.signal.maxGreen = sc.maxGreen;
    cfg.signal.yellow = sc.yellow;
    cfg.signal.allRed = sc.allRed;

    World elle{net, cfg, 77};
    SignalDriver d{elle, sabitFabrika()};
    while (!elle.finished() && !elle.timedOut() && elle.time() < sc.timeLimit) {
        d.step();
    }

    CHECK_EQ(r.digest, elle.digest());
    CHECK_EQ(r.arrived, elle.metrics().arrived);
    CHECK_NEAR_T(r.averageTravelTime, elle.metrics().averageTravelTime(), 0.0);
    CHECK_EQ(r.switchRequests, d.switchRequests());

    // Tohumu kaydiran mutasyon (seed + 1) BU testi kirmizi yapar.
}

TEST_CASE("Hic carpisma olusmaz (K1 deney katmaninda da gecerli)") {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        const RunResult r = runOnce(testScenario(), sabitFabrika(), seed);
        CHECK_EQ(r.collisions, 0);
    }
}

TEST_CASE("Sifir arac: metrikler cokmez") {
    const RunResult r = runOnce(testScenario(0, 5.0), sabitFabrika(), 1);
    CHECK_EQ(r.spawned, 0);
    CHECK_EQ(r.arrived, 0);
    CHECK_EQ(r.collisions, 0);
    // Ortalama hesaplanamaz ama PATLAMAZ.
    CHECK_NEAR_T(r.averageTravelTime, 0.0, 1e-12);
    CHECK_NEAR_T(r.averageSystemCount, 0.0, 1e-12);
}

TEST_CASE("runMany istenen sayida kosu dondurur ve etiketler") {
    const auto rs = runMany(testScenario(), sabitFabrika(), 4, "benim-etiketim");
    CHECK_EQ(rs.size(), 4u);
    for (const RunResult& r : rs) {
        CHECK_EQ(r.controller, "benim-etiketim");
    }
    // Tohumlar ardisik ve 1'den basliyor.
    std::set<std::uint64_t> tohumlar;
    for (const RunResult& r : rs) {
        tohumlar.insert(r.seed);
    }
    CHECK_EQ(tohumlar.size(), 4u);
}

TEST_CASE("runMany her kosuyu ayri tohumla calistirir (kosular birbirine karismaz)") {
    const auto rs = runMany(testScenario(), sabitFabrika(), 3, "x");
    // Tek tek calistirilana birebir ayni olmali.
    for (const RunResult& r : rs) {
        const RunResult tek = runOnce(testScenario(), sabitFabrika(), r.seed);
        CHECK_EQ(tek.digest, r.digest);
    }
}

// ---------------------------------------------------------------------------
// Senaryo
// ---------------------------------------------------------------------------

TEST_CASE("Standart harita uc kavsak icerir") {
    const Grid g = buildScenarioGrid(testScenario());
    const Network net = buildNetwork(g);
    CHECK_EQ(net.signalJunctions().size(), 3u);
    CHECK_GT(net.laneCount(), 0u);
    // Her kavsakta dort kol olmali. Node::arms bir YON MASKESI (uint8_t),
    // vektor degil: dorduncu bir koldan yoksun kavsak burada yakalanir.
    for (const JunctionId j : net.signalJunctions()) {
        const std::uint8_t maske = net.node(j).arms;
        for (std::size_t d = 0; d < 4; ++d) {
            CAPTURE(d);
            CHECK_NE(maske & (1u << d), 0u);
        }
    }
}

TEST_CASE("Ayni senaryo iki kez kurulunca ayni agi verir") {
    const Scenario s = testScenario();
    CHECK_EQ(buildNetwork(buildScenarioGrid(s)).laneCount(),
             buildNetwork(buildScenarioGrid(s)).laneCount());
}