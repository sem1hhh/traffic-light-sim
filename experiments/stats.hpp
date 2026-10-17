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

    /// Farklarin VARYANSI YOK (tumu birebir ayni). Bu durumda t, p ve guven
    /// araligi TANIMSIZDIR; `tStat`, `pTwoSided`, `ciLow`, `ciHigh` NaN'dir.
    ///
    /// Neden NaN ve eski gibi 1.0e6 degil: sifir varyans, "cok anlamlı"
    /// degil "istatistiksel iddia kurulamiyor" demektir. Sihirli bir buyuk
    /// sayi, sayisal bir artefakti da anlamli gosterebilirdi.
    bool degenerate = false;

    /// Ortalama farkin anlamli olup olmadigi (p < 0.05).
    ///
    /// Degenerate durumda asla "anlamli" DONMEZ: varyans tahmini olmadan
    /// anlamli iddia kurulamaz. (Tanimsiz ile "fark yok" AYNI SEY DEGILDIR;
    /// raporlayici `degenerate` bayragini ayrica gostermek zorundadir.)
    bool significant() const noexcept { return !degenerate && pTwoSided < 0.05; }
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

/// Student t dagiliminin TERSI: studentTCdf(t, df) = p olan t degeri.
///
/// Guven araligi icin gerekir. p-degeri t-dagilimiyla hesaplanirken araligin
/// normal kuantili (1.9599) ile hesaplanmasi TUTARSIZLIKTI: n=5'te dogru
/// kritik deger 2.776'dir, yani aralik %41 DAR basiliyordu. (denetim.md
/// BULGU 3)
///
/// Bisection ile bulunur: `studentTCdf` zaten mevcut ve dogrulanmis; onun
/// tersini yeniden icat etmek yerine onu kullanmak daha az hata kaynagi.
double studentTQuantile(double p, std::size_t df);

/// Guven araligi kritik degeri: %95 iki kuyruklu -> P(T <= t) = 1 - 0.025.
double tCritical95(std::size_t df);

/// p degerini 3 ondaliga bicimlendirir (tabloya yazmak icin).
std::string formatP(double p);

}  // namespace tsc