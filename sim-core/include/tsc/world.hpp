#pragma once

#include "tsc/network.hpp"
#include "tsc/rng.hpp"
#include "tsc/types.hpp"

#include <cstdint>
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
    double totalWaitTime = 0.0;
    int maxQueue = 0;

    double averageTravelTime() const noexcept {
        return arrived > 0 ? totalTravelTime / static_cast<double>(arrived) : 0.0;
    }
    double averageWaitTime() const noexcept {
        return arrived > 0 ? totalWaitTime / static_cast<double>(arrived) : 0.0;
    }
};

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
};

}  // namespace tsc