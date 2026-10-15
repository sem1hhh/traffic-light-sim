#pragma once

#include "q_learning.hpp"

#include "bench.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tsc {
namespace bench {

/// Egitim yapilandirmasi.
struct TrainConfig {
    int episodes = 400;          ///< egitim bölümü sayısı
    double alpha = 0.40;         ///< öğrenme oranı
    double gamma = 0.95;         ///< indirim faktörü
    double epsilonStart = 0.30;  ///< başlangıç keşif olasılığı
    double epsilonEnd = 0.02;    ///< son keşif olasılığı
    int evalEvery = 20;          ///< kaç bölümde bir değerlendir
    std::uint64_t firstSeed = 1; ///< ilk koşu tohumu
};

/// Bir bölümün sonucu.
struct EpisodeResult {
    int episode = 0;
    double epsilon = 0.0;
    /// Eğitim koşusunun özeti (keşif açıkken ölçülen).
    double trainTravelTime = 0.0;
    /// Değerlendirme koşusunun özeti (keşif KAPALI, politika ölçülür).
    double evalTravelTime = 0.0;
    double evalQueue = 0.0;
    int evalCompleted = 0;
    std::uint64_t evalSwitches = 0;
    std::size_t visitedStates = 0;
};

/// Bir senaryoda Q-learning eğitir ve **değerlendirir**.
///
/// Eğitim ile değerlendirme AYRI koşulardır: eğitimde keşif açıktır ve
/// ölçülen şey öğrenmenin hızıdır. Değerlendirmede keşif kapalıdır ve
/// ölçülen şey **öğrenilen politikanın** başarısıdır. Aynı koşuyu iki kez
/// farklı amaçla kullanmak yaygın ama yanıltıcı bir hata kaynağıdır.
///
/// Dönen tablo, `serialize()` ile kaydedilebilir; aynı tabloyla karşılaştırma
/// koşuları yapılır (K3).
struct TrainResult {
    QLearningController policy;
    std::vector<EpisodeResult> history;
    std::vector<double> evalCurve;      ///< bölüm -> değerlendirme yolculuk süresi
};

TrainResult train(const Scenario& scenario, const TrainConfig& cfg,
                  bool verbose = false);

/// Egitilmis tabloyu dosyaya yazar.
bool savePolicy(const std::string& path, const QLearningController& policy);

/// Dosyadan egitilmis tabloyu okur.
bool loadPolicy(const std::string& path, QLearningController& policy);

}  // namespace bench
}  // namespace tsc