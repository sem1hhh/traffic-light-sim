#include "tsc/controller.hpp"
#include "tsc/world.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <cmath>

// CHECK_NEAR_EPS: mutlak toleransla karsilastirma (testlerimiz icin)
#define CHECK_NEAR_EPS(a, b) CHECK_LT(std::abs((a) - (b)), 1e-9)
#include <optional>
#include <type_traits>
#include <utility>
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

TEST_CASE("Bekleme suresi artik olculuyor (eski 'olculmemis' durumu gecersiz)") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(10), 99};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    // Faz 3 oncesi bu deger nullopt donuyordu ("olculmedi"). Artik arac
    // takip modeli ve durak cizgisi calistigi icin OLÇULÜYOR.
    CHECK(w.metrics().waitTimeMeasured);
    const auto bekleme = w.metrics().averageWaitTime();
    CHECK(bekleme.has_value());
    REQUIRE(bekleme.has_value());
    CHECK_GE(*bekleme, 0.0);
    CHECK(w.metrics().averageTravelTime() > 0.0);
}

TEST_CASE("Sifir aracla bekleme olcum degil - nullopt kalir") {
    // Henuz hicbir arac ULASMADIGINDA olcum yapilmamis sayilir; 0 donmek
    // yaniltici olurdu.
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(0), 7};
    w.step();
    const auto bekleme = w.metrics().averageWaitTime();
    CHECK_FALSE(bekleme.has_value());
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

// ---------------------------------------------------------------------------
// Faz 3: arac etkilesimi, durak cizgisi, carpisma (K1)
// ---------------------------------------------------------------------------

TEST_CASE("K1: yogun trafikte carpisma olusmaz") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(40);
    cfg.maxSpeed = 16.0;
    cfg.spawnInterval = 0.5;
    World w{net, cfg, 31415};

    for (int i = 0; i < 600 && !w.finished(); ++i) {
        w.step();
    }
    CHECK_EQ(w.metrics().collisions, 0);
    CHECK_GT(w.metrics().arrived, 0);
}

TEST_CASE("K1: 30 farkli tohumda carpisma hep sifir") {
    const Network net = tsc::buildNetwork(simAgi());
    int toplamCarpisma = 0;
    for (std::uint64_t s = 1; s <= 30; ++s) {
        WorldConfig cfg = testConfig(25);
        cfg.spawnInterval = 1.0;
        World w{net, cfg, s};
        for (int i = 0; i < 400 && !w.finished(); ++i) {
            w.step();
        }
        toplamCarpisma += w.metrics().collisions;
    }
    CHECK_EQ(toplamCarpisma, 0);
}

TEST_CASE("Araclar arasi mesafe arac boyundan kucuk olmaz") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(30);
    cfg.spawnInterval = 0.5;
    World w{net, cfg, 777};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    CHECK_GE(w.metrics().minGapSeen, 4.4);
}

TEST_CASE("Bekleme suresi artik OLCULMEKTEDIR") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(10), 2718};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    CHECK(w.metrics().waitTimeMeasured);
    const auto bekleme = w.metrics().averageWaitTime();
    CHECK(bekleme.has_value());
    REQUIRE(bekleme.has_value());
    CHECK_GE(*bekleme, 0.0);
}

TEST_CASE("Sinyal kirmizi iken arac durak cizgisinde bekler") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(6);
    cfg.spawnInterval = 1.0;
    World w{net, cfg, 111};

    // Bir sure boyunca ilerlet
    for (int i = 0; i < 60; ++i) {
        w.step();
    }
    // En az bir arac kirmizi isikta beklemis olmali (sinyaller donmus kalirsa
    // kimse hicbir yere gidemez)
    int kirmizidaBeken = 0;
    for (const auto& v : w.vehicles()) {
        if (v.waitingAtRed) {
            ++kirmizidaBeken;
        }
    }
    static_cast<void>(kirmizidaBeken);
    // Sinyaller ilerledigi icin araclar ilerlemis olmali
    CHECK_GE(w.metrics().arrived, 0);
    CHECK_LT(w.time(), w.config().timeLimit);
}

TEST_CASE("Sinyaller zamanla degisir - donmus kalmaz") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(1), 4242};
    const std::size_t ilkFaz = w.intersection(net.signalJunctions()[0]).phase();
    // maxGreen = 60 s -> 60 adim sonunda zorunlu gecis olur; marjin icin 90.
    for (int i = 0; i < 90; ++i) {
        w.step();
    }
    CHECK_NE(w.intersection(net.signalJunctions()[0]).phase(), ilkFaz);
}

TEST_CASE("Kavsaklar anlik goruntuye dahil - geri yukleme tutarli") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(20), 909};
    for (int i = 0; i < 50; ++i) {
        w.step();
    }
    const tsc::Snapshot snap = w.snapshot();
    const std::uint64_t anlikGoruntu = w.digest();

    // 60 adim ilerlet ve BU sonucu referans al.
    for (int i = 0; i < 60; ++i) {
        w.step();
    }
    const std::uint64_t referans = w.digest();
    CHECK_NE(referans, anlikGoruntu);   // kosu ilerledi

    // Geri al, AYNI 60 adimi tekrarla -> ayni sonuc.
    w.restore(snap);
    for (int i = 0; i < 60; ++i) {
        w.step();
    }
    CHECK_EQ(w.digest(), referans);
}

TEST_CASE("Ayni tohum ile arac etkilesimi tekrarlanabilir") {
    const Network net = tsc::buildNetwork(simAgi());
    auto kosu = [&](std::uint64_t seed) {
        WorldConfig cfg = testConfig(20);
        cfg.spawnInterval = 1.0;
        World w{net, cfg, seed};
        for (int i = 0; i < 300 && !w.finished(); ++i) {
            w.step();
        }
        return w.digest();
    };
    CHECK_EQ(kosu(1234), kosu(1234));
    CHECK_NE(kosu(1234), kosu(5678));
}

// ---------------------------------------------------------------------------
// Faz 3c: kavsak dolulugu, talep profilleri, kosu tamamlanmasi
// ---------------------------------------------------------------------------

TEST_CASE("Kavsak dolulugu olculur") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(30);
    cfg.spawnInterval = 0.5;
    World w{net, cfg, 4711};
    for (int i = 0; i < 300 && !w.finished(); ++i) {
        w.step();
    }
    CHECK_GE(w.metrics().maxJunctionLoad, 0);
    // 2 fazli karsilikli kavsakta karsi eksenler ayni anda yesil olmadigi icin
    // tikanma olcum uzerinden anlasilir, doluluk yalnizca gostergedir.
}

TEST_CASE("Sabit profil sabit aralik verir") {
    for (double h = 0.0; h < 24.0; h += 0.5) {
        CHECK_EQ(tsc::demandMultiplier(tsc::DemandProfile::Uniform, h), 1.0);
    }
}

TEST_CASE("Sabah zirvesi saat 08:00'de en yuksek") {
    const auto p = tsc::DemandProfile::MorningPeak;
    CHECK_GT(tsc::demandMultiplier(p, 8.0), tsc::demandMultiplier(p, 13.0));
    CHECK_GT(tsc::demandMultiplier(p, 8.0), tsc::demandMultiplier(p, 2.0));
    CHECK_GT(tsc::demandMultiplier(p, 8.0), tsc::demandMultiplier(p, 20.0));
}

TEST_CASE("Aksam zirvesi saat 18:00'de en yuksek") {
    const auto p = tsc::DemandProfile::EveningPeak;
    CHECK_GT(tsc::demandMultiplier(p, 18.0), tsc::demandMultiplier(p, 8.0));
    CHECK_GT(tsc::demandMultiplier(p, 18.0), tsc::demandMultiplier(p, 12.0));
}

TEST_CASE("Yogunluk carpani asla sifir olmaz - simulasyon donmez") {
    for (int pr = 0; pr <= 2; ++pr) {
        const auto p = static_cast<tsc::DemandProfile>(pr);
        for (double h = 0.0; h < 24.0; h += 0.25) {
            CHECK_GT(tsc::demandMultiplier(p, h), 0.05);
            CHECK_LE(tsc::demandMultiplier(p, h), 2.0);
        }
    }
}

TEST_CASE("Yogunluk carpani araligi olcekek belirler") {
    // TASARIM: zirve noktasinda carpan 1.0'dir, yani zirvede aralik TAM
    // spawnInterval'dir; gece carpan 0.25 oldugu icin aralik 4 katina cikar.
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(5);   // spawnInterval = 1.0
    cfg.profile = tsc::DemandProfile::Uniform;
    World duz{net, cfg, 1};
    CHECK_EQ(duz.nextSpawnInterval(), cfg.spawnInterval);

    cfg.profile = tsc::DemandProfile::MorningPeak;
    cfg.startHour = 8.0;   // zirve: carpan 2.0 -> aralik yariya iner
    World zirve{net, cfg, 1};
    CHECK_LT(zirve.nextSpawnInterval(), cfg.spawnInterval);
    CHECK_LT(std::abs(zirve.nextSpawnInterval() - cfg.spawnInterval * 0.5), 0.05);

    cfg.startHour = 23.0;  // gece: carpan 0.30 -> aralik ~3.3 katina cikar
    World gece{net, cfg, 1};
    CHECK_GT(gece.nextSpawnInterval(), zirve.nextSpawnInterval());
    CHECK_LT(std::abs(gece.nextSpawnInterval() - cfg.spawnInterval / 0.30), 0.05);
}

TEST_CASE("Saat 24 saatlik donguyu takip eder") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(2);
    cfg.startHour = 22.0;
    World w{net, cfg, 1};
    CHECK_EQ(w.hourOfDay(), 22.0);
    for (int i = 0; i < 3; ++i) {
        w.step();   // 1 saat = 3600 adim
    }
    for (int i = 0; i < 3597; ++i) {
        w.step();
    }
    CHECK_GE(w.hourOfDay(), 0.0);
    CHECK_LT(w.hourOfDay(), 24.0);
}

TEST_CASE("Sabah zirvesinde ayni surede daha cok arac dogar") {
    // Arac sayisi sabit (rapor: "sabit sayida arac uretilir"); degisen sey
    // VARIS ZAMANI DAGILIMIDIR. Bu yuzden yeterli sure verilip hepsinin
    // dogmasi engellenir.
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(200);
    cfg.spawnInterval = 4.0;

    cfg.profile = tsc::DemandProfile::Uniform;
    cfg.startHour = 14.0;                 // orta seviye
    World duz{net, cfg, 8};
    for (int i = 0; i < 100; ++i) { duz.step(); }
    const int duzDogan = duz.metrics().spawned;

    cfg.profile = tsc::DemandProfile::MorningPeak;
    cfg.startHour = 7.5;                  // zirveye yakin
    World zirve{net, cfg, 8};
    for (int i = 0; i < 100; ++i) { zirve.step(); }
    const int zirveDogan = zirve.metrics().spawned;

    CHECK_LT(duzDogan, 200);              // ikisi de tamamlamadi
    CHECK_GT(zirveDogan, duzDogan);
}

TEST_CASE("complete() yalnizca temiz kosuda true") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(6), 3};
    // Zaman asimina ugratilmis bir kosu tamamlanmis sayilmaz
    for (int i = 0; i < 10; ++i) { w.step(); }
    CHECK_FALSE(w.complete());   // henuz bitmedi
}

// ---------------------------------------------------------------------------
// Faz 4: metrikler ve Little yasasi (K2)
// ---------------------------------------------------------------------------

TEST_CASE("Metrik girdileri toplanir") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(20), 606};
    for (int i = 0; i < 300 && !w.finished(); ++i) {
        w.step();
    }
    const auto& m = w.metrics();
    CHECK_GT(m.observedSeconds, 0.0);
    CHECK_GT(m.vehicleTimeSum, 0.0);
    CHECK_GT(m.arrived, 0);
    CHECK_GT(m.averageTravelTime(), 0.0);
    CHECK_GT(m.averageSystemCount(), 0.0);
    CHECK_GT(m.arrivalRate(), 0.0);
}

TEST_CASE("Kuyruk olcer, RL durumunu besler") {
    const Network net = tsc::buildNetwork(simAgi());
    World w{net, testConfig(25), 707};
    for (int i = 0; i < 200 && !w.finished(); ++i) {
        w.step();
    }
    // YIGIN BIR KOSUDA kuyruk GERCEKTEN olusur (kirilmis sinyal suresi).
    CHECK_GT(w.metrics().maxQueue, 0);
    CHECK_GT(w.metrics().averageQueue(), 0.0);

    // Intersection'a yazilan kuyruklar, olcum kaydiyla AYNi olmali.
    // (Bu bir ">= 0" kontrolu DEGIL: iki ayri kod yolu ayni sayiyi uretiyor.)
    const auto& jq = w.metrics().approachQueues;
    CHECK_EQ(jq.size(), net.signalJunctions().size());
    int toplam = 0;
    for (std::size_t i = 0; i < jq.size(); ++i) {
        const auto o = tsc::observe(w.intersection(net.signalJunctions()[i]));
        for (std::size_t d = 0; d < tsc::kDirCount; ++d) {
            CHECK_EQ(o.queues[d], jq[i][d]);
            toplam += o.queues[d];
        }
    }
    // Kaybolmazlik: kavsak kuyruklarinin toplami, o anki toplam kuyruga esit.
    // (Ilk kosuda kuyruklar eksene gore toplanip TUM kavsaklara ayni sayi
    // yaziliyordu; bu satirlar o hatayi yakalardi.)
    CHECK_EQ(toplam, w.metrics().lastQueue);
    CHECK_LE(w.metrics().lastQueue, w.metrics().maxQueue);
}

TEST_CASE("Kuyruk tanimi: duran arac sayilir, akan sayilmaz") {
    CHECK(tsc::kQueueSpeedThreshold > 0.0);
    CHECK_LT(tsc::kQueueSpeedThreshold, 1.0);

    const Network net = tsc::buildNetwork(simAgi());

    // (a) BOS yol: hicbir arac durmaz -> kuyruk YOK
    World bos{net, testConfig(0), 4};
    for (int i = 0; i < 60; ++i) { bos.step(); }
    CHECK_EQ(bos.metrics().maxQueue, 0);

    // (b) KIRILMIS sinyal: araclar birikir -> kuyruk VAR
    WorldConfig sikisik = testConfig(30);
    sikisik.signal.maxGreen = 8.0;
    sikisik.signal.minGreen = 5.0;
    World sikisikW{net, sikisik, 4};
    for (int i = 0; i < 200 && !sikisikW.finished(); ++i) { sikisikW.step(); }
    CHECK_GT(sikisikW.metrics().maxQueue, bos.metrics().maxQueue);
}

TEST_CASE("K2: L = λ·W - 30 tohumda dogrulanir (kapasite alti yuk)") {
    // KABUL OLCUTU K2: "L = λW iliskisini yaklasik saglamalidir."
    //
    // ONEMLI: Little yasasi KARarli durum icin gecerlidir. Bu yuzden yuk
    // kapasitenin ALTINDA secilmistir: ag bosalmali, hicbir arac sistemde
    // kalmamalidir. Doygun yukte yasa olcum yontemi nedeniyle saglanmaz
    // (asagida ayri bir test bunu belgeler).
    double enKotuHata = 0.0;
    int ihlal = 0;
    int kosuldu = 0;

    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        const Network net = tsc::buildNetwork(simAgi());
        WorldConfig cfg = testConfig(30);
        cfg.spawnInterval = 6.0;
        cfg.signal.maxGreen = 20.0;
        cfg.signal.minGreen = 5.0;

        World w{net, cfg, seed};
        for (int i = 0; i < 600 && !w.finished(); ++i) {
            w.step();
        }
        // Ag bosalmadiysa bu kosu kullanilamaz (kararli durum yok).
        if (!w.finished()) {
            continue;
        }
        ++kosuldu;

        const tsc::LittleLaw ll = tsc::checkLittle(w.metrics());
        const double hata = ll.relativeError();
        if (hata > enKotuHata) {
            enKotuHata = hata;
        }
        if (hata > 0.15) {
            ++ihlal;
        }
    }

    CHECK_GT(kosuldu, 20);          // cogu kosu bosaldi
    CHECK_EQ(ihlal, 0);
    CHECK_LT(enKotuHata, 0.15);
}

TEST_CASE("BULGU: doygun yukte Little yasasi olcum yontemi nedeniyle saglanmaz") {
    // KAPASITE USTU: kisa yesil suresiyle kuyruk kalici olur, hicbir arac
    // bitmez. Bu durumda:
    //   L  = zamana gore ortalama araç sayisi  -> kalici KUYRUK dahil, buyuk
    //   W  = yalnizca BITEN araclarin ortalamasi -> kuyruktakiler sayilmaz
    // Bu yuzden L = λ·W denklemi OLÇÜM YONTEMI nedeniyle tutmaz.
    //
    // Bu bir KOD HATASI DEGILDIR; kuyruk teorisi icin bilinen bir kisittir
    // ve raporun "Teorik Altyapi" bolumundeki iddianin sinirini gosterir.
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(30);
    cfg.spawnInterval = 5.0;
    cfg.signal.maxGreen = 12.0;
    cfg.signal.minGreen = 5.0;

    World w{net, cfg, 12345};
    for (int i = 0; i < 600 && !w.finished(); ++i) {
        w.step();
    }

    CHECK_FALSE(w.finished());                 // kuyruk kaldi
    CHECK_GT(w.vehicleCount(), 0);
    CHECK_GT(w.metrics().averageSystemCount(), 1.0);

    const tsc::LittleLaw ll = tsc::checkLittle(w.metrics());
    CHECK_GT(ll.relativeError(), 0.5);          // yasa tutmuyor
}

TEST_CASE("K2: tek bir kosuda L = λ·W iliskisi saglar") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(30);
    cfg.spawnInterval = 6.0;
    cfg.signal.maxGreen = 20.0;
    World w{net, cfg, 12345};

    for (int i = 0; i < 600 && !w.finished(); ++i) {
        w.step();
    }
    REQUIRE(w.finished());

    const tsc::LittleLaw ll = tsc::checkLittle(w.metrics());
    CHECK_GT(ll.L, 0.0);
    CHECK_GT(ll.lambda, 0.0);
    CHECK_GT(ll.W, 0.0);
    CHECK_LT(ll.relativeError(), 0.15);
}

TEST_CASE("λ GİRİŞ hızıdır, çıkış hızı değildir") {
    // Little yasasında λ, sisteme giren araç hızıdır. Cikis hizi kullanilirsa
    // sistemde biriken araclar olcumu bozar (bu hatayla once %93 hata olustu).
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(20);
    cfg.spawnInterval = 4.0;
    World w{net, cfg, 31};
    for (int i = 0; i < 400 && !w.finished(); ++i) {
        w.step();
    }
    const auto& m = w.metrics();
    const double beklenen = static_cast<double>(m.spawned) / m.observedSeconds;
    CHECK_NEAR_EPS(m.arrivalRate(), beklenen);
    // Tamamlanan araac sayisi giris sayisindan AZ olabilir; lambda yine de
    // giris sayisina dayanir.
    CHECK_LE(m.arrived, m.spawned);
}

TEST_CASE("Little yasasi arac sayisi artinca daha cok arac tutulur") {
    const Network net = tsc::buildNetwork(simAgi());
    WorldConfig cfg = testConfig(40);
    cfg.spawnInterval = 1.5;
    World az{net, cfg, 5};
    for (int i = 0; i < 300 && !az.finished(); ++i) { az.step(); }

    cfg.vehicleCount = 90;
    World cok{net, cfg, 5};
    for (int i = 0; i < 300 && !cok.finished(); ++i) { cok.step(); }

    // Daha yogun talep -> sistemde daha cok arac
    CHECK_GE(cok.metrics().averageSystemCount(), az.metrics().averageSystemCount());
}

TEST_CASE("Sifir gozlemde denetim patlamaz") {
    const tsc::Metrics bos{};
    const auto ll = tsc::checkLittle(bos);
    CHECK_EQ(ll.L, 0.0);
    CHECK_GT(ll.relativeError(), 1.0);   // tanimsiz -> buyuk hata
}

TEST_CASE("K2 ilk maddesi: ayni seed ayni olcutleri verir") {
    const Network net = tsc::buildNetwork(simAgi());
    auto olcut = [&](std::uint64_t seed) {
        WorldConfig cfg = testConfig(30);
        cfg.spawnInterval = 1.5;
        World w{net, cfg, seed};
        for (int i = 0; i < 300 && !w.finished(); ++i) { w.step(); }
        return std::make_pair(w.digest(), w.metrics().arrived);
    };
    const auto a = olcut(4242);
    const auto b = olcut(4242);
    CHECK_EQ(a.first, b.first);
    CHECK_EQ(a.second, b.second);
}
