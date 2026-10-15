#pragma once

#include "tsc/network.hpp"
#include "tsc/rng.hpp"
#include "tsc/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Arac
//
// Faz 1'de araclar KARSILIKLI ETKILESIMDE DEGILDIR: sabit hizla ilerler,
// durak cizgisine saygi gostermez, kavşaklarda beklemez. Arac takip modeli
// ve kavsak giris kontrolu Faz 3'te eklenir. Buradaki amac sadece:
// durumun kopyalanabilirligi ve tekrarlanabilirligi dogrulamaktir.
// ---------------------------------------------------------------------------

struct Vehicle {
    VehicleId id{};
    LaneId currentLane{};
    LaneId exitLane{};       ///< bu serit bitince arac hedefe ulasmis olur

    double position = 0.0;   ///< serit basindan itibaren kat edilen mesafe (m)
    double speed = 0.0;      ///< m/s

    double spawnTime = 0.0;  ///< simülasyon saati
    double totalDistance = 0.0;

    /// Hedefe giden en kisa yol (serit dizisi). Uretimde hesaplanir ve
    /// araç bu rotayi izler; kavsak baglantilarini rastgele secmez.
    std::vector<LaneId> route;
    std::size_t routeIndex = 0;   ///< route icindeki mevcut konum

    bool valid() const noexcept { return id.valid() && currentLane.valid(); }
};

// ---------------------------------------------------------------------------
// Olcumler
//
// Faz 4'te genisletilecek. Simdilik tekrarlanabilirlik testi icin yeterli:
// ayni tohum -> ayni degerler.
// ---------------------------------------------------------------------------
struct Metrics {
    int spawned = 0;
    int arrived = 0;
    double totalTravelTime = 0.0;   ///< ulasilan araclarin toplam yolculuk suresi (s)

    // --- Bekleme suresi: HENUZ OLCULMUYOR ---------------------------------
    // Bekleme suresi kavsak kontrolu eklenmeden (Faz 2/3) hesaplanamaz.
    // Alan burada dursa bile deger 0 kalir ve "bekleme yok" sanilabilir.
    // Bu yuzden erisim optional doner: olcum yoksa nullopt verir, cagiran
    // taraf derleyici seviyesinde uyarilir. Sessizce 0'a dönmez.
    double totalWaitTime = 0.0;
    bool waitTimeMeasured = false;

    // --- Tikanma (stall / deadlock) ----------------------------------------
    int stalledNow = 0;      ///< su an hareketsiz duran arac sayisi
    int maxStalled = 0;      ///< kosu boyunca gorulen en yuksek deger
    int stalledSteps = 0;    ///< arac hareketi olmadan gecen ardisik adim
    int stalledEvents = 0;   ///< tespit edilen tikanma olaylarinin sayisi

    double averageTravelTime() const noexcept {
        return arrived > 0 ? totalTravelTime / static_cast<double>(arrived) : 0.0;
    }

    /// Bekleme suresi olculmediyse nullopt doner. (Faz 2/3'te uygulanir.)
    std::optional<double> averageWaitTime() const noexcept {
        if (!waitTimeMeasured) {
            return std::nullopt;
        }
        if (arrived == 0) {
            return std::optional<double>{0.0};
        }
        return std::optional<double>{totalWaitTime / static_cast<double>(arrived)};
    }
};

/// Bir aracin rotasi tukendigi halde hedef seritte degilse, ya da uzun
/// sure hareketsiz kaldiginda tespit edilir. Sessizce donmak yoktur.
constexpr int kStallStepThreshold = 30;  // 30 saniye hareketsizlik = tikanma

enum class StallReason : std::uint8_t { None = 0, RouteMismatch, NoProgress };

// ---------------------------------------------------------------------------
// Dunya yapilandirmasi
// ---------------------------------------------------------------------------
struct WorldConfig {
    int vehicleCount = 50;          ///< uretilecek arac sayisi (sabit sayi)
    double maxSpeed = 13.9;         ///< m/s (~50 km/s)
    double spawnInterval = 2.0;     ///< araclar arasi uretim araligi (s)
    double timeLimit = 1800.0;      ///< bu surenin uzerinde koşu basarisiz sayilir
    std::uint64_t demandSeed = 0;   ///< talep (baslangic/hes) uretimi icin ayri tohum
};

// ---------------------------------------------------------------------------
// Anlik goruntu (snapshot)
//
// Tum durum ID-indexed ve pointer'siz oldugu icin kopyalanabilir.
// Geri sarma ozelligi (IG-8) bu temele oturur.
// ---------------------------------------------------------------------------
struct Snapshot {
    double time = 0.0;
    std::vector<Vehicle> vehicles;
    Metrics metrics;
    std::uint64_t spawnRngState = 0;
    std::uint64_t spawnRngInc = 1;
    std::uint64_t driveRngState = 0;
    std::uint64_t driveRngInc = 1;
    int spawnCursor = 0;
    std::uint32_t nextVehicleCounter = 0;  ///< ID sayaci; atlarsinca tekrarlanabilirlik bozulur
};

// ---------------------------------------------------------------------------
// Dunya
// ---------------------------------------------------------------------------
class World {
public:
    World() = default;

    /// Ag gecerli olmalidir. Gecersizse validateNetwork() hata dondurur.
    explicit World(const Network& network, WorldConfig config, std::uint64_t seed);

    bool ready() const noexcept { return ready_; }

    // --- Zaman ---------------------------------------------------------------

    double time() const noexcept { return time_; }
    bool finished() const noexcept;
    bool timedOut() const noexcept { return time_ > config_.timeLimit; }

    /// Sabit zaman adimi (1 adim = 1 saniye).
    void step();

    // --- Araclar ------------------------------------------------------------

    const std::vector<Vehicle>& vehicles() const noexcept { return vehicles_; }
    std::size_t vehicleCount() const noexcept { return vehicles_.size(); }

    /// Arac uretir ve listede tutar. Gecersiz seritler reddedilir.
    bool spawnVehicle(LaneId entry, LaneId exit);

    // --- Olcum / dogrulama --------------------------------------------------

    const Metrics& metrics() const noexcept { return metrics_; }

    /// Bir aracin neden takildigi (StallReason) - None degilse.
    StallReason lastStall() const noexcept { return lastStall_; }

    /// Tikanma tespit edildi mi? Kullaniciya ve rapora bildirilir.
    bool deadlocked() const noexcept { return metrics_.stalledEvents > 0; }

    /// Tum durumu (araclar, zaman, RNG) bir 64-bit ozetine indirger.
    /// Iki kosu ayni tohumla ayni ozet vermelidir -> tekrarlanabilirlik kaniti.
    std::uint64_t digest() const;

    // --- Anlik goruntu ------------------------------------------------------

    Snapshot snapshot() const;
    void restore(const Snapshot& s);

private:
    void stepVehicles(double dt);
    /// Serit bittiginde araci rotada bir sonraki serite gecirir.
    enum class AdvanceResult { Moved, Arrived, Stuck };
    AdvanceResult advanceLane(Vehicle& v);
    void maybeSpawn();
    /// Hareketsizlik ve rota tutarsizligini tespit eder.
    void updateStallState();
    std::uint64_t motionFingerprint() const;

    Network network_;
    WorldConfig config_{};

    double time_ = 0.0;
    bool ready_ = false;

    std::vector<Vehicle> vehicles_;
    /// Ara silinir; bosalan yerler swap-and-pop ile doldurulur.
    /// ID atanmaya devam eder, boylece cozumleyiciler kararli kalir.

    Metrics metrics_;
    std::uint64_t seed_ = 0;      ///< kurucudan gelir; snapshot geri yuklemede kullanilir
    Rng spawnRng_;
    Rng driveRng_;
    int spawnCursor_ = 0;
    std::uint32_t nextVehicleCounter_ = 0;
    StallReason lastStall_ = StallReason::None;
    std::uint64_t lastMotionFingerprint_ = 0;
    double nextSpawnTime_ = 0.0;
};

}  // namespace tsc