#pragma once

#include <cstdint>

namespace tsc {

// ---------------------------------------------------------------------------
// Guclu tip'li (strongly-typed) indeksler.
//
// Neden gerekli: simülasyon state'i ID-indexed tutulur.
// `VehicleId` ile `JunctionId` birbirine karistirilamaz; derleyici hata verir.
// Ayrica state kopyalanabilir olur -> snapshot / geri sarma ucuz olur.
// ---------------------------------------------------------------------------

template <typename Tag>
struct Id {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::uint32_t value = kInvalid;

    constexpr bool valid() const noexcept { return value != kInvalid; }
    constexpr explicit operator bool() const noexcept { return valid(); }

    constexpr bool operator==(const Id& other) const noexcept { return value == other.value; }
    constexpr bool operator!=(const Id& other) const noexcept { return value != other.value; }
    constexpr bool operator<(const Id& other) const noexcept { return value < other.value; }

    static constexpr Id invalid() noexcept { return Id{kInvalid}; }
    static constexpr Id from(std::uint32_t v) noexcept { return Id{v}; }
};

struct VehicleTag;
struct JunctionTag;
struct LaneTag;
struct ConnectionTag;
struct EdgeTag;

using VehicleId = Id<VehicleTag>;
using JunctionId = Id<JunctionTag>;
using LaneId = Id<LaneTag>;
using ConnectionId = Id<ConnectionTag>;
using EdgeId = Id<EdgeTag>;

// ---------------------------------------------------------------------------
// Izgara geometrisi
// ---------------------------------------------------------------------------

/// Tam sayi tabanli 2B nokta. Floating-point birikiminden kaçinmak icin
/// simülasyon geometrisi bu tipi kullanir.
struct Vec2i {
    int x = 0;
    int y = 0;

    constexpr Vec2i() = default;
    constexpr Vec2i(int x_, int y_) : x(x_), y(y_) {}

    constexpr Vec2i operator+(const Vec2i& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(const Vec2i& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr bool operator==(const Vec2i& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2i& o) const noexcept { return !(*this == o); }
};

/// Izgaranin kenar uzunlugu (hucre sayisi).
struct GridSize {
    int width = 0;
    int height = 0;

    constexpr GridSize() = default;
    constexpr GridSize(int w, int h) : width(w), height(h) {}

    constexpr bool contains(const Vec2i& c) const noexcept {
        return c.x >= 0 && c.y >= 0 && c.x < width && c.y < height;
    }
    constexpr std::uint32_t cellCount() const noexcept {
        return static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height);
    }
    constexpr bool valid() const noexcept { return width > 0 && height > 0; }
};

// ---------------------------------------------------------------------------
// Zaman
// ---------------------------------------------------------------------------

/// Sabit zaman adimi (1 adim = 1 saniye).
constexpr double kStepSeconds = 1.0;

/// Toplam simülasyon süresi üst sınırı. Asilirsa koşu "tamamlanamadi" isaretlenir.
constexpr double kMaxSimSeconds = 3600.0;

}  // namespace tsc