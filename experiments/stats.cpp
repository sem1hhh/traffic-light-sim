#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace tsc {
namespace {

// NOT: Normal kuantili (eski kZ95 = 1.9599) BILINCLIKLE KALDIRILDI.
// Guven araliklari artik Student t ile hesapliyor; iki farkli kritik deger
// kullanmak tutarsizlikti (BULGU 3). Geri getirmeyin.

// --- Tam sayisal olmayan beta fonksiyonu (Numerical Recipes, betacf/betai) ---

double logGamma(double x) {
    // Lanczos yaklasimi
    static const double c[9] = {
        0.99999999999980993, 676.5203681218851,   -1259.1392167224028,
        771.32342877765313,  -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};
    if (x < 0.5) {
        return std::log(3.14159265358979323846 / std::sin(3.14159265358979323846 * x)) - logGamma(1.0 - x);
    }
    x -= 1.0;
    double a = c[0];
    const double t = x + 7.5;
    for (int i = 1; i < 9; ++i) {
        a += c[i] / (x + static_cast<double>(i));
    }
    return 0.5 * std::log(2.0 * 3.14159265358979323846) + (x + 0.5) * std::log(t) - t + std::log(a);
}

/// Beta devam kesri (Continued fraction).
double betaContinuedFraction(double a, double b, double x) {
    constexpr int kMaxIter = 200;
    constexpr double kEps = 3.0e-14;
    constexpr double kFpMin = 1.0e-300;

    double qab = a + b;
    double qap = a + 1.0;
    double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::abs(d) < kFpMin) {
        d = kFpMin;
    }
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m <= kMaxIter; ++m) {
        const double m2 = 2.0 * static_cast<double>(m);
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::abs(d) < kFpMin) { d = kFpMin; }
        c = 1.0 + aa / c;
        if (std::abs(c) < kFpMin) { c = kFpMin; }
        d = 1.0 / d;
        h *= d * c;

        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::abs(d) < kFpMin) { d = kFpMin; }
        c = 1.0 + aa / c;
        if (std::abs(c) < kFpMin) { c = kFpMin; }
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < kEps) {
            break;
        }
    }
    return h;
}

/// Duzeltilmis tam sayisal olmayan beta fonksiyonu I_x(a,b).
double regularizedIncompleteBeta(double a, double b, double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    const double front = std::exp(logGamma(a + b) - logGamma(a) - logGamma(b)
                                  + a * std::log(x) + b * std::log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * betaContinuedFraction(a, b, x) / a;
    }
    return 1.0 - std::exp(logGamma(a + b) - logGamma(a) - logGamma(b)
                          + b * std::log(1.0 - x) + a * std::log(x))
                 * betaContinuedFraction(b, a, 1.0 - x) / b;
}

}  // namespace

double studentTCdf(double t, std::size_t df) {
    if (df == 0) {
        return 0.5;
    }
    const double x = df / (df + t * t);
    const double ib = regularizedIncompleteBeta(0.5 * static_cast<double>(df), 0.5, x);
    return (t > 0.0) ? (1.0 - 0.5 * ib) : (0.5 * ib);
}

double studentTQuantile(double p, std::size_t df) {
    if (df == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // Asimetrik (0,1) araligi [0,1]'e sigdir; uc degerlerde bisection
    // sinirsiz genislemesin diye.
    const double q = std::min(std::max(p, 1.0e-12), 1.0 - 1.0e-12);
    // cdf tek ve artan; klasik bisection. t > 0 bolgesinde ariyoruz.
    double lo = 0.0;
    double hi = 1.0;
    while (studentTCdf(hi, df) < q && hi < 1.0e12) {
        hi *= 2.0;
    }
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (studentTCdf(mid, df) < q) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

double tCritical95(std::size_t df) {
    // %95 iki kuyruklu: P(T <= t) = 0.975
    return studentTQuantile(0.975, df);
}

ConfidenceInterval describe(const std::vector<double>& x) {
    ConfidenceInterval r;
    r.n = x.size();
    if (x.empty()) {
        return r;
    }
    double sum = 0.0;
    for (const double v : x) {
        sum += v;
    }
    r.mean = sum / static_cast<double>(x.size());

    if (x.size() > 1) {
        double ss = 0.0;
        for (const double v : x) {
            const double d = v - r.mean;
            ss += d * d;
        }
        r.sd = std::sqrt(ss / static_cast<double>(x.size() - 1));
    }
    // Guven araligi da STUDENT t ile: normal kuantili (kZ95) n kucukken
    // araligi daraltir. (BULGU 3 ile ayni duzeltme.)
    if (x.size() > 1) {
        const double tCrit = tCritical95(x.size() - 1);
        const double yarim = tCrit * r.sd / std::sqrt(static_cast<double>(x.size()));
        r.low = r.mean - yarim;
        r.high = r.mean + yarim;
    } else {
        // Tek gozlem: varyans tahmini yok, aralik TANIMSIZ.
        r.low = std::numeric_limits<double>::quiet_NaN();
        r.high = std::numeric_limits<double>::quiet_NaN();
    }
    return r;
}

PairedTResult pairedTTest(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("pairedTTest: eslestirilmis ornekler esit uzunlukta olmali");
    }
    if (a.size() < 2) {
        throw std::invalid_argument("pairedTTest: en az 2 eslesmis gozlem gerekir");
    }

    PairedTResult r;
    r.n = a.size();
    r.df = a.size() - 1;

    std::vector<double> diff(a.size());
    double sumA = 0.0;
    double sumB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff[i] = a[i] - b[i];
        sumA += a[i];
        sumB += b[i];
    }
    r.meanA = sumA / static_cast<double>(a.size());
    r.meanB = sumB / static_cast<double>(a.size());

    double sumDiff = 0.0;
    for (const double d : diff) {
        sumDiff += d;
    }
    r.meanDiff = sumDiff / static_cast<double>(a.size());

    double ss = 0.0;
    for (const double d : diff) {
        const double dev = d - r.meanDiff;
        ss += dev * dev;
    }
    r.sdDiff = std::sqrt(ss / static_cast<double>(a.size() - 1));
    r.stdError = r.sdDiff / std::sqrt(static_cast<double>(a.size()));

    // Guven araligi kritik degeri: STUDENT t, normal kuantili DEGIL.
    //
    // Eski kurgu p-degerini t-dagilimiyla, araligi normal kuantili
    // (kZ95 = 1.9599) ile hesapliyordu. Bu tutarsizlikti: n=5'te dogru
    // kritik deger 2.776'dir. Doğru aralik eski araligin 1.416 KATIDIR,
    // yani eski aralik dogrusundan %29 dar basilirdi — dogrusu %41 daha
    // genistir. (BULGU 3)
    const double tCrit = tCritical95(r.df);

    if (r.stdError > 1e-15) {
        r.tStat = r.meanDiff / r.stdError;
        r.pTwoSided = 2.0 * (1.0 - studentTCdf(std::abs(r.tStat), r.df));
        r.pTwoSided = std::min(1.0, std::max(0.0, r.pTwoSided));
        r.ciLow = r.meanDiff - tCrit * r.stdError;
        r.ciHigh = r.meanDiff + tCrit * r.stdError;
    } else {
        // Tum farklar birebir ayni: varyans TAHMINI YOK.
        //
        // Eski kurgu `tStat = 1.0e6` yazip "cok anlamlı" basiyordu. Bu
        // sihirli sayi, sayisal bir artefakti da anlamli gosterebilirdi.
        // Artik istatistik TANIMDIR: t, p ve aralik NaN'dir. Raporlayici
        // `degenerate` bayragini gostermek zorundadir; "fark yok" DIYEMEZ,
        // cunku "fark yok" ile "olcum yapilamadi" ayni sey degildir.
        r.degenerate = true;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        r.tStat = nan;
        r.pTwoSided = nan;
        r.ciLow = nan;
        r.ciHigh = nan;
    }
    return r;
}

std::string formatP(double p) {
    std::ostringstream os;
    if (std::isnan(p)) {
        // Tanimsiz: 0.000 ya da "cok kucuk" olarak GORUNMEMELI.
        os << "n/a";
    } else if (p < 0.0005) {
        os << "<0.001";
    } else {
        os << std::fixed << std::setprecision(3) << p;
    }
    return os.str();
}

}  // namespace tsc