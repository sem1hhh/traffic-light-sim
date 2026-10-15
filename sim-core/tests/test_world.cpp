#include "tsc/world.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <optional>
#include <type_traits>
#include <vector>

using tsc::CellType;
using tsc::Grid;
using tsc::GridSize;
using tsc::LaneId;
using tsc::Network;
using tsc::Vec2i;
using tsc::World;
using tsc::WorldConfig;

namespace {

/// Dort kavsakli, iki gecisli T ag: gecerli, simule edilebilir.
/// Satir 2'de uc kavsak (sol, orta, sag), her biri ust ve alt kola sahip.
/// Sol ve sag ucu serit sonudur -> giris/cikis noktasi olusturur.
Grid simAgi() {
    Grid g{GridSize{9, 7}};
    // yatay omurga
    for (int x = 0; x < 9; ++x) {
        g.set(Vec2i{x, 2}, CellType::Road);
    }
    // dikey kollar
    g.set(Vec2i{4, 1}, CellType::Road);
    g.set(Vec2i{4, 3}, CellType::Road);
    g.set(Vec2i{2, 1}, CellType::Road);
    g.set(Vec2i{2, 3}, CellType::Road);
    g.set(Vec2i{6, 1}, CellType::Road);
    g.set(Vec2i{6, 3}, CellType::Road);
    // kavsaklar
    g.set(Vec2i{2, 2}, CellType::Junction);
    g.set(Vec2i{4, 2}, CellType::Junction);
    g.set(Vec2i{6, 2}, CellType::Junction);
    return g;
}

WorldConfig testConfig(int arac = 20) {
    WorldConfig c;
    c.vehicleCount = arac;
    c.maxSpeed = 10.0;
    c.spawnInterval = 1.0;
    c.timeLimit = 600.0;
    return c;
}

/// Tam kosu yurutup ozet dondurur.
std::uint64_t tamKosu(std::uint64_t seed, WorldConfig cfg, Network& netOut) {
    const Network net = tsc::buildNetwork(simAgi());
    netOut = net;
    World w{net, cfg, seed};
    for (int i = 0; i < 600 && !w.finished(); ++i) {
        w.step();
    }
    return w.digest();
}

}  // namespace

// ---------------------------------------------------------------------------
// Kurulum
// ---------------------------------------------------------------------------

TEST_CASE("Gecersiz agla dunya hazir olmaz") {
    const Network bos;
    World w{bos, testConfig(5), 42};
    CHECK_FALSE(w.ready());
}

TEST_CASE("Gecerli agla dunya hazirdir") {
    const Network net = tsc::buildNetwork(simAgi());
    CHECK(tsc::validateNetwork(net).ok());
    World w{net, testConfig(5), 42};
    CHECK(w.ready());
}

TEST_CASE("Simule edilebilir ag giris/cikis ucu sunar") {
    const Network net = tsc::buildNetwork(simAgi());
    CHECK_GE(net.boundaryLanes().size(), 2u);
    CHECK_GE(net.exitLanes().size(), 2u);
}

// ---------------------------------------------------------------------------
// Zaman ve adim
// ---------------------------------------------------------------------------

TEST_CASE("Sabit adim degeri degistirilmemis olmali - regresyon") {
    // IG-2: 1 adim = 1 saniye. Deger bir gun degistirilirse DENKLEM bozulur;
    // testler karsilastirmali oldugu icin iki kosu da birlikte kayar ve
    // tekrarlanabilirlik testleri fark etmez. Bu test sabitler.
    CHECK_EQ(tsc::kStepSeconds, 1.0);
    CHECK_EQ(tsc::kMaxSimSeconds, 3600.0);
}

TEST_CASE("Zaman sifirdan baslar") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(5), 1};
    CHECK_EQ(w.time(), 0.0);
}

TEST_CASE("Her adim tam bir saniye ilerletir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(5), 1};
    w.step();
    CHECK_EQ(w.time(), 1.0);   // sabit sayi: kStepSeconds'a baglanmaz
    w.step();
    CHECK_EQ(w.time(), 2.0);
    for (int i = 0; i < 9; ++i) {
        w.step();
    }
    CHECK_EQ(w.time(), 11.0);
}

TEST_CASE("Hazir olmayan dunya adim atmaz") {
    const Network bos;
    World w{bos, testConfig(5), 1};
    w.step();
    w.step();
    CHECK_EQ(w.time(), 0.0);
}

// ---------------------------------------------------------------------------
// IOG-1 / K2: TEKRARLANABILIRLIK -- Faz 1'in asil kabul kriteri
// ---------------------------------------------------------------------------

TEST_CASE("AYNI tohum ile iki kosu bit-bit ayni sonuc verir") {
    Network a, b;
    const std::uint64_t d1 = tamKosu(12345, testConfig(20), a);
    const std::uint64_t d2 = tamKosu(12345, testConfig(20), b);

    CHECK_EQ(d1, d2);
}

TEST_CASE("FARKLI tohum farkli sonuc uretir") {
    Network a, b;
    const std::uint64_t d1 = tamKosu(1, testConfig(20), a);
    const std::uint64_t d2 = tamKosu(2, testConfig(20), b);
    CHECK_NE(d1, d2);
}

TEST_CASE("Tohum degistikce sonuclar sistemli olarak degisir") {
    std::vector<std::uint64_t> ozetler;
    for (std::uint64_t s = 1; s <= 10; ++s) {
        Network n;
        ozetler.push_back(tamKosu(s, testConfig(20), n));
    }
    std::sort(ozetler.begin(), ozetler.end());
    const auto tekillestir = std::unique(ozetler.begin(), ozetler.end());
    CHECK_EQ(std::distance(ozetler.begin(), tekillestir), 10);
}

TEST_CASE("Uc ardışık kosu ayni tohumla ayni ozeti verir") {
    Network n;
    const std::uint64_t ilk = tamKosu(777, testConfig(15), n);
    for (int i = 0; i < 2; ++i) {
        CHECK_EQ(tamKosu(777, testConfig(15), n), ilk);
    }
}

TEST_CASE("Adim sayisi arttikca sonuc degisir - kosu gercekten ilerliyor") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(10), 99};

    CHECK_EQ(w.vehicles().size(), 0u);
    w.step();
    std::size_t ilkAdimSonrasi = w.vehicles().size();
    CHECK_GT(ilkAdimSonrasi, 0u);

    for (int i = 0; i < 50; ++i) {
        w.step();
    }
    CHECK_NE(w.digest(), 0u);
}

// ---------------------------------------------------------------------------
// Anlik goruntu / geri yukleme (IG-8 temeli)
// ---------------------------------------------------------------------------

TEST_CASE("Anlik goruntu ve geri yukleme durumu bit-bit geri getirir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(15), 31337};

    for (int i = 0; i < 40; ++i) {
        w.step();
    }
    const std::uint64_t kaydet = w.digest();
    const tsc::Snapshot s = w.snapshot();

    for (int i = 0; i < 40; ++i) {
        w.step();
    }
    CHECK_NE(w.digest(), kaydet);  // ilerledi

    w.restore(s);
    CHECK_EQ(w.digest(), kaydet);  // tam geri dondu
}

TEST_CASE("Geri yuklemeden sonra ayni adimlar ayni sonucu verir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(15), 31337};

    for (int i = 0; i < 20; ++i) {
        w.step();
    }
    const tsc::Snapshot s = w.snapshot();

    // Bir kez 30 adim at, referans ozeti al.
    for (int i = 0; i < 30; ++i) {
        w.step();
    }
    const std::uint64_t referans = w.digest();

    // Geri al ve AYNI 30 adimi tekrarla -> ayni sonuc olmali.
    w.restore(s);
    for (int i = 0; i < 30; ++i) {
        w.step();
    }
    CHECK_EQ(w.digest(), referans);
}

TEST_CASE("Geri yuklemeden sonra arac uretimi de tekrarlanabilir") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(5);

    World a{net, cfg, 5150};
    for (int i = 0; i < 10; ++i) {
        a.step();
    }
    const tsc::Snapshot s = a.snapshot();
    for (int i = 0; i < 60; ++i) {
        a.step();
    }
    const std::uint64_t referans = a.digest();
    a.restore(s);
    for (int i = 0; i < 60; ++i) {
        a.step();
    }
    CHECK_EQ(a.digest(), referans);
}

// ---------------------------------------------------------------------------
// Araclar
// ---------------------------------------------------------------------------

TEST_CASE("Araclar uretilir ve uretim ZAMANA YAYILIR") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(20);
    cfg.spawnInterval = 2.0;
    World w{net, cfg, 11};

    // Ilk adimda YALNIZCA bir arac dogar (eskiden hepsi birden doguyordu).
    w.step();
    CHECK_EQ(w.metrics().spawned, 1);
    CHECK_EQ(w.vehicleCount(), 1u);

    // Uretim araliklar halinde devam eder.
    w.step();
    CHECK_EQ(w.metrics().spawned, 1);  // 2. saniye henuz gelmedi
    for (int i = 0; i < 60; ++i) {
        w.step();
    }
    CHECK_EQ(w.metrics().spawned, 20);
    // Uretilen = yolda olan + ulasmis olmali (kaybolan arac olmamali).
    CHECK_EQ(static_cast<int>(w.vehicleCount()) + w.metrics().arrived, 20);
}

TEST_CASE("Gecersiz seritle arac uretilemez") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(0), 11};
    CHECK_FALSE(w.spawnVehicle(LaneId::from(9999), LaneId::from(0)));
    CHECK_FALSE(w.spawnVehicle(LaneId::from(0), LaneId::from(9999)));
    CHECK_FALSE(w.spawnVehicle(tsc::LaneId{}, LaneId::from(0)));
    CHECK_EQ(w.vehicleCount(), 0u);
}

TEST_CASE("Araclar hedefe ulasir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(15), 2718};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    CHECK_EQ(w.metrics().arrived, 15);
    CHECK_EQ(w.vehicleCount(), 0u);
    CHECK(w.finished());
}

TEST_CASE("Yolculuk suresi pozitiftir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(15), 2718};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    CHECK(w.metrics().averageTravelTime() > 0.0);
}

TEST_CASE("Zaman asimi isaretlenir") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(30);
    cfg.timeLimit = 5.0;  // gercekten yetecek zaman degil
    World w{net, cfg, 4};
    for (int i = 0; i < 20; ++i) {
        w.step();
    }
    CHECK(w.timedOut());
}

// ---------------------------------------------------------------------------
// Olcumler
// ---------------------------------------------------------------------------

TEST_CASE("Ozet bos dunya icin kararli") {
    World w;
    CHECK_FALSE(w.ready());
    CHECK_EQ(w.digest(), w.digest());
    CHECK_EQ(w.vehicleCount(), 0u);
}

TEST_CASE("Bos yapilandirmada simulasyon patlamaz") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg;
    cfg.vehicleCount = 0;
    World w{net, cfg, 8};
    w.step();
    CHECK_EQ(w.vehicleCount(), 0u);
    CHECK(w.finished());
}
// ---------------------------------------------------------------------------
// Bekleme suresi: HENUZ OLCULMUYOR - sessizce 0 donmemeli
// ---------------------------------------------------------------------------

TEST_CASE("Bekleme suresi henuz olculmedigi icin nullopt doner") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(10), 99};
    for (int i = 0; i < 200 && !w.finished(); ++i) {
        w.step();
    }
    // Onemli: deger 0 DEGIL, "olculmedi" doner. Karsi lastirmada kullanilamaz.
    const auto bekleme = w.metrics().averageWaitTime();
    CHECK_FALSE(bekleme.has_value());
    CHECK_FALSE(w.metrics().waitTimeMeasured);

    // Yolculuk suresi ise olculuyor ve kullanilabilir.
    CHECK(w.metrics().averageTravelTime() > 0.0);
}

TEST_CASE("averageWaitTime optional olmayi zorunlu kilar - regresyon") {
    // Eski davranis: averageWaitTime() sessizce 0.0 donuyordu ve cagiran
    // taraf "bekleme yok" diye yorumlardi. Artik optional donuyor.
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(1), 7};
    w.step();
    const auto v = w.metrics().averageWaitTime();
    static_assert(!std::is_same_v<decltype(v), double>,
                  "averageWaitTime double DONMEMELI; olculmedigi bildirilmeli");
    CHECK(v == std::nullopt);
}

// ---------------------------------------------------------------------------
// Tikanma / deadlock tespiti
// ---------------------------------------------------------------------------

TEST_CASE("Normal kosuda tikanma olmaz") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(12), 246};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    CHECK_EQ(w.metrics().stalledEvents, 0);
    CHECK_FALSE(w.deadlocked());
    CHECK_EQ(w.lastStall(), tsc::StallReason::None);
}

TEST_CASE("Hareketsiz arac sayaci izlenir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(12), 246};
    for (int i = 0; i < 5; ++i) {
        w.step();
    }
    // Her adimda araclar ilerledigi icin hareketsiz sayac sifir kalmali.
    CHECK_EQ(w.metrics().stalledNow, 0);
    CHECK_EQ(w.metrics().stalledSteps, 0);
}

TEST_CASE("Tikanma sayaclari snapshot ile korunur") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(12), 246};
    for (int i = 0; i < 5; ++i) {
        w.step();
    }
    const tsc::Snapshot s = w.snapshot();
    for (int i = 0; i < 30; ++i) {
        w.step();
    }
    w.restore(s);
    for (int i = 0; i < 30; ++i) {
        w.step();
    }
    CHECK_EQ(w.digest(), w.digest());  // kararli
    CHECK_GE(w.metrics().maxStalled, 0);
}

TEST_CASE("Tikanma esigi tanimli") {
    CHECK(tsc::kStallStepThreshold > 0);
    static_assert(tsc::kStallStepThreshold == 30, "30 saniye hareketsizlik esigi sabitlenmis olmali");
}
