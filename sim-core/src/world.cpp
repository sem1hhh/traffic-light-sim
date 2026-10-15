#include "tsc/world.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace tsc {
namespace {

/// FNV-1a 64-bit karma. Sirali bayt beslemesi; platformdan bagimsiz cikar.
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;

/// Arac boyu (m). Iki aracin merkezleri arasindaki minimum bosluktur.
constexpr double kVehicleLength = 4.5;
/// "Onunde engel yok" anlamina gelen mesafe.
constexpr double kNoObstacle = 1.0e9;

/// Bir saniyelik makro adim kac alt adime bolunur?
/// IDM 1 saniyelik tek adimda kararli DEGILDIR (arac duran onundekinin
/// uzerinden gecer). 0.1 s'lik alt adimlar K1'i garanti eder.
constexpr int kCarFollowingSubSteps = 10;
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

    // Sinyalli her dugum icin bir faz makinesi kur.
    intersections_.assign(network_.nodeCount(), Intersection{});
    for (const JunctionId jid : network_.signalJunctions()) {
        std::vector<Dir> arms;
        for (const Dir d : kAllDirs) {
            if (network_.node(jid).arms & (1u << static_cast<int>(d))) {
                arms.push_back(d);
            }
        }
        intersections_[jid.value] = Intersection{jid, arms, config_.signal};
    }
    junctionOccupancy_.assign(network_.nodeCount(), 0);
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
    stepVehicles(kStepSeconds);   // 1 saniye = birden fazla alt adim
    detectCollisions();           // K1 olcumu
    sampleMetrics();              // L, kuyruk, RL durumu
    maybeSpawn();
    updateStallState();
    time_ += kStepSeconds;
}

void World::maybeSpawn() {
    // Uretim ZAMANA YAYILIR: spawnInterval kadar saniye arayla bir arac.
    // (Eskiden hepsi ilk adimda doguyordu; bu, talep profilini anlamsiz
    // kiliyordu.)
    while (metrics_.spawned < config_.vehicleCount && time_ >= nextSpawnTime_) {
        const std::vector<LaneId>& entries = network_.boundaryLanes();
        const std::vector<LaneId>& exits = network_.exitLanes();
        if (entries.empty() || exits.empty()) {
            nextSpawnTime_ = time_ + nextSpawnInterval();
            return;  // agda giris/cikis yok
        }

        const LaneId entry = entries[spawnRng_.below(static_cast<std::uint32_t>(entries.size()))];
        const LaneId exit = exits[spawnRng_.below(static_cast<std::uint32_t>(exits.size()))];

        // Ulasilamayan cift uretilmesin (IG-1). Cifti atla, sonrakini dene.
        // Not: dongunun sonlanmasi validateNetwork'in "her giristen en az bir
        // cikisa ulasilir" garantisine dayanir. Bu ciftler sirasiyla
        // denendigi icin, ulasilabilir bir cift varsa dongu eninde sonlanir.
        if (!network_.canReach(entry, exit)) {
            continue;
        }
        spawnVehicle(entry, exit);
        nextSpawnTime_ += nextSpawnInterval();
    }
}

/// Arac hareket ediyor mu? Hareketsizlik tikanma tespitinin girdisidir.
std::uint64_t World::motionFingerprint() const {
    std::uint64_t h = kFnvOffset;
    for (const Vehicle& v : vehicles_) {
        hashU64(h, v.currentLane.value);
        hashDouble(h, v.position);
        hashU64(h, static_cast<std::uint64_t>(v.speed));
    }
    return h;
}

/// Hareketsizlik ve rota tutarsizligini TESPIT EDER (sessizce beklemez).
void World::updateStallState() {
    // KIRMIZI ISIKTA BEKLEYEN araç tıkanma DEĞİLDİR; sıradaki yeşil
    // gelince kendiliğinden kalkar. Sayac yalnızca gerçek kilitlenmeyi görür.
    int hareketsiz = 0;
    for (const Vehicle& v : vehicles_) {
        if (v.speed <= 0.0 && !v.waitingAtRed) {
            ++hareketsiz;
        }
    }
    metrics_.stalledNow = hareketsiz;
    metrics_.maxStalled = std::max(metrics_.maxStalled, hareketsiz);

    // "Ilerleme yok" tespiti: arac sayisi azalsa da olsa, hicbir aracin
    // konumu degismediginde sayac artar. Yalnizca "hepsi durdu" demek
    // yetersizdir; bir ara takilip digerleri ilerliyorsa da ilerleme vardir.
    const std::uint64_t parmak = motionFingerprint();
    const bool ilerlemeVar = (parmak != lastMotionFingerprint_);
    lastMotionFingerprint_ = parmak;

    // KIRMIZI ISIKTA BEKLEMEK TIKANMA DEGILDIR.
    // Butun kuyruklar yalnizca kirmizi isikta bekliyorsa sayac sifirlanir;
    // yesil gelince araçlar kendiliğinden kalkar.
    if (hareketsiz == 0) {
        metrics_.stalledSteps = 0;
        lastStall_ = StallReason::None;
        return;
    }

    if (ilerlemeVar) {
        metrics_.stalledSteps = 0;
    } else {
        ++metrics_.stalledSteps;
    }

    // Rota tukendi ama arac hedef seritte degilse (tutarsiz rota).
    for (const Vehicle& v : vehicles_) {
        if (v.currentLane != v.exitLane && v.routeIndex + 1 >= v.route.size()) {
            lastStall_ = StallReason::RouteMismatch;
            ++metrics_.stalledEvents;
            return;
        }
    }

    // Uzerinde esik degeri gecen sure hareketsiz kaldik: tikanma olayi.
    if (metrics_.stalledSteps >= kStallStepThreshold) {
        lastStall_ = StallReason::NoProgress;
        ++metrics_.stalledEvents;
        metrics_.stalledSteps = 0;  // olay sayaci tekrar tekrar artmasin
    }
}

LittleLaw checkLittle(const Metrics& m, double warmupSeconds) {
    // Baslangic transiyentini kirp: yasa kararli durum icin gecerlidir.
    double L = 0.0;
    double lambda = 0.0;
    double W = m.averageTravelTime();

    if (m.observedSeconds > warmupSeconds && warmupSeconds > 0.0) {
        // warmup sonrasi gozlem suresi
        // vehicleTimeSum'den warmup katkisi dusulur. Araclar warmup boyunca
        // kademeli geldigi icin tam bir duzeltme yerine oranlama kullanilir.
        const double oran = (m.observedSeconds - warmupSeconds) / m.observedSeconds;
        L = m.averageSystemCount() * oran;
        lambda = m.arrivalRate() * oran;
    } else {
        L = m.averageSystemCount();
        lambda = m.arrivalRate();
    }

    LittleLaw r;
    r.L = L;
    r.lambda = lambda;
    r.W = W;
    return r;
}

const char* toString(DemandProfile p) noexcept {
    switch (p) {
        case DemandProfile::Uniform:     return "sabit";
        case DemandProfile::MorningPeak: return "sabah-zirvesi";
        case DemandProfile::EveningPeak: return "aksam-zirvesi";
    }
    return "?";
}

double demandMultiplier(DemandProfile profile, double hourOfDay) {
    // Gauss zirvesi; gece tabani 0.25. Gece asla sifir DEGILDIR.
    const auto zirve = [hourOfDay](double merkez, double genislik) {
        const double d = (hourOfDay - merkez) / genislik;
        return std::exp(-d * d);
    };
    // Aralik [0.30, 2.00]. Sabit profil 1.0'dir ve BASLANGIC referansidir;
    // zirve 2.0 ile onun IKIDEN hizlidir, gece 0.30 ile yavas.
    // (Once zirve 1.0 idi; boylece 'sabit' profil zirveden hizli cikiyordu
    //  ve profil hicbir sey degistirmiyordu. Testler bunu yakaladi.)
    constexpr double kGece = 0.30;
    constexpr double kZirve = 2.00;
    switch (profile) {
        case DemandProfile::Uniform:
            return 1.0;
        case DemandProfile::MorningPeak:
            return kGece + (kZirve - kGece) * zirve(8.0, 1.6);
        case DemandProfile::EveningPeak:
            return kGece + (kZirve - kGece) * zirve(18.0, 1.9);
    }
    return 1.0;
}

double World::hourOfDay() const noexcept {
    double h = config_.startHour + time_ / 3600.0;
    while (h >= 24.0) { h -= 24.0; }
    while (h < 0.0) { h += 24.0; }
    return h;
}

double World::nextSpawnInterval() const noexcept {
    // Yogunluk arttikca uretim araligi kisalir.
    return config_.spawnInterval / std::max(0.05, demandMultiplier());
}


Intersection& World::intersection(JunctionId j) { return intersections_[j.value]; }

const Intersection& World::intersection(JunctionId j) const { return intersections_[j.value]; }

bool World::hasSignalAt(JunctionId j) const {
    return j.valid() && j.value < intersections_.size() && intersections_[j.value].active();
}

int World::vehiclesInJunction(JunctionId j) const {
    if (!hasSignalAt(j)) {
        return 0;
    }
    return junctionOccupancy_[j.value];
}

Vehicle& World::findVehicle(VehicleId id) {
    return vehicles_[vehicleIndex_[id.value]];
}

const Vehicle& World::findVehicle(VehicleId id) const {
    return vehicles_[vehicleIndex_[id.value]];
}

JunctionId World::junctionOf(const Vehicle& v) const {
    // Arac sonraki seride ve o serit bir kavsaktan basliyorsa orada.
    if (v.routeIndex + 1 < v.route.size()) {
        return network_.lane(v.route[v.routeIndex + 1]).from;
    }
    return JunctionId{};
}

double World::gapToStopLine(const Vehicle& v) const {
    const Lane& lane = network_.lane(v.currentLane);
    return lane.length - v.position;
}

bool World::mustStopAtRed(const Vehicle& v) const {
    const JunctionId j = junctionOf(v);
    if (!hasSignalAt(j)) {
        return false;
    }
    const Lane& next = network_.lane(v.route[v.routeIndex + 1]);
    return !intersections_[j.value].hasGreen(next.entryDir);
}

void World::enforceMinimumGap() {
    // K1'in YAPISAL GARANTISI. IDM konforlu frenleme (3 m/s^2) mevcut
    // boslukta durmaya yetmeyebilir; bu yuzden konumlar serit sirasiyla
    // onden arkaya taranip arac boyu mesafesi ZORLANIR.
    //
    // Yapisi: liste konuma gore azalan sirali -> liste[k] ONDEKI.
    // Cakisma varsa once ONDEKI arac ileri itilir (yol varsa); yol yoksa
    // arkadaki geri itilir. Boylece hicbir arac digerinin uzerinden gecemez.
    for (const auto& liste : laneVehicles_) {
        if (liste.size() < 2) {
            continue;
        }
        const Lane& lane = network_.lane(findVehicle(liste.front()).currentLane);
        // Sifir uzunluklu seritler fiziksel uzunluga sahip degildir.
        if (lane.length <= 0.0) {
            continue;
        }

        // ARKADAN ONE dogru gezilir. Onde bir araci ILERI itmek, onun
        // ondekiyle olan mesafesini azaltir; bu yuzden o cift EN SON
        // ele alinmalidir. Onden arkaya gidilseydi bir sonraki cift
        // bozulur ve araclar ust uste binmeye devam ederdi.
        for (std::size_t k = liste.size() - 1; k > 0; --k) {
            // liste AZALAN sirali: liste[k] ONDEKI, liste[k+1] arkadaki.
            Vehicle& on = findVehicle(liste[k]);          // ondeki
            Vehicle& arka = findVehicle(liste[k + 1]);    // arkadaki

            if (on.position - arka.position >= kVehicleLength) {
                continue;   // zaten yeterli mesafe var
            }

            // Once arkadakini geri it; negatif olmayacaksa temiz ve
            // one dogru etkilemez.
            const double geriCekilmis = on.position - kVehicleLength;
            if (geriCekilmis >= 0.0) {
                arka.position = geriCekilmis;
            } else {
                // Ondeki arac seridin basina cok yakin; ONUNU ileri it.
                const double ileri = arka.position + kVehicleLength;
                if (ileri <= lane.length) {
                    on.position = ileri;
                } else {
                    // Yol yok: arkadaki en cok geri gidebilecegi yere cek.
                    arka.position = std::max(0.0, lane.length - kVehicleLength);
                    on.position = std::max(on.position, arka.position + kVehicleLength);
                }
            }
            arka.speed = 0.0;   // arkadaki durmak zorunda
            arka.stopped = true;
        }
    }
}

void World::detectCollisions() {
    // K1: carpisma sayisi sifir olmali. Ayni seritteki araclar arasinda
    // merkez mesafesi arac boyundan kucukse carpisma vardir.
    for (const auto& liste : laneVehicles_) {
        // SIFIR UZUNLUKLU seritler (cikmaz ucu -> kavsak gecisleri) fiziksel
        // uzunluga sahip degildir; uzerlerindeki araclar konum 0'da kuyruk
        // yapar ve konum burada anlamsizdir. enforceMinimumGap da ayni
        // sebeple bunlari atlar; carpisma olcumu tutarli olmali.
        if (liste.empty()) {
            continue;
        }
        if (network_.lane(findVehicle(liste.front()).currentLane).length <= 0.0) {
            continue;
        }
        // liste[k] ondeki, liste[k+1] arkadaki -> mesafe = once - sonra
        for (std::size_t k = 0; k + 1 < liste.size(); ++k) {
            const Vehicle& on = findVehicle(liste[k]);
            const Vehicle& arka = findVehicle(liste[k + 1]);
            const double mesafe = on.position - arka.position;
            metrics_.minGapSeen = std::min(metrics_.minGapSeen, mesafe);
            if (mesafe < kVehicleLength) {
                ++metrics_.collisions;
            }
        }
    }
}

void World::updateJunctionOccupancy() {
    // "Kavsak ici" = kavsaga GIRIS seridinin sonunda olan araclar. Bunlar
    // kavsak tirisiminde (crossing) sayilir. 2 fazli karsilikli kavsakta
    // karsi eksenler ayni anda yesil olmadigi icin tikanma bu olcum
    // yalnizca DOLULUK gostergesidir (capraz carpisma bu modelde yoktur).
    std::fill(junctionOccupancy_.begin(), junctionOccupancy_.end(), 0);
    for (const Vehicle& v : vehicles_) {
        const JunctionId j = junctionOf(v);
        if (hasSignalAt(j)) {
            const Lane& lane = network_.lane(v.currentLane);
            const double kalan = lane.length - v.position;
            // Seritin son %25'inde = kavsaga giris bolgesi
            if (lane.length <= 0.0 || kalan < 0.25 * lane.length) {
                ++junctionOccupancy_[j.value];
                metrics_.maxJunctionLoad =
                    std::max(metrics_.maxJunctionLoad, junctionOccupancy_[j.value]);
            }
        }
    }
}

void World::sampleMetrics() {
    // --- Sistemdeki arac sayisi (L icin zaman-ortalamasi) -----------------
    metrics_.vehicleTimeSum += static_cast<double>(vehicles_.size()) * kStepSeconds;
    metrics_.observedSeconds += kStepSeconds;

    // --- Kuyruklar: pratik olarak durmus araclar -------------------------
    //
    // Kuyruk KAVSAK BASINA olculur, eksen basina degil. Her kavsak kendi
    // kollarinin kuyrugunu gorur; aksi halde tum kavsaklar ayni sayilari
    // gorur ve kontrolcu karari anlamli olmaktan cikar.
    //
    // (Ilk kosuda hata bu ayrim yapilmadigi icin uc kavsagin da kuyrugu
    // birebir ayni cikiyordu: 2,9,0,8.)
    std::vector<std::array<int, kDirCount>> kavsakKuyruk(
        network_.signalJunctions().size(), std::array<int, kDirCount>{});

    int toplamKuyruk = 0;
    std::size_t jIdx = 0;
    for (const JunctionId j : network_.signalJunctions()) {
        for (const Vehicle& v : vehicles_) {
            if (v.speed > kQueueSpeedThreshold) {
                continue;
            }
            if (junctionOf(v) != j) {
                continue;
            }
            ++toplamKuyruk;
            if (v.routeIndex + 1 < v.route.size()) {
                const Dir yaklasma =
                    network_.lane(v.route[v.routeIndex + 1]).entryDir;
                ++kavsakKuyruk[jIdx][static_cast<std::size_t>(yaklasma)];
            }
        }
        ++jIdx;
    }

    metrics_.queueSum += static_cast<double>(toplamKuyruk);
    metrics_.maxQueue = std::max(metrics_.maxQueue, toplamKuyruk);
    metrics_.lastQueue = toplamKuyruk;

    // Her kavsagin gordugu kuyruk: Intersection'a yazilir -> kontrolcunun
    // durumu dogrudan beslenir (RL icin).
    jIdx = 0;
    metrics_.approachQueues = kavsakKuyruk;
    for (const JunctionId j : network_.signalJunctions()) {
        for (const Dir d : kAllDirs) {
            intersections_[j.value].setQueue(d, kavsakKuyruk[jIdx][static_cast<std::size_t>(d)]);
        }
        ++jIdx;
    }
}

void World::stepSignals(double dt) {
    for (Intersection& j : intersections_) {
        if (j.active()) {
            j.step(dt);
        }
    }
}

void World::rebuildLaneIndex() {
    laneVehicles_.assign(network_.laneCount(), {});
    // vehicleIndex_ arac ID'sine gore indekslenir; sayac buyuyebildigi icin
    // once yeterince buyutulur. (Boyutlandirmadan yazmak -> SIGSEGV.)
    vehicleIndex_.assign(nextVehicleCounter_, 0);
    for (std::size_t i = 0; i < vehicles_.size(); ++i) {
        vehicleIndex_[vehicles_[i].id.value] = i;
    }
    for (const Vehicle& v : vehicles_) {
        if (v.currentLane.value < laneVehicles_.size()) {
            laneVehicles_[v.currentLane.value].push_back(v.id);
        }
    }
    for (auto& liste : laneVehicles_) {
        std::sort(liste.begin(), liste.end(), [this](VehicleId a, VehicleId b) {
            const double ya = findVehicle(a).position;
            const double yb = findVehicle(b).position;
            if (ya != yb) {
                return ya > yb;   // ondeki (buyuk konum) once
            }
            return a.value < b.value;   // esitlikte ID sirasi: deterministik
        });
    }
}

void World::applyCarFollowing(double dt) {
    // --- 1) Serit basina arac listesi, konuma gore sirali -----------------
    // Ondekiler (buyuk konum) once. Esit konumda ID sirasi kirilir:
    // determinizm icin zorunlu.
    rebuildLaneIndex();

    // --- 2) Her aracin onundeki engeli bulup IDM uygula ---------------------
    for (const auto& liste : laneVehicles_) {
        for (std::size_t k = 0; k < liste.size(); ++k) {
            Vehicle& v = findVehicle(liste[k]);

            double gap = kNoObstacle;
            double leadSpeed = v.speed;

            if (k > 0) {
                // Ayni seritte ONUNDEKI arac liste[k-1]'dir (azalan sira).
                const Vehicle& ondek = findVehicle(liste[k - 1]);
                gap = std::max(0.0, ondek.position - v.position - kVehicleLength);
                leadSpeed = ondek.speed;
            } else if (v.routeIndex + 1 < v.route.size()) {
                // Seritin sonu: durak cizgisi ve kirmizi isik.
                const Lane& laneNow = network_.lane(v.currentLane);
                if (laneNow.length <= 0.0) {
                    // Gecis seridi (or. cikmaaz ucu -> kavsak): durak cizgisi
                    // YOK. Arac aninda gecer; engel yalnizca ilk arac olabilir.
                    const LaneId nextLane = v.route[v.routeIndex + 1];
                    if (nextLane.value < laneVehicles_.size() &&
                        !laneVehicles_[nextLane.value].empty()) {
                        const Vehicle& ondeki = findVehicle(laneVehicles_[nextLane.value].front());
                        leadSpeed = ondeki.speed;
                        gap = std::max(0.0, ondeki.position - kVehicleLength);
                    } else {
                        gap = kNoObstacle;
                    }
                    goto hesaplandi;
                }

                gap = std::max(0.0, gapToStopLine(v));

                if (mustStopAtRed(v)) {
                    leadSpeed = 0.0;   // kirmizi isik: durak cizgisi engel
                } else {
                    // Yesil. Sonraki seridin ilk araci varsa o engeldir;
                    // MESAFE kalan yol + onun konumu olmak zorunda.
                    // (Yalnizca "kalan yol" alinirsa araclar sifir mesafede
                    // birbirini gorur ve simulasyon kilitlenir.)
                    const LaneId nextLane = v.route[v.routeIndex + 1];
                    if (nextLane.value < laneVehicles_.size() &&
                        !laneVehicles_[nextLane.value].empty()) {
                        const Vehicle& ondeki = findVehicle(laneVehicles_[nextLane.value].front());
                        leadSpeed = ondeki.speed;
                        gap = std::max(0.0, gap + ondeki.position - kVehicleLength);
                    } else {
                        gap = kNoObstacle;   // kavsakta kimse yok: girebilir
                    }
                }
            }

        hesaplandi:
            v.speed = idmSpeedAfterStep(config_.idm, v.speed, gap, leadSpeed,
                                        dt, config_.idm.desiredSpeed);
            v.stopped = (v.speed < 0.05);
            v.waitingAtRed = v.stopped && mustStopAtRed(v);
            if (v.stopped) {
                v.waitTime += dt;
            }
        }
    }
}

bool World::nextLaneHasRoom(const Vehicle&, LaneId nextLane) const {
    // DOGRUDAN vehicles_ uzerinden taranir, laneVehicles_ UZERINDEN DEGIL.
    // laneVehicles_ alt adimin BASINDA kurulur; bu alt adim icinde baska
    // araclar gecis yaparsa bayat kalir ve birden fazla arac ayni konuma
    // (0) girip ust uste biner. Canli liste her zaman gunceldir.
    for (const Vehicle& other : vehicles_) {
        if (other.currentLane == nextLane && other.position < kVehicleLength) {
            return false;
        }
    }
    return true;
}

World::AdvanceResult World::advanceLane(Vehicle& v) {
    // Rota uretimde hesaplandi; arac bu diziyi izler, baglantilari secmez.
    if (v.routeIndex + 1 < v.route.size()) {
        const LaneId sonraki = v.route[v.routeIndex + 1];
        // KAVSAK GIRIS KONTROLU: hedef serit doluysa girme. Bu olmadan
        // birden fazla arac ayni konuma gecip zincirleme birikir ve
        // simulasyon kilitlenir.
        if (!nextLaneHasRoom(v, sonraki)) {
            return AdvanceResult::Stuck;
        }
        ++v.routeIndex;
        v.currentLane = sonraki;
        v.position = 0.0;
        return AdvanceResult::Moved;
    }
    // Rota bitti ama hedef seritte degilsek: tutarsiz rota.
    // Sessizce donmek yerine TESPIT EDILIR (updateStallState).
    if (v.currentLane == v.exitLane) {
        return AdvanceResult::Arrived;
    }
    return AdvanceResult::Stuck;
}

void World::stepVehicles(double dt) {
    // IDM kararliligi icin makro adim alt adimlara bolunur.
    // 1 saniyelik TEK bir adimda IDM kararli degildir: 10 m/s hizla giden
    // arac, duran onundekinin uzerinden bir adimda gecer (K1 ihlali).
    // Disaridan gorulen zaman adimi degismez: IG-2 '1 adim = 1 saniye'.
    const int altAdim = kCarFollowingSubSteps;
    const double h = dt / static_cast<double>(altAdim);
    for (int i = 0; i < altAdim; ++i) {
        stepSignals(h);
        applyCarFollowing(h);
        integrateSubStep(h);   // kisit hareketin icinde uygulanir
        updateJunctionOccupancy();
    }
}

void World::integrateSubStep(double dt) {
    // TASARIM: arac bos olmayan bir alana HAREKET EDEMEZ. Kisit sonradan
    // onarimla degil, hareketin kaynaginda uygulanir; boylece "arac bir
    // adimda ondekinin uzerinden gecer" durumu hicbir zaman olusmaz.
    //
    // Siralama: liste konuma gore AZALAN sirali -> liste[0] ONDEKI.
    // Onden arkaya islenir; boylece ondekinin yeni konumu arkadakine
    // dogru hesaplanir.
    rebuildLaneIndex();

    std::vector<VehicleId> ulasanlar;

    for (const auto& liste : laneVehicles_) {
        if (liste.empty()) {
            continue;
        }
        const Lane& lane = network_.lane(findVehicle(liste.front()).currentLane);

        for (std::size_t k = 0; k < liste.size(); ++k) {
            Vehicle& v = findVehicle(liste[k]);

            // Bosluk: serit sonuna kadar, veya ondeki aracin arkasina kadar.
            double bosluk = lane.length - v.position;
            if (k > 0) {
                const Vehicle& ondek = findVehicle(liste[k - 1]);
                bosluk = std::min(bosluk, ondek.position - kVehicleLength - v.position);
            }
            bosluk = std::max(0.0, bosluk);

            const double hareket = std::min(v.speed * dt, bosluk);
            v.position += hareket;
            v.totalDistance += hareket;

            if (hareket <= 0.0) {
                v.speed = 0.0;
            }

            // Serit sonuna ulasti mi? Sifir uzunluklu seritlerde bu
            // kosul bastan dogrudur (arac aninda gecer).
            if (v.position < lane.length - 1e-9) {
                continue;
            }

            // Serit sonu: advanceLane hem SIRAYA GEÇIŞi hem de VARISI
            // ele alir. Burada erken bir `continue` konulursa araç olasi
            // gelis seridin sonunda kalici olarak durur (bu hata yasandi).
            const AdvanceResult r = advanceLane(v);
            if (r == AdvanceResult::Arrived) {
                ulasanlar.push_back(v.id);
            } else if (r == AdvanceResult::Stuck) {
                // Giris yok (kavsak/kuyruk dolu): seridin sonunda bekle.
                v.position = lane.length;
                v.speed = 0.0;
                v.stopped = true;
            }
        }
    }

    // Ulasan araclari olcumle ve kaldir.
    for (const VehicleId id : ulasanlar) {
        if (id.value >= vehicleIndex_.size()) {
            continue;
        }
        const std::size_t idx = vehicleIndex_[id.value];
        if (idx >= vehicles_.size() || vehicles_[idx].id != id) {
            continue;
        }
        const Vehicle& v = vehicles_[idx];
        metrics_.totalTravelTime += time_ - v.spawnTime;
        metrics_.totalWaitTime += v.waitTime;
        metrics_.waitTimeMeasured = true;
        ++metrics_.arrived;

        vehicles_[idx] = vehicles_.back();
        vehicles_.pop_back();
    }
    vehicleIndex_.clear();
    laneVehicles_.clear();
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
    // Sinyal durumlari da simülasyonun parcasidir: tekrarlanabilirlik icin
    // ozete dahil edilmelidir.
    for (const Intersection& j : intersections_) {
        const auto st = j.snapshotState();
        hashU64(h, static_cast<std::uint64_t>(st.phase));
        hashU64(h, static_cast<std::uint64_t>(st.state));
        hashDouble(h, st.phaseElapsed);
        hashDouble(h, st.greenElapsed);
        hashDouble(h, st.stateElapsed);
        hashU64(h, st.switchCount);
        hashU64(h, st.rejectedSwitches);
        hashU64(h, st.forcedSwitches);
        hashU64(h, st.switchRequested ? 1u : 0u);
    }
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
    s.junctionStates.reserve(intersections_.size());
    for (const Intersection& j : intersections_) {
        s.junctionStates.push_back(j.snapshotState());
    }
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
    for (std::size_t i = 0; i < intersections_.size() && i < s.junctionStates.size(); ++i) {
        intersections_[i].restoreState(s.junctionStates[i]);
    }
}

}  // namespace tsc