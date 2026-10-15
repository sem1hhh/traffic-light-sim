#pragma once

#include "tsc/grid.hpp"
#include "tsc/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Yol aginin semasi
//
// Izgara (grid.hpp) hucreleri tutar; bu baslik o hucrelerden *turetilen*
// yonlu serit ve kavsak hareket grafigini tanimlar. Ikisi ayridir: izgara
// kullanici tarafindan duzenlenir, ag ise ondan turerilir.
//
// Cizim kurallari:
//  - Yon basina tek serit vardir; sollama / serit degistirme yoktur.
//  - Bir hucre 3 veya daha fazla kola sahipse KAVSAK sayilir ve sinyali vardir.
//    2 kollu hucre (dusey ya da duz) kavsak DEGILDIR: yalnizca bir baglanti
//    noktasidir, sinyali ve cakismasi yoktur.
//  - Tek komsulu hucre bir CIKMAZ UCUDUR. Yalnizca arac giris/cikis noktasi
//    olarak kullanilir; araclarin rotasi icinden gecmez.
//  - Hicbir hareket sinyali baypas edemez: serbest saga donus YOKTUR.
// ---------------------------------------------------------------------------

/// Izgara bir hucreyi kac metre sayariz?
constexpr double kCellLength = 10.0;

/// Kavsakta bir aracin izledigi hareket.
enum class Movement : std::uint8_t { Straight = 0, Right = 1, Left = 2, UTurn = 3 };

const char* toString(Movement m) noexcept;

/// inDir yonunden outDir yonune gecis hangi harekettir?
Movement movementOf(Dir inDir, Dir outDir) noexcept;

/// Yon basina tek serit oldugu icin bu daima 1. Yine de acik bir sabit:
/// serit sayisi artarsa kuyruk hesaplari buradan genisler.
constexpr int kLanesPerDirection = 1;

// ---------------------------------------------------------------------------
// Serit: iki dugum arasindaki yonlu yol parcasi.
// ---------------------------------------------------------------------------
struct Lane {
    LaneId id;
    JunctionId from{};                 ///< baslangic dugumu
    JunctionId to{};                   ///< bitis dugumu
    Dir exitDir = Dir::North;          ///< from dugumunden ilk adim yonu
    Dir entryDir = Dir::North;         ///< `to` dugumune SON adim yonu
    std::vector<Vec2i> cells;          ///< from -> to yonunde, iki dugumun hucresi de HARIC
    double length = 0.0;               ///< hucre sayisi * kCellLength

    /// Serit duz cizgi degildir: yol virajlardan gecerek donebilir.
    /// cells sirali ve ardisik hucrelerdir, ancak tek bir yonde ilerlemez.

    /// Arac bu seritte ilerlerken bulunabilecegi en son konum (stop line).
    /// Son hucredir; kavsak girisi burada engellenir.
    Vec2i stopLine() const noexcept;

    bool valid() const noexcept { return id.valid() && from.valid() && to.valid(); }
};

// ---------------------------------------------------------------------------
// Baglanti: kavsak icinden bir seritten digerine gecis.
// ---------------------------------------------------------------------------
struct Connection {
    ConnectionId id;
    JunctionId junction{};
    LaneId fromLane{};
    LaneId toLane{};
    Dir inDir = Dir::North;     ///< kavsaga yaklasma yonu
    Dir outDir = Dir::North;    ///< kavsaktan cikis yonu
    Movement movement = Movement::Straight;

    /// Sol donusler "yol ver" (permissive) seklindedir: karsi yoldan bosluk
    /// ararlarken karar rastgelelik icerir. Duz ve saga donus korumali
    /// (protected) tir: yalnizca kendi yesillik doneminde gecerler.
    bool permissive = false;

    /// U donusu varsayilan olarak kullanilamaz (geri donus dongusu riski).
    bool allowed = true;

    bool valid() const noexcept { return id.valid() && junction.valid(); }
};

// ---------------------------------------------------------------------------
// Dugum: ya bir kavsak ya da bir cikmaaz ucu.
// ---------------------------------------------------------------------------
struct Node {
    JunctionId id{};
    Vec2i cell{};
    std::uint8_t arms = 0;             ///< komsu yol/kavsak yon maskesi
    bool hasSignal = false;            ///< 3+ kol => trafik isigi var
    std::vector<LaneId> incoming;      ///< bu dugume gelen seritler
    std::vector<LaneId> outgoing;      ///< bu dugumden cikan seritler
};

// ---------------------------------------------------------------------------
// Yol agi
// ---------------------------------------------------------------------------
class Network {
public:
    Network() = default;

    std::size_t laneCount() const noexcept { return lanes_.size(); }
    std::size_t connectionCount() const noexcept { return connections_.size(); }
    std::size_t nodeCount() const noexcept { return nodes_.size(); }

    const Lane& lane(LaneId id) const;
    const Connection& connection(ConnectionId id) const;
    const Node& node(JunctionId id) const;

    const std::vector<LaneId>& allLanes() const noexcept { return laneIds_; }

    /// Uretim sirasinda tespit edilen, agi kullanilamaz yapan sorunlar.
    const std::vector<std::string>& buildWarnings() const noexcept { return buildWarnings_; }

    /// Sinyalli kavsaklar (faz plani olan dugumler).
    std::vector<JunctionId> signalJunctions() const;

    /// Giriş/cikis yapilabilecek cikmaaz uclari.
    std::vector<LaneId> boundaryLanes() const;

    /// Bir seritten cikan tum baglantilar.
    std::vector<ConnectionId> connectionsFrom(LaneId laneId) const;

    /// Bir seritten ulasilabilen seritler: en kisa yol mesafeleriyle.
    /// Deterministiktir; esitlik durumunda LaneId sirasiyla kirilir.
    std::vector<std::pair<LaneId, double>> reachableFrom(LaneId start) const;

    /// from seritinden to seritine yol var mi?
    bool canReach(LaneId from, LaneId to) const;

private:
    friend Network buildNetwork(const Grid&);

    std::vector<Lane> lanes_;
    std::vector<Connection> connections_;
    std::vector<Node> nodes_;
    std::vector<LaneId> laneIds_;
    std::vector<ConnectionId> connectionIds_;
    std::vector<LaneId> boundaryLanes_;
    /// laneId -> o seritten cikan baglanti indisleri (komsuluk icin)
    std::vector<std::vector<std::uint32_t>> outgoingConnections_;
    /// Uretim sirasinda tespit edilen, agi kullanilamaz yapan sorunlar.
    /// validateNetwork() bunlari kullaniciya bildirir.
    std::vector<std::string> buildWarnings_;
};

// ---------------------------------------------------------------------------
// Ag uretimi ve dogrulama
// ---------------------------------------------------------------------------

/// Izgaradan serit/baglanti agini uretir.
/// Cikmaz uclar (deg 1 hucreler) aga `boundaryLanes_` olarak eklenir.
/// Yukaridaki kurallar uygulanir: 3+ kol kavsak, 2 kol baglanti noktasi,
/// U donusu uretilir ama `allowed = false`.
Network buildNetwork(const Grid& grid);

/// Agin gecerlilik denetimi. Sorun yoksa `ok` true ve `errors` bos.
///
/// Denetimler:
///  - ag bos degil
///  - hucreler bagli (Grid::isConnected)
///  - 3 veya daha fazla kola sahip hucreler Junction tipinde
///  - Junction tipindeki hucreler 3+ kol sahip (2 kollu Junction = viraj, reddedilir)
///  - en az iki cikmaaz ucu var
struct NetworkErrors {
    std::vector<std::string> errors;
    bool ok() const noexcept { return errors.empty(); }
};

NetworkErrors validateNetwork(const Network& net);

}  // namespace tsc