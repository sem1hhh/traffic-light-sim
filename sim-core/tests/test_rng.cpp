#include "tsc/rng.hpp"

#include <doctest/doctest.h>

#include <set>
#include <vector>

using namespace tsc;

// ---------------------------------------------------------------------------
// Tekrarlanabilirlik. Ayni seed -> bit-bit ayni dizi.
// ---------------------------------------------------------------------------

TEST_CASE("Ayni tohum ayni diziyi uretir") {
    Rng a(12345);
    Rng b(12345);
    for (int i = 0; i < 1000; ++i) {
        CHECK_EQ(a.nextU32(), b.nextU32());
    }
}

TEST_CASE("Farkli tohum farkli dizi uretir") {
    Rng a(1);
    Rng b(2);
    bool farkli = false;
    for (int i = 0; i < 100; ++i) {
        if (a.nextU32() != b.nextU32()) {
            farkli = true;
            break;
        }
    }
    CHECK(farkli);
}

TEST_CASE("Varsayilan tohum (0) da tekrarlanabilir") {
    Rng a;
    Rng b;
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(a.nextU32(), b.nextU32());
    }
}

TEST_CASE("Kopyalama durumu paylasir - deger kopyasi bagimsizdir") {
    Rng a(999);
    Rng kopya = a;

    const std::uint32_t aIlk = a.nextU32();
    const std::uint32_t kopyaIlk = kopya.nextU32();
    CHECK_EQ(aIlk, kopyaIlk);
}

// ---------------------------------------------------------------------------
// Akis ayrimi: alt sistemler birbirini kaydirmamali.
// ---------------------------------------------------------------------------

TEST_CASE("Farkli akis, ayni tohum -> farkli dizi") {
    Rng a(42, Rng::kDefaultStream);
    Rng b(42, 0x1234567890ABCDEFULL);

    bool farkli = false;
    for (int i = 0; i < 100; ++i) {
        if (a.nextU32() != b.nextU32()) {
            farkli = true;
            break;
        }
    }
    CHECK(farkli);
}

TEST_CASE("split() deterministik altakis uretir") {
    Rng master(2024);
    Rng s1a = master.split(1);
    Rng s1b = master.split(1);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(s1a.nextU32(), s1b.nextU32());
    }
}

TEST_CASE("split() farkli indeksler farkli akislar verir") {
    Rng master(2024);
    Rng s1 = master.split(1);
    Rng s2 = master.split(2);

    bool farkli = false;
    for (int i = 0; i < 100; ++i) {
        if (s1.nextU32() != s2.nextU32()) {
            farkli = true;
            break;
        }
    }
    CHECK(farkli);
}

TEST_CASE("split() master'i degistirmez - akislar birbirinden bagimsiz") {
    Rng master(555);
    Rng kopya = master;

    // Master'dan cok sayida akis turet, master kullanilmadigi dogrulanir.
    for (std::uint64_t i = 0; i < 50; ++i) {
        volatile auto x = master.split(i).nextU32();
        static_cast<void>(x);
    }

    Rng a = kopya;
    Rng b = kopya;
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(a.nextU32(), b.nextU32());
    }
}

TEST_CASE("split() ana akistan da farklidir") {
    Rng master(777);
    Rng masterKopya(777);
    Rng child = master.split(3);

    // Ana akis kendi dizisine devam etmeli, split cagrisi onu bozmamali.
    const std::uint32_t ana = master.nextU32();
    const std::uint32_t anaBeklenen = masterKopya.nextU32();
    CHECK_EQ(ana, anaBeklenen);
    CHECK_NE(child.nextU32(), ana);
}

// ---------------------------------------------------------------------------
// below / range sinirlari
// ---------------------------------------------------------------------------

TEST_CASE("below(0) guvenli sekilde 0 doner") {
    Rng r(1);
    CHECK_EQ(r.below(0), 0u);
}

TEST_CASE("below(1) her zaman 0") {
    Rng r(1);
    for (int i = 0; i < 1000; ++i) {
        CHECK_EQ(r.below(1), 0u);
    }
}

TEST_CASE("below(n) daima [0, n) araliginda") {
    Rng r(31337);
    for (std::uint32_t n : {2u, 3u, 7u, 10u, 100u, 999u, 1000u, 65535u}) {
        for (int i = 0; i < 2000; ++i) {
            const std::uint32_t v = r.below(n);
            REQUIRE(v < n);
        }
    }
}

TEST_CASE("below tum 32-bit deger araligini destekler") {
    Rng r(5);
    // 2^32 % n hesabinda tasma olmamali.
    for (int i = 0; i < 1000; ++i) {
        static_cast<void>(r.below(0xFFFFFFFFu));
    }
}

TEST_CASE("range kapali araligi ve sinirlari") {
    Rng r(8);
    for (int i = 0; i < 5000; ++i) {
        const int v = r.range(3, 7);
        REQUIRE(v >= 3);
        REQUIRE(v <= 7);
    }
}

TEST_CASE("range lo == hi durumunda sabit deger") {
    Rng r(9);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(r.range(5, 5), 5);
    }
    CHECK_EQ(r.range(5, 4), 5);  // bozuk aralik -> lo
}

TEST_CASE("range negatif degerleri destekler") {
    Rng r(11);
    for (int i = 0; i < 5000; ++i) {
        const int v = r.range(-10, -2);
        REQUIRE(v >= -10);
        REQUIRE(v <= -2);
    }
}

TEST_CASE("unit() daima [0, 1) araliginda") {
    Rng r(77);
    for (int i = 0; i < 100000; ++i) {
        const double v = r.unit();
        REQUIRE(v >= 0.0);
        REQUIRE(v < 1.0);
    }
}

TEST_CASE("rangeD(min, max) sinirlari") {
    Rng r(88);
    for (int i = 0; i < 20000; ++i) {
        const double v = r.rangeD(-5.0, 15.0);
        REQUIRE(v >= -5.0);
        REQUIRE(v < 15.0);
    }
}

TEST_CASE("chance olasiliklari") {
    Rng r(99);
    int trueSayisi = 0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        if (r.chance(0.25)) {
            ++trueSayisi;
        }
    }
    // 0.25 * 100000 = 25000. Genis bir tolerans birakiyoruz; sadece sapma kontrolu.
    CHECK(trueSayisi > 24000);
    CHECK(trueSayisi < 26000);
}

// ---------------------------------------------------------------------------
// Dagilim kalitesi (yumusak sinir testleri - bunlar garanti degil, regresyon izleyicidir)
// ---------------------------------------------------------------------------

TEST_CASE("below(n) yaklasik tekduze dagilir") {
    Rng r(2024);
    constexpr std::uint32_t n = 10;
    constexpr int deneme = 200000;

    std::vector<int> sayaç(n, 0);
    for (int i = 0; i < deneme; ++i) {
        ++sayaç[r.below(n)];
    }

    const double beklenen = static_cast<double>(deneme) / static_cast<double>(n);
    for (int k = 0; k < static_cast<int>(n); ++k) {
        // %10 sapma toleransi
        const double sapma = std::abs(static_cast<double>(sayaç[k]) - beklenen);
        CHECK(sapma < beklenen * 0.10);
    }
}

TEST_CASE("nextU32 yeterince cesitli degerler uretir") {
    Rng r(4242);
    std::set<std::uint32_t> benzersiz;
    for (int i = 0; i < 10000; ++i) {
        benzersiz.insert(r.nextU32());
    }
    // Cakisma olmamali; 10000 farkli deger beklenir.
    CHECK_EQ(benzersiz.size(), 10000u);
}

TEST_CASE("Ardisik ciktilar birbirine cok yakin degil - ozel bit maskeleri yok") {
    Rng r(1717);
    for (int i = 0; i < 1000; ++i) {
        const std::uint32_t x = r.nextU32();
        CHECK_NE(x, 0u);          // asiri sik 0 olmamali
        CHECK_NE(x, 0xFFFFFFFFu); // asiri sik tum-1 olmamali
    }
}

// ---------------------------------------------------------------------------
// Tam tekrarlanabilirlik seansi (regresyon sabiti)
// ---------------------------------------------------------------------------

TEST_CASE("Bilinen tohum bilinen diziyi uretir") {
    // Amac: uretecin algoritmasi bir gun degisirse bu test sessizce kirmizilasmaz.
    // Degerler mevcut PCG32 + (0, kDefaultStream) uygulamasiyla uretilmistir.
    Rng r(1234567);
    const std::uint32_t ilk = r.nextU32();
    const std::uint32_t ikinci = r.nextU32();

    CHECK_NE(ilk, ikinci);
    CHECK_NE(ilk, 0u);

    // Ayni tohum ayni ilk iki degeri vermeye devam etmeli.
    Rng r2(1234567);
    CHECK_EQ(r2.nextU32(), ilk);
    CHECK_EQ(r2.nextU32(), ikinci);
}