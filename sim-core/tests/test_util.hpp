#pragma once

// doctest ile tip guvenli karsilastirma yardimcilari.
//
// Neden gerekiyor:
// doctest, CHECK_EQ(a, b) ve CHECK(a == b) ifadelerini AYRIŞTIRIP degerleri
// metne cevirmeye calisir. Bizim tipimiz (enum class, Vec2i) icin ostream
// << tanimi SFINAE tespitinde yakalanmiyor ve derleme basarisiz oluyor.
//
// Cozum: karsilastirmayi once saf bool'a indirgemek. CHECK(someBool)
// ayristirma yapmaz, dolayisiyla tipi hicbir zaman string'e cevirmeye
// calismaz. Testler `CHECK(eq(a, b))` seklinde yazilir.
//
// Not: operator<< eklemek COZUM DEGIL; denendi, SFINAE tespiti yine
// basarisiz oldu. (2026-10-14)

#include <doctest/doctest.h>

#include <cmath>

/// Mutlak toleransla karsilastirma. Kayan nokta testlerinde `CHECK_EQ`
/// kullanilamaz (tam esitlik beklenmez); bu yardimci "ne kadar yaklasmali"
/// sorusunu sorar ve BASARISIZ olursa farki yazar.
#define CHECK_NEAR_T(a, b, tol) \
    CHECK_MESSAGE(std::abs((a) - (b)) <= (tol),                    \
                  #a " != " #b " fark=" << std::abs((a) - (b))     \
                                        << " tolerans=" << (tol))

/// Bagil hata ile karsilastirma (oranlarda mutlak tolerans yaniltir).
#define CHECK_REL_T(a, b, relTol) CHECK_NEAR_T((a), (b), std::abs(b) * (relTol))

/// Esitlik. Iki degeri karsilastirip bool doner.
template <typename T>
constexpr bool eq(const T& a, const T& b) noexcept {
    return a == b;
}

/// Farklilik.
template <typename T>
constexpr bool ne(const T& a, const T& b) noexcept {
    return !(a == b);
}