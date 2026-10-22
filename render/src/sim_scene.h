// render/src/sim_scene.h — render katmaninda simulasyon sahiplenicisi.
//
// Bu sinif sim-core'un World/SignalDriver/Intersection siniflarini SAHIPLINIR
// ve onlari kare hizindan BAGIMSIZ bir tempoda ilerletir. Cizim icin yalnizca
// OKUMA erisimi verir (const World& / const Network&). Is mantigi yeniden
// yazilmaz; hepsi sim-core'adir (AGENTS.md: render is mantigi yeniden yazmaz).
//
// Neden ayri bir Sinif: "her karede world.step()" YANLISTIR. Kare hizi 60 iken
// bu 60 sim-sn/saniye olur; 30'a duserse simulasyon yavaslar. Burada bir
// AKUMULATOR var: gecen GERCEK sureyi toplar, her 1 sim-sn'de bir adim atar.
// Boylece simulasyon hizi FPS'ten bagimsizdir.

#pragma once

#include "tsc/network.hpp"
#include "tsc/types.hpp"
#include "tsc/world.hpp"
#include "controller.hpp"       // SignalDriver
#include "fixed_time.hpp"       // FixedTimeController
#include "q_learning.hpp"       // QLearningController (politika dosyasindan)

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tsc {
namespace render {

/// Kayan noktali 2B nokta. sim-core sadece Vec2i (tamsayi) tutar; ama arac
/// konumu serit uzerinde KAYAN NOKTA mesafedir, yani cizimde ara konum
/// gerekir. Bu tip yalnizca render katmanindadir; sim-core'a dokunulmaz.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

/// Bir izgara hucresine dosenen yol karosunun YOL EKSENI.
///
/// Neden gerekli: Kenney `road-straight.obj` karosunda asfalt/serit cizgileri
/// X ekseni boyunca uzanir (olculdu: asfalt x[-0.5,0.5], z[-0.4,0.4]; kaldirim
/// bantlari z kenarinda). Dikey kollarda karo 90° dondurulmezse yol, aracin
/// gidis yonune DIK cizilir — arac "yolun ortasinda/tasmayan" gorunur. Bu tip
/// karo yonunu TAHMIN etmez; hucreden gecen serit GEOMETRISINDEN turetir.
enum class RoadAxis : std::uint8_t {
    EastWest = 0,   ///< yol X boyunca (karo dondurulmez)
    NorthSouth = 1, ///< yol Z boyunca (karo Y ekseninde 90°)
    Both = 2        ///< iki yonde de serit var (kavsak hucreleri)
};

/// Render katmaninin izgara senaryosu (uc kavsakli T ag).
///
/// Bu, experiments/bench'in buildScenarioGrid fonksiyonunun birebir aynisidir.
/// Cogaltildi cunku experiments/ bir kutuphane hedefi DEGIL (executabledir)
//   ve kural gereği experiments/ dosyalarina DOKUNULMAZ. Cizim/demo
/// iskeletidir; asil izgara editoru (IG-1) bunun yerini alacak.
Grid buildRenderGrid();

/// Render katmaninda secilebilen kontrolcu (C tusu).
///
/// ONEMLI: bu tip yalnizca HANGI kontrolucunun kurulacagini secer. Karar
/// mantigi controllers/ altindadir; burada yeniden yazilmaz.
enum class ControllerKind : std::uint8_t {
    FixedTime = 0,     ///< Webster ayarli sabit zamanli
    ThresholdRule = 1, ///< kural tabanli (esik)
    QLearning = 2      ///< models/ altindaki egitilmis politika
};

/// HUD/log icin Turkce ad.
const char* controllerDisplayName(ControllerKind k) noexcept;

/// Q politikasi dosyasi icin varsayilan aday yollar (cwd'ye gore).
/// Uygulama `render/` icinden calistirilir; ilk aday ../models/.
const char* qPolicyPathCandidate(int index) noexcept;

/// Sahne senaryosu — experiments bench'in **medium** rejimiyle ayni degerler.
/// Canli kosu, IG-7 paneli ve IG-6 egitimi AYNI senaryoyu kullanir.
WorldConfig renderScenarioConfig();

/// Bir kontrolcunun AYNI tohum + AYNI senaryodaki kosu ozeti (IG-7 paneli).
struct EvalRow {
    ControllerKind kind = ControllerKind::FixedTime;
    std::string controller;         ///< gorunen ad (Q tablosu yoksa notlu)
    bool ok = false;                ///< kosu yapildi mi (false: hata)
    double averageTravelTime = 0.0; ///< varanlarin ortalamasi (s) — sansurlu
    double averageTimeInSystem = 0.0; ///< sansursuz (World::averageTimeInSystem)
    double arrivedFraction = 0.0;   ///< 0..1
    int arrived = 0;
    int spawned = 0;
    int maxStalled = 0;
    int stalledEvents = 0;          ///< gridlock olay sayisi (bench ile ayni)
    int collisions = 0;             ///< K1: daima 0 beklenir
    double simSeconds = 0.0;
    bool finished = false;
    bool timedOut = false;
};

/// Bir kontrolcuyu GUI'siz, ayni izgarada ve ayni tohumla kosar.
///
/// Neden render'da: panel "canli kosunun ayni tohumla uc kontrolcusu" demek.
/// Is mantigi burada TEKRAR YAZILMAZ; World/SignalDriver/kontrolcu aynen
/// kullanilir. Karsilastirma kablolamasi render'in isidir.
EvalRow runHeadless(std::uint64_t seed, ControllerKind kind,
                    const WorldConfig& cfg, const QLearningController* qpolicy);

class SimScene {
public:
    /// Kurar ve world'u verilen tohumla baslatir.
    /// `stepsPerSecond`: GERCEK saniyede kac simulasyon adimi atilacagi.
    ///   1.0 = gercek zamanli, 2.0 = 2x hizli.
    SimScene(std::uint64_t seed, int stepsPerSecond);

    /// Gecen GERCEK sureyi biriktirir ve gereken kadar simulasyon adimi atar.
    /// `dtSeconds` kareler arasi gercek suredir (deltaTime).
    void advance(double dtSeconds);

    /// Simulasyon durdu mu (tum araclar vardi ya da zaman asimi/tikanma)?
    bool stopped() const;

    // --- Ev / P tuslari (IG-2 kontrol tuslari) -------------------------------
    /// Simulasyonu AYNI tohumla basa dondurur (Home tusu). world_/driver_
    /// sifirdan yeniden kurulur; adim hizi korunur, biriktirici sifirlanir.
    /// NOT: yalnizca render katmanindaki sahiplenme yeniden kurulur;
    /// sim-core/controls koduna dokunulmaz (World = deger uyesi, SignalDriver
    /// World* tutar — her ikisi de yerinde yeniden atanabilir).
    void reset();

    /// Duraklat / devam et (P tusu). Duraklatilmisken advance() adim atmaz.
    bool paused() const noexcept { return paused_; }
    void setPaused(bool p) noexcept { paused_ = p; }

    // --- Cizim icin OKUMA erisimi (asla yazma) -------------------------------
    const World& world() const noexcept { return world_; }
    const Network& network() const noexcept { return net_; }

    int stepsPerSecond() const noexcept { return stepsPerSecond_; }
    void setStepsPerSecond(int n);

    /// Talep profilini degistirir (IG-3: sabah/aksam zirvesi). World, AYNI
    /// tohum ve ayni adim hiziyla yeniden kurulur (profil varis ZAMANLARINI
    /// belirler; arac sayisi sabittir — sim-core kurali).
    void setDemandProfile(DemandProfile profile);
    DemandProfile demandProfile() const noexcept { return config_.profile; }
    /// Ekranda gosterilecek saat (IG-3). World'dan okunur, TURETILMEZ.
    double hourOfDay() const noexcept { return world_.hourOfDay(); }

    // --- Kontrolcu secimi (C tusu) -----------------------------------------
    ControllerKind controllerKind() const noexcept { return kind_; }
    const char* controllerName() const noexcept { return controllerDisplayName(kind_); }
    /// Kontrolcuyu degistirir ve AYNI tohumla bastan baslatir (kullanici
    /// istegi: "her degisimde ayni tohumla"). Ayni tohum + ayni senaryo
    /// oldugu icin uc kosu ESLESTIRILMIS olur (IG-7 paneli bunu kullanir).
    void setController(ControllerKind kind);
    void cycleController();
    /// Q politika dosyasini yukler. Basarisizsa false doner (cokme YOK).
    bool loadQPolicy(const std::string& path);
    /// Egitim sonucu politikayi devralir (IG-6 Train tusu).
    void adoptQPolicy(QLearningController policy);
    bool qPolicyLoaded() const noexcept { return qpolicy_ != nullptr; }
    /// Eval/train icin salt-okunur politika kopyasi (yoksa nullptr).
    const QLearningController* qPolicy() const noexcept { return qpolicy_.get(); }
    /// Live senaryo yapilandirmasi (ayni tohumla kiyaslama kosulari icin).
    const WorldConfig& worldConfig() const noexcept { return config_; }
    std::uint64_t seed() const noexcept { return seed_; }

    /// Bir izgara hucresinin karo ekseni. Hucreye dokunan serit
    /// segmentlerinden turetilmistir (kurulumda bir kez).
    RoadAxis roadAxisAt(const Vec2i& cell) const;

    /// Bir aracin cizim konumunun olculmus ofseti (metre).
    struct VehicleOffset {
        LaneId lane;
        bool horizontal = true;   ///< serit yonu X agirlikli mi
        double lateralM = 0.0;    ///< SERIT MERKEZINE dik uzaklik (beklenen 1.75)
        double tileM = 0.0;       ///< CIZILEN KARO eksenine dik uzaklik (beklenen 1.75)
    };

    /// Aktif araclarin ofsetlerini olcer. Cizimde kullanilan fonksiyonlarin
    /// (lanePointAt/laneDirAt/applyLaneOffset/roadAxisAt) AYNISINI kullanir;
    /// kavsak hucresindeki araclar atlanir (karo ekseni tanimsiz).
    /// `forceUnrotatedTiles`: YALNIZCA teshis icin. Butun karolari dondurulmemis
    /// (EastWest) varsayar; duzeltme ONCESI davranisi olcer. Uretimde false.
    std::vector<VehicleOffset> measureVehicleOffsets(bool forceUnrotatedTiles = false) const;

    /// Bir serit uzerinde `distMeters` mesafesindeki noktanin IZGARA koordinati.
    /// Cizim piksele cevirmeden once bunu kullanir. Serit disina tasarsa
    /// sinirlar (clamped).
    Vec2d lanePointAt(LaneId lane, double distMeters) const;

    /// Bir seritte `distMeters` konumundaki YON acisi (radyan). Arac modelini
    /// dogru yone cevirmek icin cizim tarafinda kullanilir. Sim-core'a dokunmaz;
    /// yalnizca lanePointAt'i iki noktadan cagirip yonu hesaplar.
    double laneHeadingAt(LaneId lane, double distMeters) const;

    /// Bir seridin TAM merkez-yolu (from-dugum -> cells -> to-dugum), IZGARA
    /// koordinatinda ve metre cinsinden uzunluk sirasina gore. Debug katmaninda
    /// serit merkez cizgisini cizmek icin. Not: bu yol ARAC OFSETI ICERMEZ
    /// (yolun kendisidir); araclar sag-ofsetle cizilir.
    std::vector<Vec2d> laneCenterPath(LaneId lane) const;

    /// Seritte `distMeters` konumundaki GIDIS YON VEKTORU (izgara x, z).
    /// Kaynak: laneCenterPath GEOMETRISI (konum türevi DEGIL). 0 hucreli
    /// kollarda (dikey kollar) konum türevi sifirdi; geometri dogru yonu verir.
    Vec2d laneDirAt(LaneId lane, double distMeters) const;

private:
    void stepOnce();

    /// Aktif kontrolcu icin fabrika. `reset()` her cagrildiginda yeniden
    /// kurulur; Q-learning icin politikadan kavsak basina KOPYA uretir.
    SignalDriver::ControllerFactory makeFactory() const;

    /// Karo eksenlerini serit geometrisinden turetir (kurulumda bir kez).
    void buildRoadAxes();

    Network net_;
    WorldConfig config_{};      ///< profil/arac sayisi vb. (IG-3 profil secimi)
    World world_;               ///< config_'e bagli: once config_ kurulur
    SignalDriver driver_;
    std::map<std::pair<int, int>, RoadAxis> roadAxes_;  ///< hucre -> karo ekseni

    ControllerKind kind_ = ControllerKind::FixedTime;
    /// Egitilmis Q tablosu (yuklenmis ya da egitimden gelmis). nullptr ise
    /// Q-learning secildiginde bos tablo kullanilir (log'da uyarilir).
    std::unique_ptr<QLearningController> qpolicy_;

    std::uint64_t seed_ = 0;    ///< reset() ayni tohumla yeniden kurar (Home)
    bool paused_ = false;       ///< P tusu: advance() adim atmaz

    int stepsPerSecond_ = 1;
    double accumulator_ = 0.0;   ///< toplanan simulasyon-saniyesi (0 <= x < 1)

    /// Bir karede atilacak azami adim (spiral of death onlemi).
    static constexpr int kMaxStepsPerFrame = 8;
};

}  // namespace render
}  // namespace tsc
