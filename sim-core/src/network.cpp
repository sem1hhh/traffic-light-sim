#include "tsc/network.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tsc {
namespace {

constexpr int popcount4(std::uint8_t mask) noexcept {
    return ((mask >> 0) & 1u) + ((mask >> 1) & 1u) + ((mask >> 2) & 1u) + ((mask >> 3) & 1u);
}

/// Hucreyi siniflandirir.
enum class CellRole : std::uint8_t {
    Empty,     ///< hicbir sey degil
    Passable,  ///< duz yol veya viraj (2 kol): serit uzerinden gecilir
    Junction,  ///< 3+ kol: dugum, sinyali var
    Stub,      ///< 1 kol: cikmaaz ucu, giris/cikis noktasi
    Invalid,   ///< 3+ kollu ama Junction tipinde degil -> gecersiz yerlesim
};

CellRole classify(const Grid& grid, const Vec2i& c) noexcept {
    const CellType t = grid.at(c);
    if (t == CellType::Empty) {
        return CellRole::Empty;
    }
    const int degree = popcount4(grid.connectivityMask(c));
    if (degree == 0) {
        // Tek basina kalmis hucre: ne dugum ne cikmaaz ucu. Gecerli yerlesim degildir.
        return CellRole::Empty;
    }
    if (degree == 1) {
        return CellRole::Stub;
    }
    if (degree >= 3) {
        return (t == CellType::Junction) ? CellRole::Junction : CellRole::Invalid;
    }
    // 2 kol: duz ya da kose. Junction tipinde olsa bile sinyalsiz baglanti noktasidir.
    return CellRole::Passable;
}

}  // namespace

const char* toString(Movement m) noexcept {
    switch (m) {
        case Movement::Straight: return "straight";
        case Movement::Right:    return "right";
        case Movement::Left:     return "left";
        case Movement::UTurn:    return "u-turn";
    }
    return "?";
}

Movement movementOf(Dir inDir, Dir outDir) noexcept {
    if (outDir == opposite(inDir)) {
        return Movement::Straight;
    }
    if (outDir == turnRight(inDir)) {
        return Movement::Right;
    }
    if (outDir == turnLeft(inDir)) {
        return Movement::Left;
    }
    return Movement::UTurn;
}

Vec2i Lane::stopLine() const noexcept {
    if (cells.empty()) {
        return Vec2i{};
    }
    return cells.back();  // kavsaga en yakin hucre
}

// ---------------------------------------------------------------------------
// Erisim
// ---------------------------------------------------------------------------

const Lane& Network::lane(LaneId id) const {
    if (!id.valid() || id.value >= lanes_.size()) {
        throw std::out_of_range("Network::lane: gecersiz LaneId");
    }
    return lanes_[id.value];
}

const Connection& Network::connection(ConnectionId id) const {
    if (!id.valid() || id.value >= connections_.size()) {
        throw std::out_of_range("Network::connection: gecersiz ConnectionId");
    }
    return connections_[id.value];
}

const Node& Network::node(JunctionId id) const {
    if (!id.valid() || id.value >= nodes_.size()) {
        throw std::out_of_range("Network::node: gecersiz JunctionId");
    }
    return nodes_[id.value];
}

std::vector<JunctionId> Network::signalJunctions() const {
    std::vector<JunctionId> out;
    for (const Node& n : nodes_) {
        if (n.hasSignal) {
            out.push_back(n.id);
        }
    }
    return out;
}

std::vector<LaneId> Network::boundaryLanes() const { return boundaryLanes_; }

std::vector<LaneId> Network::exitLanes() const {
    std::vector<LaneId> out;
    for (const Lane& l : lanes_) {
        if (popcount4(nodes_[l.to.value].arms) == 1) {
            out.push_back(l.id);
        }
    }
    return out;
}

std::vector<ConnectionId> Network::connectionsFrom(LaneId laneId) const {
    std::vector<ConnectionId> out;
    if (!laneId.valid() || laneId.value >= outgoingConnections_.size()) {
        return out;
    }
    for (const std::uint32_t idx : outgoingConnections_[laneId.value]) {
        out.push_back(ConnectionId::from(idx));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Rota: Dijkstra. Deterministik: esit mesafede LaneId sirasiyla kirilir.
// ---------------------------------------------------------------------------

std::vector<std::pair<LaneId, double>> Network::reachableFrom(LaneId start) const {
    const std::size_t n = lanes_.size();
    std::vector<std::pair<LaneId, double>> sonuc;
    if (n == 0 || !start.valid() || start.value >= n) {
        return sonuc;
    }

    const double kSonsuz = std::numeric_limits<double>::infinity();
    std::vector<double> mesafe(n, kSonsuz);

    // std::pair siralamasi: once mesafe, sonra LaneId. Bu siralama deterministiktir.
    using Giris = std::pair<double, std::uint32_t>;
    std::priority_queue<Giris, std::vector<Giris>, std::greater<Giris>> kuyruk;

    mesafe[start.value] = 0.0;
    kuyruk.push({0.0, start.value});

    while (!kuyruk.empty()) {
        const auto [mesafeUst, u] = kuyruk.top();
        kuyruk.pop();

        // bayat kayitlari atla
        if (mesafeUst > mesafe[u]) {
            continue;
        }

        for (const std::uint32_t connIdx : outgoingConnections_[u]) {
            const Connection& c = connections_[connIdx];
            if (!c.allowed) {
                continue;
            }
            const std::uint32_t v = c.toLane.value;
            const double yeni = mesafeUst + lanes_[v].length;
            if (yeni < mesafe[v]) {
                mesafe[v] = yeni;
                kuyruk.push({yeni, v});
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (mesafe[i] != kSonsuz) {
            sonuc.emplace_back(LaneId::from(static_cast<std::uint32_t>(i)), mesafe[i]);
        }
    }
    return sonuc;
}

std::vector<LaneId> Network::shortestPath(LaneId from, LaneId to) const {
    std::vector<LaneId> yol;
    if (!from.valid() || !to.valid()) {
        return yol;
    }
    if (from == to) {
        yol.push_back(from);
        return yol;
    }

    const std::size_t n = lanes_.size();
    if (from.value >= n || to.value >= n) {
        return yol;
    }

    const double kSonsuz = std::numeric_limits<double>::infinity();
    std::vector<double> mesafe(n, kSonsuz);
    std::vector<std::uint32_t> onceki(n, 0xFFFFFFFFu);

    using Giris = std::pair<double, std::uint32_t>;
    std::priority_queue<Giris, std::vector<Giris>, std::greater<Giris>> kuyruk;
    mesafe[from.value] = 0.0;
    kuyruk.push({0.0, from.value});

    while (!kuyruk.empty()) {
        const auto [d, u] = kuyruk.top();
        kuyruk.pop();
        if (d > mesafe[u]) {
            continue;
        }
        if (u == to.value) {
            break;
        }
        for (const std::uint32_t ci : outgoingConnections_[u]) {
            const Connection& c = connections_[ci];
            if (!c.allowed) {
                continue;
            }
            const std::uint32_t v = c.toLane.value;
            const double yeni = d + lanes_[v].length;
            if (yeni < mesafe[v]) {
                mesafe[v] = yeni;
                onceki[v] = u;
                kuyruk.push({yeni, v});
            }
        }
    }

    if (mesafe[to.value] == kSonsuz) {
        return yol;
    }
    for (std::uint32_t cur = to.value; cur != 0xFFFFFFFFu; cur = onceki[cur]) {
        yol.push_back(LaneId::from(cur));
        if (cur == from.value) {
            break;
        }
    }
    std::reverse(yol.begin(), yol.end());
    return yol;
}

bool Network::canReach(LaneId from, LaneId to) const {
    for (const auto& [hedef, mesafe] : reachableFrom(from)) {
        static_cast<void>(mesafe);
        if (hedef == to) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Ag uretimi
// ---------------------------------------------------------------------------

Network buildNetwork(const Grid& grid) {
    if (!grid.valid()) {
        throw std::invalid_argument("buildNetwork: gecersiz izgara");
    }

    Network net;

    // --- 1) Dugumleri belirle. Sirali tarama determinizm icin onemli. -------
    std::vector<Vec2i> dugumHucreleri;
    for (int y = 0; y < grid.size().height; ++y) {
        for (int x = 0; x < grid.size().width; ++x) {
            const Vec2i c{x, y};
            const CellRole rol = classify(grid, c);
            if (rol == CellRole::Junction || rol == CellRole::Stub) {
                dugumHucreleri.push_back(c);
            } else if (rol == CellRole::Invalid) {
                std::ostringstream os;
                os << "Hucre (" << x << "," << y << ") 3 veya daha fazla kola sahip "
                   << "ama Junction degil; kavsak olarak isaretlenmelidir.";
                net.buildWarnings_.push_back(os.str());
            }
        }
    }

    for (const Vec2i& c : dugumHucreleri) {
        Node n;
        n.id = JunctionId::from(static_cast<std::uint32_t>(net.nodes_.size()));
        n.cell = c;
        n.arms = grid.connectivityMask(c);
        n.hasSignal = (classify(grid, c) == CellRole::Junction);
        net.nodes_.push_back(n);
    }

    // Dugum hucre -> indeks (BFS yurutme icin)
    const auto dugumIndeksi = [&](const Vec2i& c) -> int {
        for (std::size_t i = 0; i < net.nodes_.size(); ++i) {
            if (net.nodes_[i].cell == c) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    if (net.nodes_.size() < 2) {
        std::ostringstream os;
        os << "Ag icin en az iki dugum gerekir; bulunan dugum sayisi: " << net.nodes_.size();
        net.buildWarnings_.push_back(os.str());
        return net;
    }

    // --- 2) Her dugumden her kola yuruyerek seritleri uret. ---------------
    for (std::size_t fromIdx = 0; fromIdx < net.nodes_.size(); ++fromIdx) {
        const Node& dugum = net.nodes_[fromIdx];

        for (const Dir d : kAllDirs) {
            if ((dugum.arms & (1u << static_cast<int>(d))) == 0) {
                continue;  // bu yon bir kola sahip degil
            }

            Lane l;
            l.id = LaneId::from(static_cast<std::uint32_t>(net.lanes_.size()));
            l.from = dugum.id;
            l.exitDir = d;

            // Virajlari da takip ederek yuru: her adimda duz devam etmeye
            // calis, yol kesilirse bagli diger yonlardan deterministik olacak
            // sekilde ilk uygun olani sec (kisayol -> sira -> dunya).
            Vec2i cur = dugum.cell;
            Dir dir = d;
            Dir sonYon = d;
            bool izgaraDisi = false;
            bool kotuYol = false;
            std::vector<Vec2i> yol;

            const std::size_t adimSiniri = grid.size().cellCount() + 4;
            for (std::size_t adim = 0; adim < adimSiniri; ++adim) {
                const Vec2i nxt = cur + dirOffset(dir);
                if (!grid.inBounds(nxt)) {
                    izgaraDisi = true;
                    break;
                }
                const CellRole rol = classify(grid, nxt);
                if (rol == CellRole::Junction || rol == CellRole::Stub) {
                    sonYon = dir;
                    cur = nxt;  // HEDEF DUGUMUN HUCResi. Atlanmazsa dugumIndeksi
                               // yanlis dugumu bulur (serit bos "dongu" olur).
                    break;
                }
                if (rol != CellRole::Passable) {
                    // Yol burada kesiliyor ve dugum yok: kotu yerlesim.
                    kotuYol = true;
                    break;
                }
                yol.push_back(nxt);
                cur = nxt;

                // Sonraki yonu sec: duz devam tercih edilir.
                if (grid.isRoadOrJunction(cur + dirOffset(dir))) {
                    continue;
                }
                // GELINDIGI YONE GERI DONMEK YASAKTIR: bu, aracin serit
                // icinde U donmesi demektir (asagida mutlaka geri sarar).
                const Dir geri = opposite(dir);
                Dir secilen = dir;
                bool bulundu = false;
                for (std::size_t k = 1; k < kDirCount && !bulundu; ++k) {
                    const Dir d2 = static_cast<Dir>((static_cast<int>(dir) + static_cast<int>(k)) % 4);
                    if (d2 == geri) {
                        continue;
                    }
                    if (grid.isRoadOrJunction(cur + dirOffset(d2))) {
                        secilen = d2;
                        bulundu = true;
                    }
                }
                if (!bulundu) {
                    kotuYol = true;
                    break;
                }
                dir = secilen;
            }

            if (izgaraDisi || kotuYol) {
                // Bu kolda yol bitmiyor ya da kotu bir hucreye giriyor.
                // Kirilmis yerlesim: serit uretilmez.
                std::ostringstream os;
                os << "Dugum (" << dugum.cell.x << "," << dugum.cell.y << ") yonundeki "
                   << static_cast<int>(d) << " numarali kollari bir dugume ulasmiyor.";
                net.buildWarnings_.push_back(os.str());
                continue;
            }

            const int hedefIdx = dugumIndeksi(cur);
            if (hedefIdx < 0) {
                continue;  // tanimsiz: guvenli sekilde atla
            }

            l.to = net.nodes_[static_cast<std::size_t>(hedefIdx)].id;
            l.entryDir = sonYon;
            l.cells = std::move(yol);
            l.length = static_cast<double>(l.cells.size()) * kCellLength;

            const std::uint32_t laneIdx = l.id.value;
            net.lanes_.push_back(std::move(l));
            net.laneIds_.push_back(LaneId::from(laneIdx));
            net.nodes_[fromIdx].outgoing.push_back(LaneId::from(laneIdx));
            net.nodes_[static_cast<std::size_t>(hedefIdx)].incoming.push_back(LaneId::from(laneIdx));

            // Cikmaaz ucu: baslangic dugumu tek kollu ise bu serit bir "sinir seridi"tir.
            if (popcount4(net.nodes_[fromIdx].arms) == 1) {
                net.boundaryLanes_.push_back(LaneId::from(laneIdx));
            }
            if (popcount4(net.nodes_[static_cast<std::size_t>(hedefIdx)].arms) == 1) {
                net.exitLanes_.push_back(LaneId::from(laneIdx));
            }
        }
    }

    net.outgoingConnections_.assign(net.lanes_.size(), {});

    // --- 3) Sinyalli kavsaklarda baglantilar uret. ------------------------
    // Cikmaz uclarinda baglanti olusmaz: tek kollari vardir ve tek gecis
    // U donusudur, o da uretilmez.
    for (std::size_t nIdx = 0; nIdx < net.nodes_.size(); ++nIdx) {
        const Node& dugum = net.nodes_[nIdx];
        if (!dugum.hasSignal) {
            continue;
        }

        for (const LaneId inLaneId : dugum.incoming) {
            const Lane& inLane = net.lanes_[inLaneId.value];
            // Yaklasma yonu: kavsaga GIRIS adiminin yonu.
            const Dir inDir = inLane.entryDir;

            for (const LaneId outLaneId : dugum.outgoing) {
                const Lane& outLane = net.lanes_[outLaneId.value];
                const Dir outDir = outLane.exitDir;
                const Movement mv = movementOf(inDir, outDir);

                Connection c;
                c.id = ConnectionId::from(static_cast<std::uint32_t>(net.connections_.size()));
                c.junction = dugum.id;
                c.fromLane = inLaneId;
                c.toLane = outLaneId;
                c.inDir = inDir;
                c.outDir = outDir;
                c.movement = mv;
                c.permissive = (mv == Movement::Left);
                c.allowed = (mv != Movement::UTurn);

                net.outgoingConnections_[inLaneId.value].push_back(c.id.value);
                net.connectionIds_.push_back(c.id);
                net.connections_.push_back(c);
            }
        }
    }

    return net;
}

// ---------------------------------------------------------------------------
// Dogrulama
// ---------------------------------------------------------------------------

NetworkErrors validateNetwork(const Network& net) {
    NetworkErrors sonuc;

    for (const std::string& w : net.buildWarnings()) {
        sonuc.errors.push_back(w);
    }

    if (net.nodeCount() == 0 || net.laneCount() == 0) {
        sonuc.errors.push_back("Yol agi bos. En az bir kavsak ve iki cikmaaz ucu gerekir.");
        return sonuc;
    }

    if (net.boundaryLanes().size() < 2) {
        std::ostringstream os;
        os << "Giris/cikis icin en az iki cikmaaz ucu gerekir; bulunan: "
           << net.boundaryLanes().size();
        sonuc.errors.push_back(os.str());
    }

    // Her giris ucundan en az bir cikis ucuna ulasilabilmeli.
    // Ulasilamayan hedef varsa simülasyon baslatilmaz.
    for (const LaneId inLane : net.boundaryLanes()) {
        const std::size_t baslangicIndex = net.boundaryLanes().size();
        static_cast<void>(baslangicIndex);

        bool birCikisaUlasti = false;
        for (const auto& [serit, mesafe] : net.reachableFrom(inLane)) {
            static_cast<void>(mesafe);
            // Cikis ucu, bu seridin BITTIGI dugumun tek kollu olmasidir.
            const Lane& l = net.lane(serit);
            const Node& hedef = net.node(l.to);
            bool cikisUcuMu = false;
            for (const LaneId bl : net.boundaryLanes()) {
                if (net.lane(bl).to == hedef.id) {
                    cikisUcuMu = true;
                    break;
                }
            }
            if (cikisUcuMu) {
                birCikisaUlasti = true;
                break;
            }
        }
        if (!birCikisaUlasti) {
            std::ostringstream os;
            os << "Serit " << inLane.value << " baslangic noktasindan hicbir cikis noktasina "
               << "ulasilamiyor. Ulasilamayan hedef (IG-1).";
            sonuc.errors.push_back(os.str());
        }
    }

    return sonuc;
}

}  // namespace tsc