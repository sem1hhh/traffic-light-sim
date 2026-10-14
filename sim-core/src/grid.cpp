#include "tsc/grid.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <stdexcept>

namespace tsc {
namespace {

constexpr std::uint8_t dirBit(Dir d) noexcept { return static_cast<std::uint8_t>(1u << static_cast<int>(d)); }

/// Bit maskesindeki acik kollarin sayisi (C++20 std::popcount yok, C++17'de elle).
constexpr int popcount4(std::uint8_t mask) noexcept {
    return ((mask >> 0) & 1u) + ((mask >> 1) & 1u) + ((mask >> 2) & 1u) + ((mask >> 3) & 1u);
}

}  // namespace

const char* toString(CellType t) noexcept {
    switch (t) {
        case CellType::Empty:    return "empty";
        case CellType::Road:     return "road";
        case CellType::Junction: return "junction";
    }
    return "?";
}

const char* toString(Dir d) noexcept {
    switch (d) {
        case Dir::North: return "North";
        case Dir::East:  return "East";
        case Dir::South: return "South";
        case Dir::West:  return "West";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Yon yardimcilari
// ---------------------------------------------------------------------------

static_assert(dirOffset(Dir::North) == Vec2i(0, -1));
static_assert(dirOffset(Dir::East) == Vec2i(1, 0));
static_assert(dirOffset(Dir::South) == Vec2i(0, 1));
static_assert(dirOffset(Dir::West) == Vec2i(-1, 0));

static_assert(opposite(Dir::North) == Dir::South);
static_assert(opposite(Dir::East) == Dir::West);
static_assert(opposite(Dir::South) == Dir::North);
static_assert(opposite(Dir::West) == Dir::East);

static_assert(turnRight(Dir::North) == Dir::East);
static_assert(turnRight(Dir::East) == Dir::South);
static_assert(turnRight(Dir::South) == Dir::West);
static_assert(turnRight(Dir::West) == Dir::North);

static_assert(turnLeft(Dir::North) == Dir::West);
static_assert(turnLeft(Dir::East) == Dir::North);

static_assert(opposite(opposite(Dir::North)) == Dir::North);
static_assert(turnRight(turnRight(Dir::East)) == Dir::West);
// 3 kez 90° = 270°, kimlik DEGIL. Donusumun kimligi 4 cevrimdir.
static_assert(turnRight(turnRight(turnRight(turnRight(Dir::West)))) == Dir::West);
static_assert(turnLeft(turnLeft(turnLeft(turnLeft(Dir::West)))) == Dir::West);
static_assert(turnLeft(turnLeft(turnLeft(Dir::West))) == turnRight(Dir::West));

static_assert(isStraight(Dir::North, Dir::South));
static_assert(isStraight(Dir::East, Dir::West));
static_assert(!isStraight(Dir::North, Dir::East));

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

Grid::Grid(GridSize size) : size_(size) {
    if (!size_.valid()) {
        throw std::invalid_argument("Grid: size must be positive");
    }
    cells_.assign(size_.cellCount(), CellType::Empty);
}

CellType Grid::at(const Vec2i& c) const noexcept {
    if (!inBounds(c)) {
        return CellType::Empty;
    }
    return cells_[indexOf(c)];
}

void Grid::set(const Vec2i& c, CellType t) {
    if (!inBounds(c)) {
        throw std::out_of_range("Grid::set: cell out of bounds");
    }
    cells_[indexOf(c)] = t;
}

CellType Grid::neighbour(const Vec2i& c, Dir d) const noexcept { return at(c + dirOffset(d)); }

bool Grid::neighbourInBounds(const Vec2i& c, Dir d) const noexcept {
    return inBounds(c + dirOffset(d));
}

bool Grid::isRoadOrJunction(const Vec2i& c) const noexcept {
    const CellType t = at(c);
    return t == CellType::Road || t == CellType::Junction;
}

std::uint8_t Grid::connectivityMask(const Vec2i& c) const noexcept {
    std::uint8_t mask = 0;
    for (const Dir d : kAllDirs) {
        if (isRoadOrJunction(c + dirOffset(d))) {
            mask |= dirBit(d);
        }
    }
    return mask;
}

std::uint8_t Grid::junctionArms(const Vec2i& c) const noexcept { return connectivityMask(c); }

std::size_t Grid::count(CellType t) const noexcept {
    return static_cast<std::size_t>(std::count(cells_.begin(), cells_.end(), t));
}

Vec2i Grid::firstOccupied() const noexcept {
    for (int y = 0; y < size_.height; ++y) {
        for (int x = 0; x < size_.width; ++x) {
            if (isRoadOrJunction(Vec2i{x, y})) {
                return Vec2i{x, y};
            }
        }
    }
    return Vec2i{0, 0};
}

std::size_t Grid::reachableFrom(const Vec2i& start) const noexcept {
    if (!inBounds(start) || !isRoadOrJunction(start)) {
        return 0;
    }

    std::vector<bool> gorulen(cells_.size(), false);
    std::queue<Vec2i> kuyruk;

    gorulen[indexOf(start)] = true;
    kuyruk.push(start);
    std::size_t sayac = 0;

    while (!kuyruk.empty()) {
        const Vec2i c = kuyruk.front();
        kuyruk.pop();
        ++sayac;

        for (const Dir d : kAllDirs) {
            const Vec2i n = c + dirOffset(d);
            if (!inBounds(n) || !isRoadOrJunction(n)) {
                continue;
            }
            const std::size_t ni = indexOf(n);
            if (!gorulen[ni]) {
                gorulen[ni] = true;
                kuyruk.push(n);
            }
        }
    }
    return sayac;
}

bool Grid::isConnected() const noexcept {
    if (empty()) {
        // Bos ag "bagli" sayilir; baska kontrol (en az bir yol) ayrica yapilir.
        return true;
    }
    return reachableFrom(firstOccupied()) == totalOccupied();
}

bool Grid::isValidJunction(const Vec2i& c) const noexcept {
    if (at(c) != CellType::Junction) {
        return false;
    }

    const std::uint8_t mask = junctionArms(c);
    const int kollar = popcount4(mask);

    // 0 kol: kolsuz. 1 kol: cikmaz. Ikisi de trafik acisindan anlamsiz.
    if (kollar < 2) {
        return false;
    }

    // Dort yollu kavsakta capraz (elmas) yol varsa gecersiz: kavsak tasiyor.
    // NOT: Tek bir capraz komsu gecersiz degildir - o durum sadece bir kose
    // donusudur ve T kavsak gibi gecerlidir.
    if (kollar == 4) {
        const bool caprazSolUst = isRoadOrJunction(c + dirOffset(Dir::North) + dirOffset(Dir::West));
        const bool caprazSagAlt = isRoadOrJunction(c + dirOffset(Dir::South) + dirOffset(Dir::East));
        const bool caprazSagUst = isRoadOrJunction(c + dirOffset(Dir::North) + dirOffset(Dir::East));
        const bool caprazSolAlt = isRoadOrJunction(c + dirOffset(Dir::South) + dirOffset(Dir::West));
        if ((caprazSolUst && caprazSagAlt) || (caprazSagUst && caprazSolAlt)) {
            return false;
        }
    }

    return true;
}

}  // namespace tsc