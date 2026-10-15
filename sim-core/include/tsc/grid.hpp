#pragma once

#include "tsc/types.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Yonler. Saat yonunde siralanir; bu siralama Q-learning durum kodlamasinda
// ve faz planlarinda da kullanilacagindan degistirilmemelidir.
// ---------------------------------------------------------------------------
enum class Dir : std::uint8_t { North = 0, East = 1, South = 2, West = 3 };

constexpr std::size_t kDirCount = 4;

/// Yonun izgara koordinatindaki ofseti.
constexpr Vec2i dirOffset(Dir d) noexcept {
    switch (d) {
        case Dir::North: return {0, -1};
        case Dir::East:  return {1, 0};
        case Dir::South: return {0, 1};
        case Dir::West:  return {-1, 0};
    }
    return {0, 0};
}

/// Karsi yon. Duz hat geceislerinde (N-S, D-B) kullanilir.
constexpr Dir opposite(Dir d) noexcept {
    switch (d) {
        case Dir::North: return Dir::South;
        case Dir::East:  return Dir::West;
        case Dir::South: return Dir::North;
        case Dir::West:  return Dir::East;
    }
    return Dir::North;
}

/// Yonu 90° saga cevirir. Donus yonu (sag/sol) ve yaya gecidi hesaplarinda kullanilir.
constexpr Dir turnRight(Dir d) noexcept {
    switch (d) {
        case Dir::North: return Dir::East;
        case Dir::East:  return Dir::South;
        case Dir::South: return Dir::West;
        case Dir::West:  return Dir::North;
    }
    return Dir::North;
}

/// Yonu 90° sola cevirir.
constexpr Dir turnLeft(Dir d) noexcept {
    return turnRight(turnRight(turnRight(d)));
}

/// Duz gecis mi? (N-S veya D-B). Faz planlari buna gore kurulur.
constexpr bool isStraight(Dir from, Dir to) noexcept { return opposite(from) == to; }

constexpr std::array<Dir, kDirCount> kAllDirs = {
    Dir::North, Dir::East, Dir::South, Dir::West,
};

/// Yuz yolunu temsil eden hucre tipi.
enum class CellType : std::uint8_t {
    Empty = 0,     ///< yerlestirilmemis
    Road = 1,      ///< duz yol parcasi
    Junction = 2,  ///< kavsak
};

const char* toString(CellType t) noexcept;
const char* toString(Dir d) noexcept;

const char* toString(CellType t) noexcept;
const char* toString(Dir d) noexcept;

// DIKKAT: enum class icin operator<< TASIMAYIN.
// doctest'in "insertion operator var mi" SFINAE tespiti bunu yakalayamiyor ve
// enum karsilastirmalari derlenmiyor. Testlerde test_util.hpp'deki eq()/ne()
// yardimcilarini kullan: CHECK(eq(a, b)). (2026-10-14'te denendi, ise yaramadi.)

// ---------------------------------------------------------------------------
// Izgara. Hucreler kullanicinin editorde yerlestirdigi yol agini tutar.
// Lane/Connection grafigi bundan *turetilir* (network.hpp) - ayri tutulur,
// boylece izgara duzenlemesi ag degistirmeden yapilabilir.
// ---------------------------------------------------------------------------
class Grid {
public:
    Grid() = default;
    explicit Grid(GridSize size);

    GridSize size() const noexcept { return size_; }
    bool valid() const noexcept { return size_.valid(); }

    bool inBounds(const Vec2i& c) const noexcept { return size_.contains(c); }

    CellType at(const Vec2i& c) const noexcept;
    void set(const Vec2i& c, CellType t);

    /// Komsu hucreyi dondurur; izgara disindaysa Empty.
    CellType neighbour(const Vec2i& c, Dir d) const noexcept;

    /// Izgara disina dusmeyen komsu koordinatlar.
    bool neighbourInBounds(const Vec2i& c, Dir d) const noexcept;

    /// Bu hucrede yolun hangi yone baglandigi (komsu yol/kavsak hucreleri).
    /// Donus: hangi yonlara bagli oldugunun bit maskesi (North=1<<0, East=1<<1, ...).
    std::uint8_t connectivityMask(const Vec2i& c) const noexcept;

    /// Cevredeki hucre yol/kavsak mi?
    bool isRoadOrJunction(const Vec2i& c) const noexcept;

    // --- Sayimlar -----------------------------------------------------------
    std::size_t count(CellType t) const noexcept;
    std::size_t totalOccupied() const noexcept { return count(CellType::Road) + count(CellType::Junction); }
    bool empty() const noexcept { return totalOccupied() == 0; }

    /// Ilk dolu hucrenin koordinati. Yoksa {0,0}.
    /// Baglantilik kontrolunu baslatmak icin kullanilir; deterministiktir.
    Vec2i firstOccupied() const noexcept;

    // --- Dogrulama: ulasilamayan hedef / baglantisiz yol agi tespiti ----------

    /// Butun yol/kavsak hucreleri TEK bir bagli kutuphanede mi?
    /// Bu olmadan simülasyon baslatilmaz.
    bool isConnected() const noexcept;

    /// Su anki hucreden ulasilabilir hucre sayisi (yalnizca inceleme icin).
    std::size_t reachableFrom(const Vec2i& start) const noexcept;

    /// Bir kavsagin kollarinin yon maskesi (hangi yonlarda yol var).
    /// T kavsak = 3 koll, dort yollu = 4 koll.
    std::uint8_t junctionArms(const Vec2i& c) const noexcept;

    /// Bu hucre kavsak mi ve kollari gecerli mi?
    /// Gecerli: en az 2 kol. Dort yollu kavsakta capraz (elmas) yol varsa gecersiz.
    /// Gecersiz ornekler: kolsuz hucre, tek kollu cikmaz, elmas tasiyan dort yollu kavsak.
    bool isValidJunction(const Vec2i& c) const noexcept;

    // --- Serilestirme ------------------------------------------------------
    std::vector<CellType>& raw() noexcept { return cells_; }
    const std::vector<CellType>& raw() const noexcept { return cells_; }

private:
    std::size_t indexOf(const Vec2i& c) const noexcept {
        return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(size_.width) +
               static_cast<std::size_t>(c.x);
    }

    GridSize size_{};
    std::vector<CellType> cells_;
};

}  // namespace tsc