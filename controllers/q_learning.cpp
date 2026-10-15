#include "q_learning.hpp"

#include <cmath>
#include <istream>
#include <sstream>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace tsc {
namespace {

/// Kuyruk degerini 0..3 arasina cevirir.
std::size_t binQueue(int q, const std::array<int, 3>& edges) {
    if (q <= edges[0]) return 0;
    if (q <= edges[1]) return 1;
    if (q <= edges[2]) return 2;
    return 3;
}

/// Yesilde gecen sureyi 0..4 arasina cevirir.
std::size_t binTime(double t, const std::array<double, 4>& edges) {
    if (t < edges[0]) return 0;
    if (t < edges[1]) return 1;
    if (t < edges[2]) return 2;
    if (t < edges[3]) return 3;
    return 4;
}

/// Deterministik kesif: ogrenim sirasi disaridan degil, yalnizca durumdan
/// turetilir. Aksi halde ayni tohum ayni kosuyu vermez (K2 ihlali).
///
/// Rastgele sayi kullanmadan once sorulmasi gereken soru: "bu karar ayni
/// girdide ayni mi?" Evet, olmalidir.
double kesifSinyali(std::size_t state) {
    // 64-bit karistirma (splitmix64). Tablonun kova indeksine baglanir.
    std::uint64_t z = static_cast<std::uint64_t>(state) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    // [0, 1) araligina indir.
    return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
}

}  // namespace

void QLearningController::clearTable() {
    tablo.assign(kQStateCount * kQActionCount, 0.0);
    gorulenler_.assign(kQStateCount, false);
    kStateKnown_.assign(kQStateCount, false);
    sayac_.assign(kQStateCount * kQActionCount, 0u);
    deneme_.assign(kQStateCount * kQActionCount, 0u);
    ziyaretli_ = 0;
}

void QLearningController::onReset() {
    // YENI EPIZOT: (s,a) zinciri koparilir, tablo KORUNUR (ogrenme devam eder).
    havePrev_ = false;
}

std::size_t QLearningController::encode(const SignalObservation& obs) const {
    // Kuyruklar: 4 kollarin her biri 4 kovaya -> 4^4
    std::size_t s = 0;
    for (std::size_t d = 0; d < kDirCount; ++d) {
        s = s * 4u + binQueue(obs.queues[d], params_.queueEdges);
    }
    // Faz: 2
    s = s * 2u + (obs.phase == 0 ? 0u : 1u);
    // Yesilde gecen sure: 5 kova
    s = s * 5u + binTime(obs.greenElapsed, params_.timeEdges);
    return s;
}

double QLearningController::q(std::size_t state, Action a) const {
    const std::size_t i = state * kQActionCount + static_cast<std::size_t>(a);
    return i < tablo.size() ? tablo[i] : 0.0;
}

void QLearningController::setQ(std::size_t state, Action a, double v) {
    const std::size_t i = state * kQActionCount + static_cast<std::size_t>(a);
    if (i < tablo.size()) {
        tablo[i] = v;
    }
}

bool QLearningController::isKnown(std::size_t state) const {
    return state < kStateKnown_.size() && kStateKnown_[state];
}

std::size_t QLearningController::knownStates() const {
    std::size_t n = 0;
    for (const bool b : kStateKnown_) {
        if (b) ++n;
    }
    return n;
}

double QLearningController::meanAbsQ() const {
    double t = 0.0;
    for (const double v : tablo) {
        t += std::abs(v);
    }
    return tablo.empty() ? 0.0 : t / static_cast<double>(tablo.size());
}

Action QLearningController::greedyAction(const SignalObservation& obs) const {
    const std::size_t s = encode(obs);
    if (params_.assumeUnknownStay && !isKnown(s)) {
        // Durumu bilmiyoruz: iddiali bir karar vermek yerine tahmin edici
        // davran. Kesif bunu zaten ogrenme firsatina cevirir.
        return Action::Stay;
    }
    return q(s, Action::Switch) > q(s, Action::Stay) ? Action::Switch : Action::Stay;
}

Action QLearningController::epsilonGreedy(std::size_t state) const {
    // --- Kesif: henuz yeterince denenmemis eylem var mi? -------------------
    //
    // Deterministik kalir: hangi eylemin secilecegi durumdan turetilen bir
    // hash ile belirlenir, rastgelelik yoktur. Ayni tohum -> ayni kosu.
    const bool azStay = sayacokul(state, Action::Stay);
    const bool azSwitch = sayacokul(state, Action::Switch);
    if (azStay || azSwitch) {
        ++kesifSayisi_;
        if (azStay && azSwitch) {
            // HATA (testler buldu): burada hash kullanilirsa ayni durum icin
            // HER ZAMAN ayni eylem secilir ve digeri HIC denenmez.
            // Cozum: daha AZ denenmis eylemi sec; esitlik durumunda hash.
            const std::uint32_t ns = deneme_[state * kQActionCount];
            const std::uint32_t nw = deneme_[state * kQActionCount + 1];
            if (ns != nw) {
                return (ns < nw) ? Action::Stay : Action::Switch;
            }
            return (kesifSinyali(state) < 0.5) ? Action::Stay : Action::Switch;
        }
        return azStay ? Action::Stay : Action::Switch;
    }

    if (params_.epsilon > 0.0 && kesifSinyali(state) < params_.epsilon) {
        ++kesifSayisi_;
        return (kesifSinyali(state + 1u) < 0.5) ? Action::Stay : Action::Switch;
    }

    if (params_.assumeUnknownStay && !isKnown(state)) {
        return Action::Stay;
    }
    return q(state, Action::Switch) > q(state, Action::Stay) ? Action::Switch : Action::Stay;
}

bool QLearningController::sayacokul(std::size_t state, Action a) const {
    // DENEME sayaci, degerlendirme sayacindan AYRI tutulur.
    // Kesif "bu eylem kac kez secildi" sorusuna bakar; odulle ogrenme
    // "kac kez degerlendirildi" sorusuna. Tek sayac kullanmak, odul
    // gelmeden bir sonraki karara gecilmesi durumunda kesfi kilitler:
    // deneme sayaci sayacaya bagliydi ve onReward cagrilmadan artmiyordu.
    if (deneme_.size() != kQStateCount * kQActionCount) {
        return true;
    }
    const std::size_t i = state * kQActionCount + static_cast<std::size_t>(a);
    return i >= deneme_.size() || static_cast<int>(deneme_[i]) < params_.minVisitsPerAction;
}

Action QLearningController::decide(const SignalObservation& obs) {
    const std::size_t s = encode(obs);
    if (gorulenler_.size() != kQStateCount) {
        gorulenler_.assign(kQStateCount, false);
    }
    if (!gorulenler_[s]) {
        gorulenler_[s] = true;
        ++ziyaretli_;
    }

    const Action a = epsilonGreedy(s);
    if (deneme_.size() != kQStateCount * kQActionCount) {
        deneme_.assign(kQStateCount * kQActionCount, 0u);
    }
    ++deneme_[s * kQActionCount + static_cast<std::size_t>(a)];
    prevState_ = s;
    prevAction_ = a;
    havePrev_ = true;
    return a;
}

double QLearningController::reward(const SignalObservation& obs) const {
    int bekleyen = obs.totalWaiting();

    if (params_.penalizeRedOnly) {
        // Yalnizca KIRMIZI kollardaki araclar. Faz 0 = {Kuzey, Guney},
        // Faz 1 = {Dogu, Bati}. Dizilim sirasi: [K, D, G, B].
        bekleyen = (obs.phase == 0) ? (obs.queues[0] + obs.queues[2])
                                    : (obs.queues[1] + obs.queues[3]);
    }

    if (!params_.penalizeIdle && bekleyen <= 0) {
        return 0.0;
    }
    return -static_cast<double>(bekleyen);
}

void QLearningController::onReward(const SignalObservation& nextObs, Action executed) {
    if (!havePrev_) {
        return;
    }
    // Ogrenilen eylem, KONTROLCUNUN SECDIGI eylemdir — uygulanan degil.
    // Reddedilmis bir gecis istegi de bir gecistir; (s, Switch) -> s'
    // gecisi gozlenmistir ve degerlendirilmelidir.
    //
    // `executed` yalnizca tanh olarak (kabul/red sayaci) kullanilir.
    static_cast<void>(executed);
    const Action a = prevAction_;
    const std::size_t s2 = encode(nextObs);
    const double r = reward(nextObs);

    const std::size_t idx = prevState_ * kQActionCount + static_cast<std::size_t>(a);

    double alpha = params_.alpha;
    if (params_.decayAlpha) {
        // Robbins-Monro: alpha_eff = 1/N. Ilk guncellemede tam adim (alpha=1),
        // sonra yavaslayan ortalama. Bu, nadir eylemlerin de ogrenmesini saglar.
        alpha = std::max(params_.alpha, 1.0 / static_cast<double>(sayac_[idx] + 1));
    }
    ++sayac_[idx];

    const double qsa = q(prevState_, a);
    const double hedef = r + params_.gamma * std::max(q(s2, Action::Stay),
                                                     q(s2, Action::Switch));
    setQ(prevState_, a, qsa + alpha * (hedef - qsa));
    // Durum yalnizca HER IKI eylem de degerlendirildiginde "bilinen" sayilir.
    // Tek eylemi bilmek yeterli degildir: karsilastirma yapilamaz.
    if (kStateKnown_.size() != kQStateCount) {
        kStateKnown_.assign(kQStateCount, false);
    }
    kStateKnown_[prevState_] = bilinen(prevState_, Action::Stay) &&
                               bilinen(prevState_, Action::Switch);

    // Zincir devam eder: bir sonraki guncelleme (s2, a_{t+1}) icin.
    prevState_ = s2;
    havePrev_ = true;
}

// ---------------------------------------------------------------------------
// Model kaydet / yukle
// ---------------------------------------------------------------------------
// Deneyin tekrarlanabilir olmasi icin: egitilen tablo dosyaya yazilir, ayni
// tabloyla degerlendirme kosusu yapilir. Aksi halde "ogrenilmis politika"
// her seferinde farkli olur ve K3 karsilastirmasi anlamsizlasir.

void QLearningController::serialize(std::ostream& os) const {
    os << "qlearning-v1 " << kQStateCount << ' ' << kQActionCount << ' '
       << params_.alpha << ' ' << params_.gamma << ' ' << params_.epsilon << ' '
       << ziyaretli_ << ' ' << params_.minVisitsPerAction << '\n';
    os.precision(17);
    for (const double v : tablo) {
        os << v << '\n';
    }
    // Bilinen-durum maskesi AYRI bir satirda: 2560 deger satir basina
    // yazmak dosyayi gereksiz buyuturdu.
    for (std::size_t i = 0; i < kStateKnown_.size(); ++i) {
        os << (kStateKnown_[i] ? 1 : 0) << (i + 1 == kStateKnown_.size() ? '\n' : ' ');
    }
}

void QLearningController::deserialize(std::istream& is) {
    std::string etiket;
    std::size_t n = 0;
    std::size_t a = 0;
    if (!(is >> etiket >> n >> a)) {
        throw std::runtime_error("Q tablosu okunamadi (baslik eksik)");
    }
    if (etiket != "qlearning-v1" || n != kQStateCount || a != kQActionCount) {
        throw std::runtime_error("Q tablosu bicimi uyusmuyor");
    }
    double al = 0.0;
    double ga = 0.0;
    double ep = 0.0;
    std::size_t ziy = 0;
    int mv = 0;
    if (!(is >> al >> ga >> ep >> ziy >> mv)) {
        throw std::runtime_error("Q tablosu parametreleri eksik");
    }
    params_.alpha = al;
    params_.gamma = ga;
    params_.epsilon = ep;
    ziyaretli_ = ziy;
    params_.minVisitsPerAction = mv;

    gorulenler_.assign(n, false);
    kStateKnown_.assign(n, false);
    sayac_.assign(n * a, 0u);
    deneme_.assign(n * a, static_cast<std::uint32_t>(params_.minVisitsPerAction));
    tablo.resize(n * a);
    for (double& v : tablo) {
        if (!(is >> v)) {
            throw std::runtime_error("Q tablosu eksik: veri bitti");
        }
    }

    // Son sayidan sonra satir sonu kaliyor; once bosluk atlanmali yoksa
    // getline BOS satir okur ve maske cozumlenmez.
    is >> std::ws;
    // HATA (testler buldu): "bilinen durum" maskesi tasinmazsa yuklenen
    // kontrolcu ogrenilmis degerleri BILMIYOR ve her yerde tahmin edici
    // davranir. Kaydedilen politika ile yuklenen politika ayni karari
    // vermez — ki deneyin tekrarlanabilirligi butun olarak buna dayanir.
    std::string maskeSatiri;
    if (std::getline(is, maskeSatiri)) {
        std::istringstream m{maskeSatiri};
        for (std::size_t i = 0; i < n; ++i) {
            int bayrak = 0;
            if (!(m >> bayrak)) {
                throw std::runtime_error("Q tablosu bilinen-durum maskesi eksik");
            }
            kStateKnown_[i] = (bayrak != 0);
        }
    } else {
        throw std::runtime_error("Q tablosu bilinen-durum maskesi yok");
    }
}

}  // namespace tsc