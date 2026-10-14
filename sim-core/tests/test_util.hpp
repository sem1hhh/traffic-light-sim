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