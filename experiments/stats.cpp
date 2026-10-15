#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace tsc {
namespace {

constexpr double kZ95 = 1.959963984540054;   // iki kuyruklu %95 kritik deger

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
    r.low = r.mean - kZ95 * r.sd / std::sqrt(static_cast<double>(x.size()));
    r.high = r.mean + kZ95 * r.sd / std::sqrt(static_cast<double>(x.size()));
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

    if (r.stdError > 1e-15) {
        r.tStat = r.meanDiff / r.stdError;
    } else {
        // Tum farklar ayni: varyans yok. Ortak fark sifirsa anlamli degil,
        // degilse istatistiksel olarak belirgin (pratikte p -> 0).
        r.tStat = (std::abs(r.meanDiff) > 1e-12) ? 1.0e6 : 0.0;
    }

    r.pTwoSided = 2.0 * (1.0 - studentTCdf(std::abs(r.tStat), r.df));
    r.pTwoSided = std::min(1.0, std::max(0.0, r.pTwoSided));

    r.ciLow = r.meanDiff - kZ95 * r.stdError;
    r.ciHigh = r.meanDiff + kZ95 * r.stdError;
    return r;
}

std::string formatP(double p) {
    std::ostringstream os;
    if (p < 0.0005) {
        os << "<0.001";
    } else {
        os << std::fixed << std::setprecision(3) << p;
    }
    return os.str();
}

}  // namespace tsc