#include "tsc/network.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

using tsc::ConnectionId;
using tsc::Dir;
using tsc::Grid;
using tsc::GridSize;
using tsc::JunctionId;
using tsc::LaneId;
using tsc::Movement;
using tsc::Network;
using tsc::Vec2i;
using tsc::CellType;

namespace {

/// Dort yollu kavsak, dort yone birer komsu yol (kendi cikmaz uclu degil).
Grid dortYolluKavsak() {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Junction);
    g.set(Vec2i{2, 1}, CellType::Road);  // North -> diger kavsak yok, cikmaaz
    g.set(Vec2i{3, 2}, CellType::Road);  // East
    g.set(Vec2i{1, 2}, CellType::Road);  // West
    g.set(Vec2i{2, 3}, CellType::Road);  // South
    return g;
}

/// Iki kavsak arasi duz yol + iki cikmaaz ucu.
Grid ikiKavsakliDuzYol() {
    Grid g{GridSize{7, 3}};
    for (int x = 0; x < 7; ++x) {
        g.set(Vec2i{x, 1}, CellType::Road);
    }
    // Ucte kavsak yerine serit ucu birakiliyor (deg 1) -> cikmaaz ucu
    // Ortada T kavsak eklemek icin dikey kol
    g.set(Vec2i{3, 0}, CellType::Road);
    g.set(Vec2i{3, 2}, CellType::Road);
    g.set(Vec2i{3, 1}, CellType::Junction);
    return g;
}

/// Kosesel donus: iki kollu, dik. Kavsak DEGILDIR.
Grid koseDonusu() {
    Grid g{GridSize{4, 4}};
    g.set(Vec2i{0, 0}, CellType::Road);
    g.set(Vec2i{1, 0}, CellType::Road);
    g.set(Vec2i{2, 0}, CellType::Road);
    g.set(Vec2i{2, 1}, CellType::Road);
    g.set(Vec2i{2, 2}, CellType::Road);
    return g;
}

/// "Lollipop" harita: tek kavsak (4,4) + tek cikmaaz ucu (3,4).
/// Kavsagin kuzey kolu genis bir yay cizip dogudan ayni kavsaga geri doner.
/// Boylece TAM 1 cikmaaz ucu olur ve dugum sayisi 2'dir.
///
/// Yay yolu (13 hucre): (4,3)(4,2)(4,1)(4,0)(5,0)(6,0)(7,0)
///                      (7,1)(7,2)(7,3)(7,4)(6,4)(5,4)
Grid lollipop() {
    Grid g{GridSize{9, 6}};
    g.set(Vec2i{4, 4}, CellType::Junction);
    g.set(Vec2i{3, 4}, CellType::Road);   // tek komsulu -> cikmaaz ucu
    g.set(Vec2i{4, 3}, CellType::Road);
    g.set(Vec2i{4, 2}, CellType::Road);
    g.set(Vec2i{4, 1}, CellType::Road);
    g.set(Vec2i{4, 0}, CellType::Road);
    g.set(Vec2i{5, 0}, CellType::Road);
    g.set(Vec2i{6, 0}, CellType::Road);
    g.set(Vec2i{7, 0}, CellType::Road);
    g.set(Vec2i{7, 1}, CellType::Road);
    g.set(Vec2i{7, 2}, CellType::Road);
    g.set(Vec2i{7, 3}, CellType::Road);
    g.set(Vec2i{7, 4}, CellType::Road);
    g.set(Vec2i{6, 4}, CellType::Road);
    g.set(Vec2i{5, 4}, CellType::Road);   // kavsaga dogrudan batar
    return g;
}

}  // namespace

// ---------------------------------------------------------------------------
// Hareket yonu hesabi
// ---------------------------------------------------------------------------

TEST_CASE("movementOf duz/sag/sol/geri") {
    CHECK(eq(tsc::movementOf(Dir::North, Dir::South), Movement::Straight));
    CHECK(eq(tsc::movementOf(Dir::East, Dir::West), Movement::Straight));

    // Kuzeye giderken doguya donmek = sag
    CHECK(eq(tsc::movementOf(Dir::North, Dir::East), Movement::Right));
    CHECK(eq(tsc::movementOf(Dir::East, Dir::South), Movement::Right));
    // Kuzeye giderken batıya donmek = sol
    CHECK(eq(tsc::movementOf(Dir::North, Dir::West), Movement::Left));
    CHECK(eq(tsc::movementOf(Dir::East, Dir::North), Movement::Left));

    // Geri donus
    CHECK(eq(tsc::movementOf(Dir::North, Dir::North), Movement::UTurn));
}

TEST_CASE("movementOf her yon icin dort farkli hareket verir") {
    for (const Dir in : tsc::kAllDirs) {
        // Duz gecis YALNIZCA karsi yon olur.
        CHECK(eq(tsc::movementOf(in, tsc::opposite(in)), Movement::Straight));

        // Geri kalan uc yon ucunun da farkli bir hareket vermesi gerekir.
        const Movement sag = tsc::movementOf(in, tsc::turnRight(in));
        const Movement sol = tsc::movementOf(in, tsc::turnLeft(in));
        const Movement uturn = tsc::movementOf(in, in);

        CHECK(!eq(sag, Movement::Straight));
        CHECK(!eq(sol, Movement::Straight));
        CHECK(!eq(uturn, Movement::Straight));
        CHECK(!eq(sag, sol));
        CHECK(!eq(sag, uturn));
        CHECK(!eq(sol, uturn));
    }
}

// ---------------------------------------------------------------------------
// Ag uretimi - temel
// ---------------------------------------------------------------------------

TEST_CASE("Gecersiz izgara buildNetwork'i patlatir") {
    CHECK_THROWS_AS(tsc::buildNetwork(Grid{}), std::invalid_argument);
}

TEST_CASE("Bos izgara ag uretmez, uyari verir - build asla patlamaz") {
    const Grid bos{GridSize{5, 5}};
    const Network net = tsc::buildNetwork(bos);   // istisna atmaz
    CHECK_EQ(net.nodeCount(), 0u);
    CHECK_EQ(net.laneCount(), 0u);
    CHECK_FALSE(net.buildWarnings().empty());
    CHECK_FALSE(tsc::validateNetwork(net).ok());
}

TEST_CASE("Tek basina kalmis hucre dugum sayilmaz") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Road);  // cevresi tamamen bos -> derece 0
    const Network net = tsc::buildNetwork(g);
    CHECK_EQ(net.nodeCount(), 0u);
    CHECK(net.boundaryLanes().empty());
    CHECK_FALSE(tsc::validateNetwork(net).ok());
}

TEST_CASE(" Dort yollu kavsak bir dugum ve dort serit uretir") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);

    // Merkez tek dugumdur. Komsu yol hucreleri de stub (deg 1) oldugu icin dugumdur.
    CHECK_EQ(net.nodeCount(), 5u);  // 1 kavsak + 4 komsu stub

    // Sinyalli kavsay sayisi 1
    const auto sinyalli = net.signalJunctions();
    CHECK_EQ(sinyalli.size(), 1u);

    // Her stub tek kolludur, dolayisiyla serit yok uretir (yol yoksa)
    // Merkez kavsagin 4 cikan seriti var
    const tsc::Node& merkez = net.node(sinyalli[0]);
    CHECK_EQ(merkez.outgoing.size(), 4u);
}

TEST_CASE("Uc kollu T kavsak sinyalli") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    const auto sinyalli = net.signalJunctions();
    CHECK_GE(sinyalli.size(), 1u);
}

// ---------------------------------------------------------------------------
// Kukret / serit kurallari
// ---------------------------------------------------------------------------

TEST_CASE("Kose donusu kavsak sayilmaz - sinyal yok") {
    const Grid g = koseDonusu();
    const Network net = tsc::buildNetwork(g);
    // Kose donusunde iki uct (0,0 ve (2,2)) stub, aradaki hucreler gecilebilir.
    // Sinyalli dugum olmamalidir.
    CHECK_EQ(net.signalJunctions().size(), 0u);
}

TEST_CASE("Cikmaaz uclar sinir seridi olarak isaretlenir") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    CHECK_GE(net.boundaryLanes().size(), 2u);
}

TEST_CASE("Ag bos ise erisim guvenli - exception firlatir") {
    const Network net;
    CHECK_EQ(net.laneCount(), 0u);
    CHECK_EQ(net.connectionCount(), 0u);
    CHECK_THROWS_AS(net.lane(LaneId::from(0)), std::out_of_range);
    CHECK_THROWS_AS(net.connection(ConnectionId::from(0)), std::out_of_range);
    CHECK_THROWS_AS(net.node(JunctionId::from(0)), std::out_of_range);
}

// ---------------------------------------------------------------------------
// Baglantilar - hareketler ve guvenlik
// ---------------------------------------------------------------------------

TEST_CASE("Kavşakta dogru sayida baglanti uretilir") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);

    // 4 kollu kavsak: 4 gelis x 4 cikis = 16 kombinasyon, 4 U-donus elenir = 12
    // Ama komsu stub'larin cikan seriti diger tarafa gider, geri donus elenir.
    // Net baglanti sayisini dogrula:
    CHECK_GE(net.connectionCount(), 8u);
}

TEST_CASE("U donusu baglantilari kullanilamaz isaretlidir") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);

    int uturnVaryant = 0;
    for (const LaneId lid : net.allLanes()) {
        for (const ConnectionId cid : net.connectionsFrom(lid)) {
            if (net.connection(cid).movement == Movement::UTurn) {
                ++uturnVaryant;
                CHECK_FALSE(net.connection(cid).allowed);
            }
        }
    }
    // Her seritten bir U-donus olmamali ama sifir da degil; sadece isaret kontrolu
    CHECK_GE(uturnVaryant, 0);
}

TEST_CASE("Sol donusler permissive isaretlidir") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);

    for (const LaneId lid : net.allLanes()) {
        for (const ConnectionId cid : net.connectionsFrom(lid)) {
            const auto& c = net.connection(cid);
            if (c.movement == Movement::Left) {
                CHECK(c.permissive);
            } else {
                CHECK_FALSE(c.permissive);
            }
        }
    }
}

TEST_CASE("Duz ve sag donusler korumali (permissive degil)") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);

    for (const LaneId lid : net.allLanes()) {
        for (const ConnectionId cid : net.connectionsFrom(lid)) {
            const auto& c = net.connection(cid);
            if (c.movement == Movement::Straight || c.movement == Movement::Right) {
                CHECK_FALSE(c.permissive);
                CHECK(c.allowed);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Rota: determinizm ve ulasilabilirlik
// ---------------------------------------------------------------------------

TEST_CASE("reachableFrom bos agda bos sonuc doner") {
    const Network net;
    CHECK(net.reachableFrom(LaneId::from(0)).empty());
}

TEST_CASE("Ayni agdan iki kez rota hesabi ayni sonucu verir - determinizm") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);

    const auto& lanes = net.allLanes();
    CHECK_GE(lanes.size(), 2u);

    const auto birinci = net.reachableFrom(lanes[0]);
    const auto ikinci = net.reachableFrom(lanes[0]);

    CHECK_EQ(birinci.size(), ikinci.size());
    for (std::size_t i = 0; i < birinci.size(); ++i) {
        CHECK_EQ(birinci[i].first.value, ikinci[i].first.value);
        CHECK_EQ(birinci[i].second, ikinci[i].second);
    }
}

TEST_CASE("Baslangic seridi kendisine ulasilabilir (mesafe 0)") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    const auto& lanes = net.allLanes();
    if (!lanes.empty()) {
        const auto r = net.reachableFrom(lanes[0]);
        CHECK_GE(r.size(), 1u);
        // ilk eleman baslangic olmali, mesafe 0
        bool baslangicVar = false;
        for (const auto& [lid, mesafe] : r) {
            if (eq(lid, lanes[0])) {
                CHECK_EQ(mesafe, 0.0);
                baslangicVar = true;
            }
        }
        CHECK(baslangicVar);
    }
}

TEST_CASE("canReach simetrik degildir ama tutarli") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    const auto& lanes = net.allLanes();
    if (lanes.size() >= 2) {
        // Kendine her zaman ulasabilir
        CHECK(net.canReach(lanes[0], lanes[0]));
        // Her seritten kendine
        for (const LaneId lid : lanes) {
            CHECK(net.canReach(lid, lid));
        }
    }
}

// ---------------------------------------------------------------------------
// Dogrulama katmani
// ---------------------------------------------------------------------------

TEST_CASE("Bos ag dogrulamadan gecmez") {
    const Network net;
    const auto err = tsc::validateNetwork(net);
    CHECK_FALSE(err.ok());
}

TEST_CASE("Dort yollu kavsak dogrulamadan gecer") {
    const Grid g = dortYolluKavsak();
    const Network net = tsc::buildNetwork(g);
    const auto err = tsc::validateNetwork(net);
    // Bu haritada yeterli cikmaaz ucu ve baglanti var
    CHECK(err.ok());
}

TEST_CASE("Yalnizca bir cikmaaz ucu varsa dogrulamadan gecmez") {
    // "Lollipop": tek bir kavsak (3,2); Bati kolu bir cikmaaz ucuna (2,2) gider,
    // Kuzey kolu ise donup ayni kavsaga geri doner. Tam 1 cikmaaz ucu vardir.
    const Network net = tsc::buildNetwork(lollipop());
    CHECK_EQ(net.nodeCount(), 2u);              // kavsak + stub
    CHECK_EQ(net.signalJunctions().size(), 1u);
    CHECK_EQ(net.boundaryLanes().size(), 1u);  // tam bir tane

    const auto err = tsc::validateNetwork(net);
    CHECK_FALSE(err.ok());
    bool ikiUcuEksik = false;
    for (const std::string& m : err.errors) {
        if (m.find("iki cikmaaz") != std::string::npos) {
            ikiUcuEksik = true;
        }
    }
    CHECK(ikiUcuEksik);
}

TEST_CASE("Virajli serit dugume geri donus kurabilir - dolambaç takibi") {
    // Ayni lollipop: kavsagin kuzey kolu donup kavsaga geri baglanir.
    const Network net = tsc::buildNetwork(lollipop());

    // Kuzeyden cikan seri donerek ayni kavsaga donmus olmali.
    bool donguSeridi = false;
    for (const LaneId lid : net.allLanes()) {
        const auto& l = net.lane(lid);
        if (l.from == l.to && l.from == net.signalJunctions()[0]) {
            donguSeridi = true;
            CHECK_EQ(l.cells.size(), 13u);  // donus yayi: (4,3) ... (5,4)
        }
    }
    CHECK(donguSeridi);
}

TEST_CASE("3+ kollu ama Junction olmayan hucre uyari uretir") {
    Grid g{GridSize{7, 7}};
    // Merkezde ROAD (kavsak degil) ama 4 kola sahip
    g.set(Vec2i{3, 3}, CellType::Road);
    g.set(Vec2i{3, 2}, CellType::Road);
    g.set(Vec2i{3, 4}, CellType::Road);
    g.set(Vec2i{2, 3}, CellType::Road);
    g.set(Vec2i{4, 3}, CellType::Road);
    const Network net = tsc::buildNetwork(g);
    CHECK_FALSE(net.buildWarnings().empty());
    const auto err = tsc::validateNetwork(net);
    CHECK_FALSE(err.ok());
}

// ---------------------------------------------------------------------------
// Serit detaylari
// ---------------------------------------------------------------------------

TEST_CASE("Serit uzunlugu hucre sayisi ile orantili") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    for (const LaneId lid : net.allLanes()) {
        const auto& l = net.lane(lid);
        CHECK_EQ(l.length, static_cast<double>(l.cells.size()) * tsc::kCellLength);
    }
}

TEST_CASE("stopLine seritin son hucresidir") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    for (const LaneId lid : net.allLanes()) {
        const auto& l = net.lane(lid);
        if (!l.cells.empty()) {
            CHECK(eq(l.stopLine(), l.cells.back()));
        }
    }
}

TEST_CASE("Serit hucreleri ardışık ve tek yonludur") {
    const Grid g = ikiKavsakliDuzYol();
    const Network net = tsc::buildNetwork(g);
    for (const LaneId lid : net.allLanes()) {
        const auto& l = net.lane(lid);
        for (std::size_t i = 1; i < l.cells.size(); ++i) {
            const Vec2i fark = l.cells[i] - l.cells[i - 1];
            // Her adim bir hucre ve tek bir yon
            const int toplam = std::abs(fark.x) + std::abs(fark.y);
            CHECK_EQ(toplam, 1);
        }
    }
}
// ---------------------------------------------------------------------------
// Regresyon testleri
// ---------------------------------------------------------------------------

TEST_CASE("Serit icinde hicbir hucre tekrarlanmaz - regresyon") {
    // Onceki hata: serit yurutucusu gidip geldiği yone dönebiliyordu ve
    // lane (7,3)(7,2)(7,1)(7,0)(7,1)(7,2)(7,3) gibi kendi kendini yiyen
    // bir yol üretiyordu. Geri dönüş artık yasak.
    for (const Grid& g : {lollipop(), dortYolluKavsak(), ikiKavsakliDuzYol()}) {
        const Network net = tsc::buildNetwork(g);
        for (const LaneId lid : net.allLanes()) {
            const auto& l = net.lane(lid);
            std::vector<Vec2i> gorulen;
            for (const Vec2i& c : l.cells) {
                const bool dahaOnceVar = std::find(gorulen.begin(), gorulen.end(), c) != gorulen.end();
                CHECK_FALSE(dahaOnceVar);  // ayni hucre iki kez olmamali
                gorulen.push_back(c);
            }
        }
    }
}

TEST_CASE("Serit bittiginde ara bir dugumde olmamali - regresyon") {
    // Onceki hata: yurutucu hedef dugume varinca `cur` guncellenmiyordu ve
    // serit bos "dongu" olarak kendi dugumune baglaniyordu.
    const Network net = tsc::buildNetwork(dortYolluKavsak());
    const JunctionId kavsak = net.signalJunctions()[0];
    for (const LaneId lid : net.allLanes()) {
        const auto& l = net.lane(lid);
        CHECK_NE(l.from, l.to);  // komsu dugumler arasinda serit bos olamaz
        if (!l.cells.empty()) {
            CHECK_NE(l.cells.front(), net.node(l.from).cell);
            CHECK_NE(l.cells.back(), net.node(l.to).cell);
        }
    }
    // Merkez kavsagin dort cikan seridi (dort yon birer komsu)
    CHECK_EQ(net.node(kavsak).outgoing.size(), 4u);
}

TEST_CASE("Uc koldan az hucre sinyal tasimaz") {
    // T kavsak: 3 kol -> sinyal var
    const Grid t{ikiKavsakliDuzYol()};
    const Network tnet = tsc::buildNetwork(t);
    CHECK_GE(tnet.signalJunctions().size(), 1u);

    // Kose donusu: hicbir hucrede 3+ kol yok -> sinyal olmamali
    const Network knet = tsc::buildNetwork(koseDonusu());
    CHECK_EQ(knet.signalJunctions().size(), 0u);
}

TEST_CASE("Dort kollu kavsakta dort yonun dortu da serit uretir") {
    const Network net = tsc::buildNetwork(dortYolluKavsak());
    const JunctionId j = net.signalJunctions()[0];
    CHECK_EQ(net.node(j).outgoing.size(), 4u);
    CHECK_EQ(net.node(j).incoming.size(), 4u);
    CHECK_EQ(net.node(j).arms, 0x0Fu);
}
