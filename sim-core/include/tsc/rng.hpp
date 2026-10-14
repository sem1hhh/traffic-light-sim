#pragma once

#include <cstdint>

namespace tsc {

// ---------------------------------------------------------------------------
// Deterministik sözde rastgele sayi ureteci (PCG32).
//
// Neden <random> degil :
//   std::mt19937 / distribution'larin implementasyonu standarda gore degisebilir.
//   Boylece "ayni seed -> ayni sonuc" garantisi  kaybolur. PCG32 ise
//   algoritmasi sabit, cikti dizisi her platformda birebir ayni.
// ---------------------------------------------------------------------------

/// 64 -> 64 bit karma (splitmix64). Tohum turetme ve akis ayriminda kullanilir.
constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

class Rng {
public:
    /// PCG'nin varsayilan akis sabiti.
    static constexpr std::uint64_t kDefaultStream = 0xDA3E39CB94B95BDBULL;

    /// Ayni (seed, stream) cifti HER ZAMAN ayni diziyi uretir.
    explicit Rng(std::uint64_t seed = 0, std::uint64_t stream = kDefaultStream) noexcept
        : state_(0), inc_((stream << 1u) | 1u), seedOf_(seed), streamOf_(stream) {
        nextU32();
        state_ += seed;
        nextU32();
    }

    Rng(const Rng&) = default;
    Rng& operator=(const Rng&) = default;

    // --- Cikt ---------------------------------------------------------------

    std::uint32_t nextU32() noexcept {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    /// [0, bound) arasi deger. Yanliliksiz (unbiased) icin eleme yontemi kullanir.
    /// bound == 0 durumunda 0 doner.
    std::uint32_t below(std::uint32_t bound) noexcept {
        if (bound == 0) {
            return 0;
        }
        // 2^32 % bound -> bu degerden kucuk sonuclar reddedilir.
        const std::uint32_t threshold = (~bound + 1u) % bound;
        for (;;) {
            const std::uint32_t r = nextU32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    /// [lo, hi] kapali aralik. lo > hi ise lo doner.
    int range(int lo, int hi) noexcept {
        if (hi <= lo) {
            return lo;
        }
        const std::uint32_t span = static_cast<std::uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(below(span));
    }

    /// [0, 1) arasi cift.
    double unit() noexcept {
        return static_cast<double>(nextU32()) * 2.3283064365386963e-10;  // 1 / 2^32
    }

    /// [min, max) arasi cift.
    double rangeD(double min, double max) noexcept { return min + unit() * (max - min); }

    /// p olasilikla true.
    bool chance(double p) noexcept { return unit() < p; }

    // --- Akis ayrimi --------------------------------------------------------

    /// Bu uretecden deterministik olarak turetilmis bagli bir akis.
    /// Ayri bir Rng nesnesi dondurur; bu nesne uzerinde cagri yapmaz.
    /// Alt sistemler (arac uretimi, rota secimi, ...) birbirinden bagimsiz akis kullanir.
    Rng split(std::uint64_t index) const noexcept {
        return Rng{mix64(seedOf_ ^ mix64(index)), streamOf_};
    }

    std::uint64_t seed() const noexcept { return seedOf_; }
    std::uint64_t stream() const noexcept { return streamOf_; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 1;
    std::uint64_t seedOf_ = 0;
    std::uint64_t streamOf_ = 0;
};

}  // namespace tsc