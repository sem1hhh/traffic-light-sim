#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Istatistiksel araclar
//
// Rapor: "Kontrol yontemleri arasindaki fark, eslestirilmis (paired) karsilastirma
// yaklasimiyla (orn. eslestirilmis t-testi) degerlendirilecektir."
//
// ESLESTIRILMIS tasarim neden: ayni harita, ayni tohum, ayni talep profili
// her iki kontrolcu icin de kullanilir. Yani gozlemler bagimsiz degildir;
// bagimsiz t-testi kullanmak yanlis olur. Farklarin (difference) ortalamasi
// uzerinden hesaplanir.
//
// t dagiliminin iki kuyruklu olasilik fonksiyonu, tam sayisal olmayan beta
// (incomplete beta) fonksiyonu uzerinden hesaplanir. Harici kutuphane
// kullanilmaz; boylece deney araclari derlenmesi kolay ve tekrarlanabilir
// kalir.
// ---------------------------------------------------------------------------

struct PairedTResult {
    std::size_t n = 0;
    double meanA = 0.0;
    double meanB = 0.0;
    double meanDiff = 0.0;     ///< A - B (ortalama fark)
    double sdDiff = 0.0;       ///< farklarin standart sapmasi
    double stdError = 0.0;
    double tStat = 0.0;
    std::size_t df = 0;        ///< serbestlik derecesi = n - 1
    double pTwoSided = 1.0;    ///< iki kuyruklu p degeri
    double ciLow = 0.0;        ///< ortalama farkin %95 guven arligi
    double ciHigh = 0.0;

    /// Ortalama farkin anlamli olup olmadigi (p < 0.05).
    bool significant() const noexcept { return pTwoSided < 0.05; }
};

/// Eslestirilmis t-testi. a[i] ve b[i] ayni deney kosuluna ait olmalidir.
PairedTResult pairedTTest(const std::vector<double>& a, const std::vector<double>& b);

/// Tek ornekli ortalama guven arligi (yardimci).
struct ConfidenceInterval {
    double low = 0.0;
    double high = 0.0;
    double mean = 0.0;
    double sd = 0.0;
    std::size_t n = 0;
};

/// Ortalama ve standart sapma.
ConfidenceInterval describe(const std::vector<double>& x);

/// Student t dagiliminin kumulatif fonksiyonu (P(T <= t)).
double studentTCdf(double t, std::size_t df);

/// p degerini 3 ondaliga bicimlendirir (tabloya yazmak icin).
std::string formatP(double p);

}  // namespace tsc