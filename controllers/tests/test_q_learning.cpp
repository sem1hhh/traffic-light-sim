// ===========================================================================
// Q-learning kontrolcusu testleri
//
// Odulle ogrenen bir kontrolcunun testi, "calisiyor mu" demekle yetmez:
// HANGI bilesenlerin calistigini ayri ayri dogrulamak gerekir. En azindan
// su yedi seyin gercekten olustugunu kanitlamaliyiz:
//
//   1. Durum kodlama (binning) beklenen ozellige sahip mi
//   2. Odul gercekten negatif mi, kuyrukla artiyor mu
//   3. Q guncellemesi dogru yone mi gidiyor
//   4. Kesif ZIYARET EDILEN her durumda her iki eylemi de deniyor mu
//   5. Bilinmeyen durumda politika tahmin edici mi
//   6. Ayni girdi ayni karari veriyor mu (determinizm)
//   7. Kaydet/yukle dongusu tabloyu bozuyor mu
// ===========================================================================

#include "q_learning.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <set>
#include <sstream>
#include <stdexcept>

using namespace tsc;

namespace {

SignalObservation gozlem(std::array<int, 4> kuyruk, std::size_t faz, double yesil,
                          SignalState durum = SignalState::Green) {
    SignalObservation o;
    o.queues = kuyruk;
    o.phase = faz;
    o.greenElapsed = yesil;
    o.state = durum;
    o.timing.minGreen = 5.0;
    o.timing.maxGreen = 20.0;
    return o;
}

/// Ayri nokta sirasi: "N E S W" gibi okunabilir.
SignalObservation go(int n, int e, int s, int w, std::size_t faz = 0, double t = 6.0) {
    return gozlem({n, e, s, w}, faz, t);
}

double toplamQ(const QLearningController& c) {
    double t = 0.0;
    for (std::size_t s = 0; s < kQStateCount; ++s) {
        t += c.q(s, Action::Stay) + c.q(s, Action::Switch);
    }
    return t;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Durum kodlama
// ---------------------------------------------------------------------------

TEST_CASE("Durum uzayi 2560 (4^4 x 2 x 5)") {
    CHECK_EQ(kQStateCount, 4u * 4u * 4u * 4u * 2u * 5u);
    CHECK_EQ(kQActionCount, 2u);
}

TEST_CASE("Binning: kuyruklar kovaya bolunur") {
    QLearningController c;
    // Ayni kovaya düşen farklı kuyruklar AYNI durumu üretmeli.
    // (Sürekli değerlerle öğrenmek mümkün değil; kovalama olmazsa
    // her araç benzersiz durum üretir ve hiçbir şey öğrenilemez.)
    CHECK_EQ(c.encode(go(0, 0, 0, 0)), c.encode(go(1, 1, 1, 1)));
    // 5, kova 2'ye dusuyor (kenar 3 ve 6) — 0 ile AYNI degil.
    CHECK_NE(c.encode(go(0, 0, 0, 0)), c.encode(go(5, 5, 5, 5)));

    // Farklı kovalar FARKLI durum vermeli.
    CHECK_NE(c.encode(go(1, 0, 0, 0)), c.encode(go(9, 0, 0, 0)));
    CHECK_NE(c.encode(go(0, 1, 0, 0)), c.encode(go(0, 9, 0, 0)));

    // Faz farki durumu degistirmeli.
    CHECK_NE(c.encode(go(0, 0, 0, 0, 0)), c.encode(go(0, 0, 0, 0, 1)));

    // Sure farki durumu degistirmeli.
    CHECK_NE(c.encode(go(0, 0, 0, 0, 0, 1.0)), c.encode(go(0, 0, 0, 0, 0, 30.0)));
}

TEST_CASE("Binning: her kova icinde TUM degerler yer alir (bosluk yok)") {
    QLearningController c;
    std::set<std::size_t> kodlar;
    for (int n = 0; n <= 12; ++n) {
        kodlar.insert(c.encode(go(n, n, n, n)));
    }
    // 13 farkli deger, en fazla 4 kovaya dusmeli.
    CHECK_LE(kodlar.size(), 4u);
    CHECK_GE(kodlar.size(), 2u);   // ama tek kovaya da sigmamali
}

TEST_CASE("Binning: kova kenarlari turetilmis parametrelere uyar") {
    QLearningController c;
    c.setQueueEdges(2, 99, 999);
    // Yeni kenar: 0..2 -> kova 0, 3.. -> kova 1.
    CHECK_EQ(c.encode(go(0, 0, 0, 0)), c.encode(go(2, 2, 2, 2)));
    CHECK_NE(c.encode(go(0, 0, 0, 0)), c.encode(go(3, 3, 3, 3)));
}

// ---------------------------------------------------------------------------
// 2. Odul
// ---------------------------------------------------------------------------

TEST_CASE("Odul negatif ve kuyrukla buyuyor") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    // Hangi eylemin secildigine BAKMA: odulu o etilemez. Bu testte
    // kontrolcu secimi (Stay/Switch) bir ayrinti; testin konusu odulun
    // kuyrukla olcek sekilde degismesidir.
    const auto olc = [&](const SignalObservation& sonraki) {
        const auto o = go(0, 0, 0, 0);
        const Action a = c.decide(o);
        const std::size_t s = c.encode(o);
        const double once = c.q(s, a);
        c.onReward(sonraki, Action::Stay);
        return c.q(s, a) - once;   // Q degisimi = alpha * (hedef - eski)
    };

    const double bos = olc(go(0, 0, 0, 0));
    const double dolu = olc(go(2, 2, 2, 2));
    const double cok = olc(go(5, 5, 5, 5));

    CHECK_NEAR_T(bos, 0.0, 1e-9);   // kuyruk yok -> ceza yok
    CHECK_LT(dolu, 0.0);
    CHECK_LT(cok, dolu);            // kuyruk arttikca ceza DAHA kotu
    CHECK_LT(cok, bos);
}

TEST_CASE("Kotu sonuc Q degerini DUSURUR") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto o = go(0, 0, 0, 0);
    const Action a = c.decide(o);
    const std::size_t s = c.encode(o);
    CHECK_NEAR_T(c.q(s, a), 0.0, 1e-12);

    // Kotu bir sonraki durum: 12 arac bekliyor.
    c.onReward(go(3, 3, 3, 3), Action::Stay);
    CHECK_LT(c.q(s, a), 0.0);
}

TEST_CASE("İyi sonuç Q degerini ARTIRIR") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto kotu = go(4, 4, 4, 4);
    c.decide(kotu);
    const Action a = c.decide(kotu);
    const std::size_t s = c.encode(kotu);
    c.onReward(kotu, a);
    const double kotuQ = c.q(s, a);
    CHECK_LT(kotuQ, 0.0);

    // Ayni durum, iyi sonraki durumla: Q yukselmeli.
    c.decide(kotu);
    const Action b = c.decide(kotu);
    REQUIRE(static_cast<int>(b) == static_cast<int>(a));
    c.onReward(go(0, 0, 0, 0), a);
    CHECK_GT(c.q(s, a), kotuQ);
}

// ---------------------------------------------------------------------------
// 3. Q guncellemesi
// ---------------------------------------------------------------------------

TEST_CASE("Kotu sonuc Q degerini DUSURUR") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto o = go(0, 0, 0, 0);
    const Action a = c.decide(o);
    const std::size_t s = c.encode(o);
    CHECK_NEAR_T(c.q(s, a), 0.0, 1e-12);

    // Kotu bir sonraki durum: 12 arac bekliyor.
    c.onReward(go(3, 3, 3, 3), Action::Stay);
    CHECK_LT(c.q(s, a), 0.0);
}

TEST_CASE("İyi sonuç Q degerini ARTIRIR") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto kotu = go(4, 4, 4, 4);
    c.decide(kotu);
    const Action a = c.decide(kotu);
    const std::size_t s = c.encode(kotu);
    c.onReward(kotu, a);                       // kotu odul
    CHECK_LT(c.q(s, a), 0.0);

    // Ayni durum, iyi sonraki durumla: Q yukselmeli.
    const double kotuQ = c.q(s, a);
    c.decide(kotu);
    const Action b = c.decide(kotu);
    REQUIRE(static_cast<int>(b) == static_cast<int>(a));
    c.onReward(go(0, 0, 0, 0), a);
    CHECK_GT(c.q(s, a), kotuQ);
}

TEST_CASE("Ogrenilen eylem UYGULANAN degil, SECILEN eylemdir") {
    // Metodolojik karar (Faz 6'da deneysel olarak belirlendi):
    // reddedilmis bir gecis istegi de bir "gecis" gecisidir ve ogrenilmelidir.
    // Yanlis kurgu (executed) ile Q(Switch) 20 bolumde yalnizca 2 durumda
    // guncelleniyordu ve tablo neredeyse bos kaliyordu.
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto o = go(0, 0, 0, 0);
    Action secilen = Action::Stay;
    for (int i = 0; i < 20 && static_cast<int>(secilen) != static_cast<int>(Action::Switch); ++i) {
        secilen = c.decide(o);
    }
    REQUIRE(static_cast<int>(secilen) == static_cast<int>(Action::Switch));

    const std::size_t s = c.encode(o);
    const Action diger = Action::Stay;

    // ISTEK REDDEDILDI: executed = Stay. Ama kontrolcu SWITCH secmisti.
    // Odulun sifir olmamasi icin sonraki durumda kuyruk olmali; aksi halde
    // hedef deger de 0'a yakisir ve guncelleme HIC olmaz.
    c.onReward(go(6, 6, 6, 6), Action::Stay);

    // Secilen eylem guncellenmis olmali...
    CHECK_NE(c.q(s, secilen), 0.0);
    // ... ve secilmeyen eylem guncellenmemis olmali.
    CHECK_NEAR_T(c.q(s, diger), 0.0, 1e-12);
}

TEST_CASE("Durum ogrenildiginde politika OGRENILEN degeri kullanir") {
    QLearningController c;
    c.setEpsilon(0.0);
    c.setMinVisits(1);

    const auto kotu = go(4, 4, 4, 4);
    const std::size_t s = c.encode(kotu);

    // ONCE bu durumu "bilinen" yap: her iki eylem de degerlendirilsin.
    // (Ilk yazimda Stay ve Switch FARKLI durumlarda degerlendiriliyordu;
    //  o zaman hicbir durum "bilinen" olmuyordu.)
    for (int i = 0; i < 40 && !c.isKnown(s); ++i) {
        const Action a = c.decide(kotu);
        c.onReward(kotu, a);          // kotu odul
    }
    REQUIRE(c.isKnown(s));

    // SONRA Q degerlerini kur. (Tersi sirada denendi: dongu elle yazilan
    // degerleri EZDI ve test yaniltici sekilde gecti.)
    c.setQ(s, Action::Stay, -1.0);
    c.setQ(s, Action::Switch, -5.0);
    CHECK_GT(c.q(s, Action::Stay), c.q(s, Action::Switch));
    CHECK_EQ(static_cast<int>(c.greedyAction(kotu)), static_cast<int>(Action::Stay));

    // Tersi: Switch daha iyi ise gecmeli.
    c.setQ(s, Action::Stay, -9.0);
    c.setQ(s, Action::Switch, -1.0);
    CHECK_EQ(static_cast<int>(c.greedyAction(kotu)), static_cast<int>(Action::Switch));
}

TEST_CASE("Q guncellemesi SONRAKI durumun degerini tasir (bootstrap)") {
    // gamma = 0 kalsaydi Q yalnizca ANLIK odule bakar ve "gelecekte daha
    // iyi olur" bilgisi tamamen kaybolurdu.
    //
    // AYIRT EDICI TASARIM: iki kontrolcu **ayni** anlik odulu alir, ama
    // sonraki durumun Q degeri farklidir. Tek fark gamma'nin devreye
    // girmesidir.
    //   Ilk yazimda farkli ANLIK odul kullanildi; o zaman test, gamma
    //   olmasa da gecirdi ve mutasyon kacti.
    auto qUret = [](double sonrakiDurumunQ) {
        QLearningController c;
        c.setEpsilon(0.0);
        c.setMinVisits(0);        // kesif yok: karar deterministik
        c.setDecayAlpha(false);
        c.setAlpha(1.0);

        const auto s0 = go(1, 0, 0, 0);
        const auto sonraki = go(5, 5, 5, 5);      // anlik odul: -20 (her iki icin de)
        const std::size_t sk = c.encode(sonraki);
        c.setQ(sk, Action::Stay, sonrakiDurumunQ);
        c.setQ(sk, Action::Switch, sonrakiDurumunQ);

        const Action a = c.decide(s0);
        c.onReward(sonraki, a);
        return c.q(c.encode(s0), a);
    };

    const double kotuGelecek = qUret(-10.0);
    const double iyiGelecek = qUret(0.0);

    // gamma > 0 iken kotu gelecegi Daha kotu deger vermeli.
    CHECK_LT(kotuGelecek, iyiGelecek);
    // gamma = 0 iken ikisi de -20 olur ve burasi BASARISIZ olur.
}

// ---------------------------------------------------------------------------
// 4. Kesif
// ---------------------------------------------------------------------------

TEST_CASE("Kesif: ziyaret edilen her durumda her iki eylem de denenir") {
    // Ilk kurguda kesif `hash(durum)` ile veriliyordu; bu, bir durum icin
    // SABIT bir ozellik oldugu icin 47 durumun yalnizca 2'si Switch'i
    // deniyordu. Ziyaret sayaci tabanli kesif bunu duzeltir.
    QLearningController c;
    c.setMinVisits(3);

    const auto o = go(2, 2, 2, 2);
    int stay = 0;
    int sw = 0;
    for (int i = 0; i < 40; ++i) {
        if (c.decide(o) == Action::Switch) ++sw; else ++stay;
    }
    CHECK_GT(stay, 0);
    CHECK_GT(sw, 0);
}

TEST_CASE("Kesif YETERINCE denendikten sonra durur") {
    QLearningController c;
    c.setMinVisits(3);
    const auto o = go(1, 1, 1, 1);
    for (int i = 0; i < 20; ++i) {
        c.decide(o);
    }
    c.setEpsilon(0.0);

    // Artik kesif yapilmamali: ayni karar tekrarlanmali.
    const Action ilk = c.decide(o);
    for (int i = 0; i < 20; ++i) {
        CHECK_EQ(static_cast<int>(c.decide(o)), static_cast<int>(ilk));
    }
}

// ---------------------------------------------------------------------------
// 5. Bilinmeyen durum
// ---------------------------------------------------------------------------

TEST_CASE("Bilinmeyen durum TAHMIN EDICI davranir (Stay)") {
    // Tablo sifirla baslar: ogrenilmemis durumda Q=0, ogrenilmislerde
    // Q<0. `Q(Switch) > Q(Stay)` karsilastirmasi ogrenilmemis her durumda
    // Switch'i secardi; politika OGRENMEDIGI YERDE gecis yapardi.
    QLearningController c;
    c.setEpsilon(0.0);
    CHECK_FALSE(c.isKnown(c.encode(go(7, 7, 7, 7))));
    CHECK_EQ(static_cast<int>(c.greedyAction(go(7, 7, 7, 7))),
             static_cast<int>(Action::Stay));
}

// ---------------------------------------------------------------------------
// 6. Determinizm
// ---------------------------------------------------------------------------

TEST_CASE("Ayni girdi ayni karari verir (deterministik)") {
    QLearningController a;
    QLearningController b;
    a.setMinVisits(10);
    b.setMinVisits(10);

    bool ayni = true;
    for (int i = 0; i < 200; ++i) {
        const auto o = go(i % 5, (i / 3) % 5, i % 3, (i / 7) % 4, i % 2, static_cast<double>(i % 25));
        const auto o2 = go(i % 5, (i / 3) % 5, i % 3, (i / 7) % 4, i % 2, static_cast<double>(i % 25));
        a.onReward(o, (i % 2) ? Action::Stay : Action::Switch);
        b.onReward(o2, (i % 2) ? Action::Stay : Action::Switch);
        if (a.decide(o) != b.decide(o2)) ayni = false;
    }
    CHECK(ayni);
    CHECK_EQ(a.knownStates(), b.knownStates());
}

// ---------------------------------------------------------------------------
// 7. Kaydet / yukle
// ---------------------------------------------------------------------------

TEST_CASE("Kaydet/yukle tabloyu BIT BIT korur") {
    QLearningController a;
    a.setMinVisits(1);
    a.setEpsilon(0.30);
    for (int i = 0; i < 300; ++i) {
        const auto o = go(i % 7, (i / 2) % 7, (i / 3) % 7, (i / 5) % 7, i % 2,
                          static_cast<double>(i % 30));
        a.decide(o);
        a.onReward(go((i + 1) % 7, (i + 2) % 7, (i + 3) % 7, (i + 4) % 7,
                      (i + 1) % 2, static_cast<double>((i + 1) % 30)),
                   (i % 2) ? Action::Switch : Action::Stay);
    }
    CHECK_GT(a.knownStates(), 0u);

    std::ostringstream os;
    a.serialize(os);

    QLearningController b;
    std::istringstream is(os.str());
    b.deserialize(is);

    CHECK_EQ(a.knownStates(), b.knownStates());
    CHECK_NEAR_T(a.meanAbsQ(), b.meanAbsQ(), 1e-12);
    for (std::size_t s = 0; s < kQStateCount; ++s) {
        CHECK_NEAR_T(a.q(s, Action::Stay), b.q(s, Action::Stay), 1e-15);
        CHECK_NEAR_T(a.q(s, Action::Switch), b.q(s, Action::Switch), 1e-15);
    }
}

TEST_CASE("Kaydet/yukle AYNI karari uretir") {
    QLearningController a;
    a.setMinVisits(2);
    for (int i = 0; i < 200; ++i) {
        const auto o = go(i % 6, i % 4, (i / 2) % 5, (i / 3) % 3, i % 2,
                          static_cast<double>(i % 24));
        a.decide(o);
        a.onReward(go(i % 5, (i + 1) % 5, i % 3, (i + 2) % 4, (i + 1) % 2,
                      static_cast<double>((i + 2) % 24)),
                   (i % 3) ? Action::Switch : Action::Stay);
    }
    std::ostringstream os;
    a.serialize(os);
    QLearningController b;
    std::istringstream is(os.str());
    b.deserialize(is);
    b.setEpsilon(0.0);
    a.setEpsilon(0.0);

    for (int i = 0; i < 100; ++i) {
        const auto o = go(i % 6, i % 4, (i / 2) % 5, (i / 3) % 3, i % 2,
                          static_cast<double>(i % 24));
        CHECK_EQ(static_cast<int>(a.greedyAction(o)), static_cast<int>(b.greedyAction(o)));
    }
}

TEST_CASE("Bozuk model dosyasi HATA verir (sessizce bos tablo olmaz)") {
    QLearningController c;
    std::istringstream is("qlearning-v1 10 2 0.4 0.95 0.1 0\n");
    CHECK_THROWS_AS(c.deserialize(is), std::runtime_error);

    std::istringstream basliksiz("0.4 0.95\n1\n");
    CHECK_THROWS_AS(c.deserialize(basliksiz), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 8. Temizlik
// ---------------------------------------------------------------------------

TEST_CASE("clearTable tabloyu ve sayaclari sifirlar") {
    QLearningController c;
    c.setMinVisits(1);
    c.setEpsilon(0.5);
    for (int i = 0; i < 50; ++i) {
        c.decide(go(i % 5, i % 5, i % 5, i % 5));
        c.onReward(go(0, 0, 0, 0), Action::Stay);
    }
    CHECK_GT(c.knownStates(), 0u);

    c.clearTable();
    CHECK_EQ(c.knownStates(), 0u);
    CHECK_EQ(c.visitedStates(), 0u);
    CHECK_NEAR_T(c.meanAbsQ(), 0.0, 1e-12);
}

TEST_CASE("onReset zinciri KIRAR ama tabloyu KORUR") {
    QLearningController c;
    c.setMinVisits(1);
    c.setEpsilon(0.5);
    const auto o = go(1, 1, 1, 1);
    const Action a = c.decide(o);
    // Sonraki durumda kuyruk olmali; aksi halde odul 0 olur ve Q
    // guncellenmez (dejenerere durum).
    c.onReward(go(6, 6, 6, 6), Action::Stay);
    const double q = c.q(c.encode(o), a);
    CHECK_NE(q, 0.0);

    c.onReset();
    // Tablo korunur (ogrenme devam eder).
    CHECK_NEAR_T(c.q(c.encode(o), a), q, 1e-15);
    // Zincir koptugu icin onReward HICBIR seyi guncellemez: prevState_ sifirdir.
    const double toplamOnce = toplamQ(c);
    c.onReward(go(6, 6, 6, 6), Action::Stay);
    // Durum 0 belki guncellenmis olabilir; onemli olan digerlerinin degismemesi.
    CHECK_LE(toplamQ(c) - toplamOnce, 1e-9);
    CHECK_GE(toplamQ(c) - toplamOnce, -1e-9);
}
TEST_CASE("Kaydet/yukle sonrasi GERCEK KOSU ayni sonucu verir") {
    // !!!! KRITIK !!!!
    // Onceki test "Kaydet/yukle AYNI karari uretir" yalnizca greedyAction()
    // ile calisiyordu ve GEIYORDU — oysa gercek kosular FARKLI sonuc
    // veriyordu. Sebep: Q tablosu birebir ayniydi ama sayaclar (sayac_,
    // deneme_) kaydedilmiyordu; deneme_ epsilon=0 olsa bile ZIYARET
    // TABANLI KESFI belirledigi icin yuklenen politika farkli davrasti.
    //
    // Burada KOSUYU calistiriyoruz. Tablo ayni ama sayaclar farkliysa
    // karar dizisi farkli olur.
    QLearningController a;
    a.setMinVisits(4);

    // Cok sayida karar ver ve odulle guncelle -> sayaclar dolu olsun.
    for (int i = 0; i < 500; ++i) {
        const auto o = go(i % 6, (i / 2) % 5, (i / 3) % 4, (i / 5) % 5, i % 2,
                          static_cast<double>(i % 26));
        a.decide(o);
        a.onReward(go((i + 1) % 6, (i + 2) % 5, (i + 3) % 4, (i + 4) % 5,
                      (i + 1) % 2, static_cast<double>((i + 1) % 26)),
                   (i % 2) ? Action::Switch : Action::Stay);
    }
    a.setEpsilon(0.0);

    std::ostringstream os;
    a.serialize(os);
    QLearningController b;
    std::istringstream is(os.str());
    b.deserialize(is);

    // Ayni gozlem dizisi -> AYNI karar dizisi. (decide() kullanilir,
    // greedyAction() DEGIL: kesfi de dahil.)
    int fark = 0;
    for (int i = 0; i < 400; ++i) {
        const auto o = go(i % 6, (i / 2) % 5, (i / 3) % 4, (i / 5) % 5, i % 2,
                          static_cast<double>(i % 26));
        const Action ka = a.decide(o);
        const Action kb = b.decide(o);
        if (static_cast<int>(ka) != static_cast<int>(kb)) ++fark;
    }
    CHECK_EQ(fark, 0);
}
