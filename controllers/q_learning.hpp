#pragma once

#include "tsc/controller.hpp"
#include "tsc/intersection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace tsc {

// ---------------------------------------------------------------------------
// Q-learning kontrolcusu
//
// Rapor: "Kontrolcu, Q-learning algoritmasi ile egitilecektir."
// Rapor: "Durum; kavsak kollarindaki kuyruk duzeylerinden, o anki fazdan ve
// fazda gecen sureden olusan yerel bilgidir; eylem, mevcut fazi surdurmek
// veya sonraki faza gecmektir."
//
// TASARIM — neden binning:
// Tabular Q-learning SUREKLI kuyruk degerleriyle ogrenemez. Durum uzayi
// sinirsiz olur ve hicbir durum yeterince ziyaret edilmez. Bu yuzden kuyruk
// degerleri KOVAYA (bin) cevrilir.
//
//   4 kollu kuyruk  : her biri 4 kovaya   -> 4^4  = 256
//   faz             : 2                   -> 2
//   yesilde gecen   : 5 kova             -> 5
//   --------------------------------------------
//   toplam                                   = 2560 durum
//
// Hareketler (duz/sol/sag) kuyruklara BOLUNMEZ; kollar bazinda toplanir.
// Aksi halde durum uzayi 2560 yerine yuz milyonlara cikar ve tabular
// Q-learning ogrenemez. Bu, projenin en onemli tasarim kisitidir.
//
// ODDUL: r = -(kuyruktaki toplam arac sayisi). Raporun tanimidir.
// Diger kombinasyonlar da denenebilir; deney sonuclari rapora yazilir.
// ---------------------------------------------------------------------------

/// Durum uzayi boyutu (4^4 * 2 * 5).
inline constexpr std::size_t kQStateCount = 2560;
/// Eylem sayisi: kal / gec.
inline constexpr std::size_t kQActionCount = 2;

class QLearningController : public IController {
public:
    struct Params {
        double alpha = 0.40;       ///< taban ogrenme orani (asagida birlestirilir)
        /// Ogrenme oranini ziyaret sayacina gore AZALT (Robbins-Monro).
        ///
        /// Sorun (olculdu): bir kosuda Q(Stay) 1078 kez, Q(Switch) yalnizca
        /// 20 kez guncellendi. Cunku her gecis kabulunden sonra ~9 saniye
        /// boyunca yeni istekler reddediliyor (sari + temizleme + minGreen).
        /// Sabit bir alpha ile nadir eylem hic ogrenemiyor; Q(Switch) 0'da
        /// kalirken Q(Stay) derin negatife iniyor ve politika HER YERDE
        /// gecis yapmaya egilimli oluyor.
        ///
        /// Cozum: alpha_eff = 1/N(s,a). Sik kullanilan eylem yavas, nadir
        /// kullanilan eylem hizli ogrenir. Bu standart bir duzeltmedir.
        bool decayAlpha = true;
        double gamma = 0.95;       ///< indirim faktoru
        double epsilon = 0.10;     ///< kesif (keşif) olasiligi
        /// Ziyaret tabanli kesif: bir eylem bu kadar kez denenmedikce
        /// kesif olarak secilir.
        ///
        /// SORUN (olculdu): kesif karari `hash(durum)` ile veriliyordu.
        /// Bu, bir durum icin sabit bir ozelliktir: durum ya HER ZAMAN
        /// kesif yapar ya da HIC. 47 ziyaret edilen durumdan yalnizca 2'si
        /// "Switch"i denedi; Q(Switch) 20 bolum boyunca 2 durumda
        /// guncellendi, Q(Stay) 37 durumda. Politika hicbir sey ogrenemedi.
        ///
        /// Cozum: kesif, olasilik degil SAYIYA baglanir. Bir eylem
        /// `minVisitsPerAction` kez denenmedikce kesif olarak secilir.
        /// Boylece ZIYARET EDILEN her durumda her iki eylem de denenmis
        /// olur. Deterministikligi bozmamak icin "hangi eylem secilecek"
        /// sorusu yine durumdan turetilen bir hash ile cozulur.
        int minVisitsPerAction = 10;
        /// Kuyruk kovalarinin UST sinirlari. 3 deger -> 4 kova:
        /// <=e[0] -> 0, <=e[1] -> 1, <=e[2] -> 2, buyuk -> 3.
        std::array<int, 3> queueEdges{{1, 3, 6}};
        /// Yesilde gecen sure kovalarinin ust sinirlari (4 deger -> 5 kova).
        std::array<double, 4> timeEdges{{5.0, 10.0, 15.0, 22.0}};
        /// Negatif odul; ozdes "bos kuyruk" durumunda da her adim alinir.
        bool penalizeIdle = true;

        /// Odul HANGI kuyruklar icin hesaplanacak?
        ///
        /// Rapor tanimi "odul = -(bekleyen arac sayisi)" der; hangi
        /// kuyruklarin sayildigi belirtilmez. Iki secenek OLCULDU:
        ///
        ///  false (TUM kuyruklar): r = -(kuyruktaki toplam arac)
        ///      Problem: gecis yapmak bir sonraki adimda DAIMA kotu gorunur,
        ///      cunku sarilar ve temizleme suresinde kimse ilerleyemez.
        ///      Faydasi ise ancak yeni faz kuyrugu bosalttikca (5-10 adim
        ///      sonra) ortaya cikar. Olcum: politika HICBIR GECIS YAPMADI
        ///      (1035 Stay / 0 Switch), yolculuk 26.2 s.
        ///
        ///  true (yalnizca KIRMIZI kollar): r = -(simdi servis verilmeyen
        ///      kollardaki arac sayisi)
        ///      Gecis yapildiginda bir sonraki adimda ceza ANINDA duser.
        ///      Sinyal, o an kuyrugu uzun olan eksene gecmeyi ogrenir.
        ///
        /// Bu bir varsayim degil, bir DENEY sonucudur.
        ///
        /// OLCULDU (400 bolum, 10 degerlendirme tohumu, medium rejim):
        ///   TUM kuyruklar (rapor tanimi) : 11.02 s   <-- daha iyi
        ///   yalnizca KIRMIZI kollar       : 12.55 s
        /// light ve saturated rejimlerinde de ayni sira.
        ///
        /// Yani "aninda sinyal veren" odulu TERSINE daha kotu. Sebebi:
        /// kirmizi kol cezasini dusurmek o kolu oynamaya deger kilar ama
        /// yesildeki kuyrugu saymaz; kontrolcu kuyrugu bir kenara birakip
        /// kazancini maksimize eder. TOPLAM bekleyeni cezalandirmak sistem
        /// genelindeki gecikmeyi olcer ve raporun tanimi budur.
        ///
        /// Yan secenek deney icin birakildi; varsayilan RAPORUN TANIMIDIR.
        bool penalizeRedOnly = false;

        /// Bilinmeyen durumda ne yapilir.
        ///
        /// Varsayilan: TAHMIN EDICI (Stay). Tablo sifirla baslatildiginda
        /// ogrenilmemis durumlarin Q degeri 0, ogrenilmislerin degeri negatiftir;
        /// `Q(Switch) > Q(Stay)` karsilastirmasi bu yuzden ogrenilmemis her
        /// durumda Switch'i secer. 2560 durumdan yalnizca 79'u ogrenildiginde
        /// politika OGRENMEDIGI YERLERDE gecis yapmaya calisir ve egitim
        /// BOZULUR (olcum: 12.6 s -> 15.2 s, %20 kotulesme).
        ///
        /// Cozum: Q degeri 0 "sifir deger" demek deil, "henuz
        /// degerlendirilmedi" demektir. Bilinmeyen durumda varsayilan eylem
        /// uygulanir; kesif o durumu ziyaret ederek ogrenmeyi saglar.
        bool assumeUnknownStay = true;
    };

    QLearningController() { clearTable(); }
    explicit QLearningController(Params p) : params_(p) { clearTable(); }

    void onReset() override;

    Action decide(const SignalObservation& obs) override;

    void onReward(const SignalObservation& nextObs, Action executed) override;

    // --- Tablo erisimi (test ve egitim araclari icin) ----------------------
    std::size_t encode(const SignalObservation& obs) const;
    double q(std::size_t state, Action a) const;
    void setQ(std::size_t state, Action a, double v);

    /// Deger fonksiyonundan gelen politika (kesif YOK). Rapor karsilastirmasinda
    /// "ogrenilmis politika" budur; epsilon'lu oynayan politika degil.
    Action greedyAction(const SignalObservation& obs) const;

    std::size_t visitedStates() const noexcept { return ziyaretli_; }

    /// Kesif olasiligini degistirir (egitim dongusu bunu azaltarak kullanir).
    /// DIKKAT: degerlendirme kosusundan ONCE 0 yapilmalidir; aksi halde
    /// "ogrenilmis politika" olcumunde kesif karisip sonucu degistirir.
    void setEpsilon(double e) { params_.epsilon = e; }
    void setAlpha(double a) { params_.alpha = a; }
    void setGamma(double g) { params_.gamma = g; }
    void setMinVisits(int m) { params_.minVisitsPerAction = m; }
    void setQueueEdges(int a, int b, int c) { params_.queueEdges = {{a, b, c}}; }
    void setTimeEdges(double a, double b, double c, double d) { params_.timeEdges = {{a, b, c, d}}; }
    void setRedOnly(bool b) { params_.penalizeRedOnly = b; }
    void setDecayAlpha(bool b) { params_.decayAlpha = b; }

    /// Politikanin KOPYASI. SignalDriver her kavsaga ayri kopya ister;
    /// (s,a,r,s') zinciri kavsaklar arasi tasinmamalidir.
    QLearningController copy() const { return *this; }
    std::size_t tableSize() const noexcept { return tablo.size(); }
    void clearTable();
    const Params& params() const noexcept { return params_; }

    /// Ogrenilmis durum: HER IKI eylem de en az bir kez degerlendirilmis.
    /// Tabloyu analiz etmek ve "ogrenme gercekten oluyor mu" diye
    /// denetlemek icin gerekli (§8.10: "ogrenildi" degil "olcumu degistirdi").
    bool isKnown(std::size_t state) const;
    std::size_t knownStates() const;
    bool hasLearning() const noexcept { return knownStates() > 0; }
    double meanAbsQ() const;

    // --- Model kaydet/yukle (deney tekrarlanabilirligi icin) --------------
    void serialize(std::ostream& os) const override;
    void deserialize(std::istream& is) override;

    /// Baska bir tabloyu BIRLESTIRIR (ortalama).
    ///
    /// Neden gerekiyor: SignalDriver her kavsaga ayri bir kontrolcu
    /// uretir; hepsi ayni modelden baslar ama farkli trafik gordukleri
    /// icin tablolari ayrilir (olculdu: 8 / 7 / 8 bilinen durum). Ilk
    /// yazimda geri yazma dongusu `model = c.copy()` ile tum kavsaklari
    /// geziyor ve her seferinde modeli EZIYORDU; yani uc kavsagin
    /// ikisinin ogrenmesi atiliyordu.
    ///
    /// Ortalama yerine "ilk kavsagi al" secildi: secim yapmak ek bir
    /// karardir ve gerekcesi zordur; ortalama secimsiz ve simetrik.
    ///
    /// SÖZLEŞME: bu işlev **taze bir toplayıcı** üzerinde kullanılır.
    /// İlk çağrı tabloyu `other` ile değiştirir, sonrakiler artımlı ortalama
    /// yapar. N sonucu N kaynağın **eşit ağırlıklı ortalaması**dır.
    ///
    /// DIKKAT: artımlı (running) ortalama kullanır. Düz `t = (t + o)/2`
    /// yazımı YANLIŞTIR: c1,c2,c3 sırayla eklendiğinde ağırlıklar
    /// 1/8, 1/4, 1/2 olur — yani "ortalama" aslında SON kavşağa ağırlık
    /// vermekten başka bir şey değildir ve düzeltilen hata geri gelir.
    /// (Bu hata yaşandı: ilk uygulamada `(t+o)/2` yazılmıştı.)
    void mergeFrom(const QLearningController& other);


private:
    /// Odul: kuyruk ne kadar buyukse o kadar kotu.
    double reward(const SignalObservation& obs) const;

    /// Ziyaret tabanli eps-greedy.
    Action epsilonGreedy(std::size_t state) const;

    /// Bu eylem yeterince denenmis mi?
    bool sayacokul(std::size_t state, Action a) const;

    Params params_;
    std::vector<double> tablo;

    // Onceki adim (Q-learning'in (s,a,r,s') zinciri icin).
    bool havePrev_ = false;
    std::size_t prevState_ = 0;
    Action prevAction_ = Action::Stay;

    /// TEKIL ziyaret edilen durum sayisi. Birikimli sayac degil: ayni durum
    /// 3000 kez gorulse de 1 sayilir. (Ilk yazimda birikimliydi ve "16150/
    /// 2560 = %631" gibi anlamsiz bir oran cikiyordu.)
    std::vector<bool> gorulenler_;
    std::vector<bool> kStateKnown_;   ///< her iki eylemi de degerlendirilmis mi
    std::vector<std::uint32_t> sayac_;   ///< N(s,a): kac kez DEGERLENDIRILDI
    std::vector<std::uint32_t> deneme_;  ///< kac kez DENENDI (kesif icin)
    std::size_t mergeCount_ = 0;         ///< birlestirilen tablo sayisi
    /// (s,a) ikisinin de degerlendirilmis olup olmadigi.
    bool bilinen(std::size_t s, Action a) const {
        const std::size_t i = s * kQActionCount + static_cast<std::size_t>(a);
        return i < sayac_.size() && sayac_[i] > 0;
    }
    std::size_t ziyaretli_ = 0;   ///< TEKIL ziyaret edilen durum sayisi
    mutable std::uint64_t kesifSayisi_ = 0;
};

}  // namespace tsc