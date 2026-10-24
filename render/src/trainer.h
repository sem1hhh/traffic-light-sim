// render/src/trainer.h — IG-6: Train tusunun arka plan egitimi.
//
// TASARIM (İOG-3 ile birebir):
//   - Ogrenme TEK bir is parcaciginda yapilir (worker_). Q tablosuna ayni
//     anda yalnizca o thread yazar; arayuz thread'i Q'ya DOKUNMAZ.
//   - Arayuz thread'i yalnizca ILERLEME SAYILARINI (kilit altinda kopyalanan
//     Snapshot) okur. Sayilar birer kopya; paylasilan Q tablosu yok.
//   - Egitim bitince politika `takeFinished()` ile BIR KEZ arayuz thread'ine
//     devredilir. Devir aninda worker durmustur, yani tabloya es zamanli
//     erisim olmaz.
//
// Neden ayri dosya: egitim kodu experiments/ altindaki DOGRULANMIS
// train()/Scenario'dir; burada yeniden yazilmaz (AGENTS.md: render is
// mantigi yeniden yazmaz). sim_scene.cpp bu basligi GORMEZ, boylece
// tsc_measure_offset / tsc_eval_check hedefleri experiments'a bagimli olmaz.

#pragma once

#include "q_learning.hpp"      // QLearningController
#include "tsc/world.hpp"       // WorldConfig

#include <mutex>
#include <string>
#include <thread>

namespace tsc {
namespace render {

/// Arka plan Q-learning egitimi. Nesne bir "is"tir: ayni anda tek egitim.
class BackgroundTrainer {
public:
    enum class State {
        Idle = 0,   ///< hic egitim yapilmadi
        Running,    ///< worker calisiyor
        Done,       ///< bitti, politika teslim edilmeyi bekliyor/edildi
        Cancelled,  ///< kapatma sirasinda iptal edildi
        Failed      ///< istisna (log'da mesaj var)
    };

    /// Arayuzun okudugu ILERLEME KOPYASI (kilit altinda alinir).
    struct Snapshot {
        State state = State::Idle;
        int episodes = 0;        ///< toplam bolum
        int done = 0;            ///< tamamlanan bolum (0..episodes)
        int currentEpisode = 0;  ///< son biten bolum (0 tabanli)
        double epsilon = 0.0;
        double evalTravelTime = 0.0; ///< son bolumun degerlendirme yolculuk suresi (s)
        int visitedStates = 0;       ///< ogrenilen durum sayisi
        double elapsedSeconds = 0.0; ///< egitim suresi (gercek saniye)
        double maxFrameMs = 0.0;     ///< arayuzun EN UZUN karesi (donma kaniti)
        bool saved = false;          ///< model dosyaya yazildi mi
        std::string savePath;        ///< yazilan dosya (bos: denenmedi)
        std::string message;         ///< hata/ek bilgi
    };

    BackgroundTrainer() = default;
    ~BackgroundTrainer();

    BackgroundTrainer(const BackgroundTrainer&) = delete;
    BackgroundTrainer& operator=(const BackgroundTrainer&) = delete;

    /// Egitsiz baslar. `live`, CANLI kosunun yapilandirmasidir: egitim canli
    /// senaryoyla AYNI talep profilini kullanir (dagilim disi egitim olmasin).
    /// Zaten bir egitim suruyorsa false doner ve hicbir sey degismez.
    bool start(int episodes, const WorldConfig& live);

    /// Kilit altinda ilerleme kopyasi.
    Snapshot snapshot() const;

    /// Egitim bittiyse politikayi BIR KEZ teslim eder (true). Baska cagride
    /// false. Arayuz thread'i cagirir.
    bool takeFinished(QLearningController& out);

    bool running() const;

    /// Arayuz karesinin suresi (ms). Worker DEGIL, arayuz thread'i cagirir;
    /// yalnizca arayuzun donmadigini olcmek icindir.
    void noteFrame(double dtSeconds);

private:
    void run(int episodes, WorldConfig live);
    mutable std::mutex mu_;
    Snapshot snap_;
    QLearningController result_;
    bool hasResult_ = false;

    /// Iptal bayragi: kapatma sirasinda worker'a "bir sonraki bolumde dur"
    /// der. Yalnizca callback'te okunur; Q tablosuna dokunmaz.
    bool cancel_ = false;

    std::thread worker_;
};

}  // namespace render
}  // namespace tsc
