#include "tsc/grid.hpp"

#include "test_util.hpp"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

namespace {

constexpr std::uint8_t bit(tsc::Dir d) {
    return static_cast<std::uint8_t>(1u << static_cast<int>(d));
}

/// Duz yatay yol: (0,y) ... (w-1,y) boyunca Road.
tsc::Grid yatayYol(int w, int h, int y) {
    tsc::Grid g{tsc::GridSize{w, h}};
    for (int x = 0; x < w; ++x) {
        g.set(tsc::Vec2i{x, y}, tsc::CellType::Road);
    }
    return g;
}

}  // namespace

using tsc::CellType;
using tsc::Dir;
using tsc::Grid;
using tsc::GridSize;
using tsc::Vec2i;

// ---------------------------------------------------------------------------
// Yon yardimcilari
// ---------------------------------------------------------------------------

TEST_CASE("yon ofsetleri") {
    CHECK(eq(tsc::dirOffset(Dir::North), (Vec2i{0, -1})));
    CHECK(eq(tsc::dirOffset(Dir::East), (Vec2i{1, 0})));
    CHECK(eq(tsc::dirOffset(Dir::South), (Vec2i{0, 1})));
    CHECK(eq(tsc::dirOffset(Dir::West), (Vec2i{-1, 0})));
}

TEST_CASE("karsi yon ve donusler tutarli") {
    for (const Dir d : tsc::kAllDirs) {
        CHECK(eq(tsc::opposite(tsc::opposite(d)), d));
        // 90° x 4 = 360° donusumun kimligidir (3 donus 270° verir, kimlik degil).
        CHECK(eq(tsc::turnRight(tsc::turnRight(tsc::turnRight(tsc::turnRight(d)))), d));
        CHECK(eq(tsc::turnLeft(tsc::turnLeft(tsc::turnLeft(tsc::turnLeft(d)))), d));
        CHECK(ne(d, tsc::opposite(d)));  // kendi karsisi olamaz
    }
    CHECK(eq(tsc::turnRight(Dir::North), Dir::East));
    CHECK(eq(tsc::turnLeft(Dir::North), Dir::West));
}

TEST_CASE("duz gecis tespiti") {
    CHECK(tsc::isStraight(Dir::North, Dir::South));
    CHECK(tsc::isStraight(Dir::East, Dir::West));
    CHECK_FALSE(tsc::isStraight(Dir::North, Dir::East));
    CHECK_FALSE(tsc::isStraight(Dir::East, Dir::North));
}

TEST_CASE("kAllDirs dort benzersiz yon icerir") {
    CHECK_EQ(tsc::kAllDirs.size(), 4u);
}

// ---------------------------------------------------------------------------
// Grid temel davranislari
// ---------------------------------------------------------------------------

TEST_CASE("Grid varsayilan olarak bos ve gecersizdir") {
    const Grid g;
    CHECK_FALSE(g.valid());
    CHECK(g.empty());
    CHECK_EQ(g.totalOccupied(), 0u);
}

TEST_CASE("Gecersiz boyut reddedilir - uct durum") {
    CHECK_THROWS_AS(Grid(GridSize{0, 5}), std::invalid_argument);
    CHECK_THROWS_AS(Grid(GridSize{5, 0}), std::invalid_argument);
    CHECK_THROWS_AS(Grid(GridSize{-3, 5}), std::invalid_argument);
}

TEST_CASE("Izgara olusturulunca tum hucreler bos") {
    Grid g{GridSize{4, 3}};
    CHECK(g.valid());
    CHECK_EQ(g.size().cellCount(), 12u);
    CHECK(g.empty());
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            CHECK(eq(g.at(Vec2i{x, y}), CellType::Empty));
        }
    }
}

TEST_CASE("Hucre disina cikmada get guvenli, set hata firlatir") {
    Grid g{GridSize{3, 3}};
    CHECK(eq(g.at(Vec2i{-1, 0}), CellType::Empty));
    CHECK(eq(g.at(Vec2i{0, -1}), CellType::Empty));
    CHECK(eq(g.at(Vec2i{3, 0}), CellType::Empty));
    CHECK(eq(g.at(Vec2i{0, 3}), CellType::Empty));

    CHECK_THROWS_AS(g.set(Vec2i{3, 3}, CellType::Road), std::out_of_range);
    CHECK_THROWS_AS(g.set(Vec2i{-1, 0}, CellType::Road), std::out_of_range);
}

TEST_CASE("set ve get calisir") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 3}, CellType::Junction);
    CHECK(eq(g.at(Vec2i{2, 3}), CellType::Junction));
    CHECK_EQ(g.count(CellType::Junction), 1u);
    CHECK_EQ(g.count(CellType::Road), 0u);
    CHECK_EQ(g.totalOccupied(), 1u);
    CHECK_FALSE(g.empty());
}

TEST_CASE("neighbour ve sinir kontrolu") {
    Grid g{GridSize{3, 3}};
    g.set(Vec2i{1, 1}, CellType::Road);
    g.set(Vec2i{1, 0}, CellType::Road);

    CHECK(eq(g.neighbour(Vec2i{1, 1}, Dir::North), CellType::Road));
    CHECK(eq(g.neighbour(Vec2i{1, 1}, Dir::South), CellType::Empty));

    // (1,1) merkez hucrenin dort komsusu da 3x3 izgara icinde.
    for (const Dir d : tsc::kAllDirs) {
        CHECK(g.neighbourInBounds(Vec2i{1, 1}, d));
    }

    // Kosede komsularin yarisi disarida.
    CHECK_FALSE(g.neighbourInBounds(Vec2i{0, 0}, Dir::North));
    CHECK_FALSE(g.neighbourInBounds(Vec2i{0, 0}, Dir::West));
    CHECK(g.neighbourInBounds(Vec2i{0, 0}, Dir::East));
    CHECK(g.neighbourInBounds(Vec2i{0, 0}, Dir::South));

    // Izgara disindaki hucre icin de false.
    CHECK_FALSE(g.neighbourInBounds(Vec2i{2, 2}, Dir::East));
    CHECK_FALSE(g.neighbourInBounds(Vec2i{2, 2}, Dir::South));
}

TEST_CASE("connectivityMask sadece yol olan yonleri isaretler") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Road);
    g.set(Vec2i{1, 2}, CellType::Road);   // West
    g.set(Vec2i{2, 1}, CellType::Road);   // North

    const std::uint8_t mask = g.connectivityMask(Vec2i{2, 2});
    CHECK((mask & bit(Dir::West)) != 0);
    CHECK((mask & bit(Dir::North)) != 0);
    CHECK((mask & bit(Dir::East)) == 0);
    CHECK((mask & bit(Dir::South)) == 0);
}

// ---------------------------------------------------------------------------
// Baglantilik: ulasilamayan hedef / baglantisiz yol agi tespiti
// ---------------------------------------------------------------------------

TEST_CASE("Bos ag bagli sayilir") {
    const Grid g{GridSize{5, 5}};
    CHECK(g.isConnected());
    CHECK_EQ(g.reachableFrom(Vec2i{0, 0}), 0u);
}

TEST_CASE("Tek duz yol baglidir") {
    const Grid g = yatayYol(5, 3, 1);
    CHECK(g.isConnected());
    CHECK_EQ(g.totalOccupied(), 5u);
    CHECK_EQ(g.reachableFrom(Vec2i{0, 1}), 5u);
}

TEST_CASE("Iki ayri yol parasi BAGLI DEGILDIR") {
    Grid g{GridSize{6, 3}};
    for (int x = 0; x < 2; ++x) {  // sol: 0,1
        g.set(Vec2i{x, 1}, CellType::Road);
    }
    for (int x = 4; x < 6; ++x) {  // sag: 4,5
        g.set(Vec2i{x, 1}, CellType::Road);
    }

    CHECK_FALSE(g.isConnected());
    CHECK_EQ(g.totalOccupied(), 4u);
    CHECK_EQ(g.reachableFrom(Vec2i{0, 1}), 2u);
    CHECK_EQ(g.reachableFrom(Vec2i{4, 1}), 2u);
}

TEST_CASE("Kopuk kavsak yol agini baglantisiz yapar") {
    Grid g{GridSize{7, 5}};
    // Merkezde kavsak, kollara yol
    g.set(Vec2i{3, 2}, CellType::Junction);
    g.set(Vec2i{2, 2}, CellType::Road);
    g.set(Vec2i{4, 2}, CellType::Road);
    g.set(Vec2i{3, 1}, CellType::Road);
    g.set(Vec2i{3, 3}, CellType::Road);
    CHECK(g.isConnected());

    // Uzak bir yol parcasi ekle -> artik bagli degil
    g.set(Vec2i{0, 4}, CellType::Road);
    g.set(Vec2i{1, 4}, CellType::Road);
    CHECK_FALSE(g.isConnected());
}

TEST_CASE("firstOccupied deterministiktir") {
    Grid g{GridSize{5, 5}};
    CHECK(eq(g.firstOccupied(), (Vec2i{0, 0})));

    g.set(Vec2i{3, 1}, CellType::Road);
    CHECK(eq(g.firstOccupied(), (Vec2i{3, 1})));

    g.set(Vec2i{1, 1}, CellType::Road);
    CHECK(eq(g.firstOccupied(), (Vec2i{1, 1})));  // satir ici ilk dolu
}

TEST_CASE("reachableFrom bos hucrede 0 doner") {
    const Grid g{GridSize{5, 5}};
    CHECK_EQ(g.reachableFrom(Vec2i{2, 2}), 0u);
}

// ---------------------------------------------------------------------------
// Kavsak gecerliligi
// ---------------------------------------------------------------------------

TEST_CASE("Dort yollu kavsak gecerlidir") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Junction);
    for (const Dir d : tsc::kAllDirs) {
        g.set(Vec2i{2, 2} + tsc::dirOffset(d), CellType::Road);
    }
    CHECK(g.isValidJunction(Vec2i{2, 2}));
    CHECK_EQ(g.junctionArms(Vec2i{2, 2}), 0x0Fu);
}

TEST_CASE("T kavsak gecerlidir - uc kol") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Junction);
    g.set(Vec2i{2, 2} + tsc::dirOffset(Dir::North), CellType::Road);
    g.set(Vec2i{2, 2} + tsc::dirOffset(Dir::East), CellType::Road);
    g.set(Vec2i{2, 2} + tsc::dirOffset(Dir::West), CellType::Road);

    CHECK(g.isValidJunction(Vec2i{2, 2}));
    const std::uint8_t arms = g.junctionArms(Vec2i{2, 2});
    CHECK((arms & bit(Dir::North)) != 0);
    CHECK((arms & bit(Dir::South)) == 0);
}

TEST_CASE("Kolsuz kavsak gecersizdir") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Junction);
    CHECK_FALSE(g.isValidJunction(Vec2i{2, 2}));
}

TEST_CASE("Tek kollu cikmaz kavsak gecersizdir") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Junction);
    g.set(Vec2i{2, 1}, CellType::Road);
    CHECK_FALSE(g.isValidJunction(Vec2i{2, 2}));
}

TEST_CASE("Kose donusu gecerlidir - tek capraz komsu gecersiz degildir") {
    // Yalnizca W ve N kollari + kose caprazi (NW). Bu bir kose donusudur,
    // T kavsak gibi gecerli bir kavsaktir.
    Grid g{GridSize{7, 7}};
    g.set(Vec2i{3, 3}, CellType::Junction);
    g.set(Vec2i{2, 3}, CellType::Road);  // West kolu
    g.set(Vec2i{3, 2}, CellType::Road);  // North kolu
    g.set(Vec2i{2, 2}, CellType::Road);  // NW caprazi
    CHECK(g.isValidJunction(Vec2i{3, 3}));
}

TEST_CASE("Elmas tasiyan dort yollu kavsak gecersizdir") {
    // Dort kolun tamami + iki capraz komsu -> kavsak tasiyor.
    Grid g{GridSize{7, 7}};
    g.set(Vec2i{3, 3}, CellType::Junction);
    for (const Dir d : tsc::kAllDirs) {
        g.set(Vec2i{3, 3} + tsc::dirOffset(d), CellType::Road);
    }
    g.set(Vec2i{2, 2}, CellType::Road);  // NW
    g.set(Vec2i{4, 4}, CellType::Road);  // SE
    CHECK_FALSE(g.isValidJunction(Vec2i{3, 3}));
}

TEST_CASE("Dort yollu kavsak caprazsiz gecerlidir") {
    Grid g{GridSize{7, 7}};
    g.set(Vec2i{3, 3}, CellType::Junction);
    for (const Dir d : tsc::kAllDirs) {
        g.set(Vec2i{3, 3} + tsc::dirOffset(d), CellType::Road);
    }
    CHECK(g.isValidJunction(Vec2i{3, 3}));
}

TEST_CASE("Yol parcasi kavsak sayilmaz") {
    Grid g{GridSize{5, 5}};
    g.set(Vec2i{2, 2}, CellType::Road);
    g.set(Vec2i{1, 2}, CellType::Road);
    CHECK_FALSE(g.isValidJunction(Vec2i{2, 2}));
}

TEST_CASE("Kavsak Izgara disinda gecersiz") {
    const Grid g{GridSize{5, 5}};
    CHECK_FALSE(g.isValidJunction(Vec2i{9, 9}));
    CHECK_FALSE(g.isValidJunction(Vec2i{-1, 0}));
}

TEST_CASE("Izgara kenarindaki kavsak - kollari eksik olabilir ama gecerlidir") {
    Grid g{GridSize{4, 4}};
    g.set(Vec2i{0, 0}, CellType::Junction);
    g.set(Vec2i{1, 0}, CellType::Road);  // East
    g.set(Vec2i{0, 1}, CellType::Road);  // South
    // North ve West komsulari izgara disinda
    CHECK(g.isValidJunction(Vec2i{0, 0}));
    CHECK_EQ(g.junctionArms(Vec2i{0, 0}), bit(Dir::East) | bit(Dir::South));
}

TEST_CASE("toString") {
    CHECK(std::string{tsc::toString(CellType::Empty)} == "empty");
    CHECK(std::string{tsc::toString(CellType::Road)} == "road");
    CHECK(std::string{tsc::toString(CellType::Junction)} == "junction");
    CHECK(std::string{tsc::toString(Dir::North)} == "North");
}