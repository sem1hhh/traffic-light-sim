#include "tsc/world.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace tsc {
namespace {

/// FNV-1a 64-bit karma. Sirali bayt beslemesi; platformdan bagimsiz cikar.
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashBytes(std::uint64_t& h, const void* data, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnvPrime;
    }
}

void hashU64(std::uint64_t& h, std::uint64_t v) { hashBytes(h, &v, sizeof(v)); }

void hashDouble(std::uint64_t& h, double v) {
    // -0.0 ile 0.0 ayni sayilsin ki kararliligi bozmayalim.
    if (v == 0.0) {
        v = 0.0;
    }
    hashBytes(h, &v, sizeof(v));
}

}  // namespace

// ---------------------------------------------------------------------------
// Kurulum
// ---------------------------------------------------------------------------

World::World(const Network& network, WorldConfig config, std::uint64_t seed)
    : network_(network),
      config_(config),
      seed_(seed),
      spawnRng_(seed ^ mix64(0x53504157'6EULL)),   // "SPAWN"
      driveRng_(seed ^ mix64(0x44524956'45ULL)) {   // "DRIVE"
    const NetworkErrors err = validateNetwork(network_);
    ready_ = err.ok();
    vehicles_.reserve(static_cast<std::size_t>(config_.vehicleCount));
}

// ---------------------------------------------------------------------------
// Arac uretimi
// ---------------------------------------------------------------------------

bool World::spawnVehicle(LaneId entry, LaneId exit) {
    if (!entry.valid() || !exit.valid()) {
        return false;
    }
    if (entry.value >= network_.laneCount() || exit.value >= network_.laneCount()) {
        return false;
    }

    // Hedefe ulasilabilir degilse arac uretilmez (IG-1: ulasilamayan hedef).
    const std::vector<LaneId> route = network_.shortestPath(entry, exit);
    if (route.empty()) {
        return false;
    }

    Vehicle v;
    v.id = VehicleId::from(nextVehicleCounter_++);
    v.currentLane = entry;
    v.exitLane = exit;
    v.position = 0.0;
    v.speed = config_.maxSpeed;
    v.spawnTime = time_;
    v.totalDistance = 0.0;
    v.route = route;
    v.routeIndex = 0;

    vehicles_.push_back(v);
    ++metrics_.spawned;
    return true;
}

// ---------------------------------------------------------------------------
// Zaman
// ---------------------------------------------------------------------------

bool World::finished() const noexcept {
    return vehicles_.empty() && metrics_.spawned >= config_.vehicleCount;
}

void World::step() {
    if (!ready_) {
        return;
    }
    stepVehicles(kStepSeconds);
    maybeSpawn();
    time_ += kStepSeconds;
}

void World::maybeSpawn() {
    while (metrics_.spawned < config_.vehicleCount) {
        const std::vector<LaneId>& entries = network_.boundaryLanes();
        const std::vector<LaneId>& exits = network_.exitLanes();
        if (entries.empty() || exits.empty()) {
            return;  // agda giris/cikis yok: daha fazla uretem
        }

        const LaneId entry = entries[spawnRng_.below(static_cast<std::uint32_t>(entries.size()))];
        const LaneId exit = exits[spawnRng_.below(static_cast<std::uint32_t>(exits.size()))];

        // Ulasilamayan cift uretilmesin (IG-1). Cifti atla, sonrakini dene.
        if (!network_.canReach(entry, exit)) {
            continue;
        }
        spawnVehicle(entry, exit);
    }
}

World::AdvanceResult World::advanceLane(Vehicle& v) {
    // Rota uretimde hesaplandi; arac bu diziyi izler, baglantilari secmez.
    if (v.routeIndex + 1 < v.route.size()) {
        ++v.routeIndex;
        v.currentLane = v.route[v.routeIndex];
        v.position = 0.0;
        return AdvanceResult::Moved;
    }
    // Rota bitti ama hedef seritte degilsek: tutarli degil, arac takilir.
    if (v.currentLane == v.exitLane) {
        return AdvanceResult::Arrived;
    }
    return AdvanceResult::Stuck;
}

void World::stepVehicles(double dt) {
    // Hiz * dt bu adimde kat edilecek mesafe. Serit sinirini asarsa kalan
    // mesafe bir sonraki serit icin harcanir; hicbir mesafe atilmaz.
    for (std::size_t i = 0; i < vehicles_.size(); ++i) {
        Vehicle& v = vehicles_[i];
        double kalanMesafe = v.speed * dt;
        v.totalDistance += kalanMesafe;

        // Cevrim sayaci: kotu aglarda sonsuz donguye girmeyi engeller.
        for (int guvenlik = 0; kalanMesafe > 0.0 && guvenlik < 256; ++guvenlik) {
            const Lane& lane = network_.lane(v.currentLane);

            // Sifir uzunluklu serit (ornegin cikmaaz ucunun kavsaga olan
            // komsu hucresi) zaman harcamaz, dogrudan gecilir.
            if (lane.length <= 0.0) {
                const AdvanceResult r = advanceLane(v);
                if (r == AdvanceResult::Arrived) {
                    metrics_.totalTravelTime += time_ - v.spawnTime;
                    ++metrics_.arrived;
                    vehicles_[i] = vehicles_.back();
                    vehicles_.pop_back();
                    --i;  // swap edilen araci bu dongude isleme
                    kalanMesafe = 0.0;
                    break;
                }
                if (r == AdvanceResult::Stuck) {
                    v.speed = 0.0;
                    kalanMesafe = 0.0;
                    break;
                }
                continue;
            }

            const double seriIceriKalan = lane.length - v.position;

            if (kalanMesafe < seriIceriKalan) {
                v.position += kalanMesafe;   // seri icinde kaldi
                kalanMesafe = 0.0;
                break;
            }

            // Serinin sonuna ulasti: kalan mesafeyi bir sonraki seride harca.
            kalanMesafe -= seriIceriKalan;
            v.position = lane.length;

            const AdvanceResult r = advanceLane(v);
            if (r == AdvanceResult::Arrived) {
                metrics_.totalTravelTime += time_ - v.spawnTime;
                ++metrics_.arrived;
                vehicles_[i] = vehicles_.back();
                vehicles_.pop_back();
                --i;
                kalanMesafe = 0.0;
                break;
            }
            if (r == AdvanceResult::Stuck) {
                v.speed = 0.0;
                v.position = 0.0;
                kalanMesafe = 0.0;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Olcum
// ---------------------------------------------------------------------------

std::uint64_t World::digest() const {
    std::uint64_t h = kFnvOffset;
    hashDouble(h, time_);
    hashU64(h, static_cast<std::uint64_t>(metrics_.spawned));
    hashU64(h, static_cast<std::uint64_t>(metrics_.arrived));
    hashDouble(h, metrics_.totalTravelTime);
    hashDouble(h, metrics_.totalWaitTime);
    hashU64(h, static_cast<std::uint64_t>(vehicles_.size()));
    for (const Vehicle& v : vehicles_) {
        hashU64(h, v.id.value);
        hashU64(h, v.currentLane.value);
        hashU64(h, v.exitLane.value);
        hashDouble(h, v.position);
        hashDouble(h, v.speed);
        hashDouble(h, v.spawnTime);
        hashDouble(h, v.totalDistance);
        hashU64(h, static_cast<std::uint64_t>(v.routeIndex));
    }
    hashU64(h, spawnRng_.state().state);
    hashU64(h, driveRng_.state().state);
    hashU64(h, static_cast<std::uint64_t>(spawnCursor_));
    hashU64(h, nextVehicleCounter_);
    return h;
}

// ---------------------------------------------------------------------------
// Anlik goruntu
// ---------------------------------------------------------------------------

Snapshot World::snapshot() const {
    Snapshot s;
    s.time = time_;
    s.vehicles = vehicles_;
    s.metrics = metrics_;
    const Rng::State ss = spawnRng_.state();
    const Rng::State ds = driveRng_.state();
    s.spawnRngState = ss.state;
    s.spawnRngInc = ss.inc;
    s.driveRngState = ds.state;
    s.driveRngInc = ds.inc;
    s.spawnCursor = spawnCursor_;
    s.nextVehicleCounter = nextVehicleCounter_;
    return s;
}

void World::restore(const Snapshot& s) {
    time_ = s.time;
    vehicles_ = s.vehicles;
    metrics_ = s.metrics;
    spawnRng_ = Rng::fromState(Rng::State{s.spawnRngState, s.spawnRngInc, seed_ ^ mix64(0x53504157'6EULL), Rng::kDefaultStream});
    driveRng_ = Rng::fromState(Rng::State{s.driveRngState, s.driveRngInc, seed_ ^ mix64(0x44524956'45ULL), Rng::kDefaultStream});
    spawnCursor_ = s.spawnCursor;
    nextVehicleCounter_ = s.nextVehicleCounter;
}

}  // namespace tsc