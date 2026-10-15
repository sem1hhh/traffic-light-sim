#pragma once

#include <cstdint>

namespace tsc {

// ---------------------------------------------------------------------------
// Arac takip modeli: IDM (Intelligent Driver Model)
//
// Treiber, Hennecke & Helbing (2000). Gercekci parcac modeli sunar:
// hizlanma, yavaslama, durak mesafesi ve karsi arac takibi tek denklemde.
//
//   dv/dt = a * [ 1 - (v/v0)^4 - (s*/s)^2 ]
//   s*   = s0 + max( 0 , v*T + v*dv / (2*sqrt(a*b)) )
//
//     v  : anlik hiz            v0 : hedef hiz
//     a  : maks. ivme           b  : konforlu yavaslama
//     s  : ondeki araca mesafe  s0 : minimum durak mesafesi
//     T  : guvenli zaman basligi
//     dv : v - v_ondeki
//
// Bu modul SAF fonksiyonlardan olusur; World'a bagli degildir, bu yuzden
// izole ve hizli test edilir.
//
// Neden "basit arac takip modeli" degil: ana raporda bu ifade geciyor ama
// tek basina carpisma garantisi VERMEZ. K1 (carpisma = 0) icin IDM + kavsak
// giris clamp'i gerekir (GEREKLILIK.md §7).
// ---------------------------------------------------------------------------

struct IdmParams {
    double desiredSpeed = 13.9;    ///< v0, m/s (~50 km/s)
    double minGap = 2.0;          ///< s0, m
    double timeHeadway = 1.5;     ///< T, s
    double maxAccel = 2.0;        ///< a, m/s^2
    double comfortableDecel = 3.0; ///< b, m/s^2 (pozitif)

    bool valid() const noexcept;
};

/// Bir engel (veya ondeki arac) icin IDM ivmesi.
///
/// @param speed      aracin anlik hizi (m/s), >= 0
/// @param gap        ondeki engele olan net mesafe (m)
/// @param leadSpeed  ondeki engelin hizi (m/s); durak çizgisi icin 0
/// @return ivme (m/s^2). Pozitif = hizlanir, negatif = yavaslar.
double idmAcceleration(const IdmParams& p, double speed, double gap, double leadSpeed);

/// Durak cizgisi gibi HAREKETSIZ engel icin kisa yol: ivme hesapla,
/// hiz guncellenmis degeri dondur.
double idmSpeedAfterStep(const IdmParams& p, double speed, double gap,
                         double leadSpeed, double dt, double speedLimit);

/// Serbest akista (engel yok) ivme: yalnizca hedef hiza yaklasma.
double idmFreeAcceleration(const IdmParams& p, double speed);

/// Maksimum fiziksel frenleme ile hiz sinirla.
///
/// Sinyal degistirdiginde arac aninde duramaz; fren mesafesi gerekir.
/// Guvenlik kurallari suresi (yesil->sari->kirmizi) bu yuzden oyle secilmistir.
double clampSpeed(double speed, double maxDecel, double dt);

}  // namespace tsc