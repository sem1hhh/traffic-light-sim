// render/src/coords.h — RENDER KATMANININ TEK KOORDINAT TANIMI.
//
// Neden tek dosya: daha önce ölçek/ofset/yön dağınıktı → araçlar yola göre
// çok büyük, karşı yönler aynı çizgide, bazıları ters bakıyordu. Artık TÜM
// çizim metrikleri burada; sahne (scene3d) ve canvas bunları kullanır.
//
// BİRİM KARARI: 1 DUNYA BIRIMI = 1 IZGARA HUCRESI. Sim-core'da 1 hücre
// kCellLength = 10 metredir. Alt-hücre metrikleri (serit, araç) metre olarak
// verilip m2w() ile hücre-birimi dünyaya çevrilir. Boylece kamera/ölçek
// degismez, her sey tutarli kalir.

#pragma once

#include <cmath>

namespace tsc {
namespace render {

// --- Sim-core'dan tek kaynak ------------------------------------------------
// Hücre uzunluğu (metre). sim-core/include/tsc/network.hpp: kCellLength = 10.0
// Burada TEKRAR tanimlanir ama ayni deger; render sim-core'a linklenir.
constexpr double kMetersPerCell = 10.0;

// Metre -> dünya(hücre) birimi.  1 m = 0.1 dünya birimi.
constexpr double kMetersToCell = 1.0 / kMetersPerCell;

// Dünya birimini metreye çevirir (ters).
constexpr double kCellToMeters = kMetersPerCell;

/// Metreyi dünya (hücre) birimine cevirir.
constexpr double m2w(double meters) { return meters * kMetersToCell; }

// --- Yol ve arac metrikleri (METRE) ----------------------------------------
// Şerit genişliği ve araç boyu standart şehir değerleri (raporla uyumlu).
constexpr double kLaneWidthM = 3.5;   ///< serit genisligi
constexpr double kCarLengthM = 4.5;   ///< arac boyu (IG-6 hedef gorunum)
constexpr double kCarWidthM  = 1.8;   ///< arac genisligi

// Serit ofseti: araç yol MERKEZINDEN saga kayar (sagdan gidiş). Yarım serit.
// Karsi yonler boylece AYNI cizgide kalmaz; ikisi de kendi seritine girer.
constexpr double kLaneOffsetM = kLaneWidthM * 0.5;   // 1.75 m

// --- Kenney taxi.obj model olcusu (kendi birimi) ---------------------------
// OLCUM (2026-10-22, taxi.obj vertexleri):
//   X [-0.750, 0.750]  genislik 1.500
//   Y [ 0.000, 1.500]  yukseklik (taban Y=0)
//   Z [-1.400, 1.350]  boy 2.750  -> UZUN EKSEN Z
// ON EKSEN OLCUMU: OBJ grup adlari `wheel-front-*` / `wheel-back-*`;
//   on tekerlekler Z-merkez +0.760, arkada -0.760 -> model yaw=0'da +Z'ye bakar.
//   Bu yuzden kCarModelFrontAxisDeg = 0 (90/180/270 DEGIL — artik olcumle sabit).
constexpr double kCarModelRawLength = 2.750;  // Z boyu (uzun ekseni)
constexpr double kCarModelRawWidth  = 1.500;  // X genisligi
constexpr double kCarModelFrontAxisDeg = 0.0;  // modelin on ekseni: +Z

// Arac MODEL olcegi: ham model boyunu kCarLengthM metreye goturur.
//   cizim boyu = kCarModelRawLength * kCarModelScale = m2w(4.5) dunya birimi
constexpr double kCarModelScale = m2w(kCarLengthM) / kCarModelRawLength;

// --- Arac ayak-izi (dunya birimi) ------------------------------------------
constexpr double kCarLenW = m2w(kCarLengthM);   // 0.45
constexpr double kCarWidW = m2w(kCarWidthM);    // 0.18
constexpr double kLaneOffsetW = m2w(kLaneOffsetM);  // 0.175

// --- Yon ve serit ofseti ---------------------------------------------------
// Sim yon acisi h = atan2(dx_grid, dy_grid) (SimScene::laneHeadingAt).
// Dünya eksenleri: X = grid x, Z = grid y. İleri vektor (X,Z) = (sin h, cos h).
// Saga dik vektor (X,Z) = (cos h, -sin h).  (sagdan gidiş ofseti icin)

/// İleri (gidis) yonu vektoru (dunya X,Z). `h` radyan.
inline void headingForward(double h, double& fx, double& fz) {
    fx = std::sin(h);
    fz = std::cos(h);
}

/// Saga dik vektor (dunya X,Z). `h` radyan.
inline void headingRight(double h, double& rx, double& rz) {
    rx = std::cos(h);
    rz = -std::sin(h);
}

/// Bir serit Merkez noktasi (dunya) verildiginde, sagdan gidiş serit konumu.
/// Merkez + sag * kLaneOffsetW.  `h` radyan gidis acisi.
inline void applyLaneOffset(double centerX, double centerZ, double h,
                            double& outX, double& outZ) {
    double rx, rz;
    headingRight(h, rx, rz);
    outX = centerX + rx * kLaneOffsetW;
    outZ = centerZ + rz * kLaneOffsetW;
}

constexpr double kPiDeg = 180.0 / 3.14159265358979323846;

/// SERIT YON VEKTORUNDEN arac modelinin dunya yaw derecesi (GlistEngine/
/// glm Y-ekseni donusu). ARTIK gozle degil OLCUMLE:
///   - Modelin on ekseni +Z (kCarModelFrontAxisDeg, OBJ wheel gruplarindan).
///   - Model yaw=θ'de burnu (sin θ, cos θ)'de durur (Ry(θ)*(0,0,1)).
///   - istenen burun yonu (dirX, dirZ); φ = atan2(dirZ, dirX) olunca
///     sin θ = cos φ, cos θ = sin φ  ->  θ = 90° − φ.
inline double carYawDeg(double dirX, double dirZ) {
    const double phi = std::atan2(dirZ, dirX) * kPiDeg;  // atan2(dz, dx)
    double yaw = 90.0 - phi + kCarModelFrontAxisDeg;
    yaw = std::fmod(yaw, 360.0);
    if (yaw < 0.0) yaw += 360.0;
    return yaw;
}

/// Arac ayak-izinin 4 kosesi (world X,Z), saat yonunde, MERKEZ verilir
/// (ofset uygulanmis). `h` gidis acisi (radyan). Kose sirasi: on-sag, on-sol,
/// arka-sol, arka-sag. Debug katmani kutuyu bununla cizir.
inline void carFootprint(double cx, double cz, double h,
                         double& x0, double& z0,
                         double& x1, double& z1,
                         double& x2, double& z2,
                         double& x3, double& z3) {
    double fx, fz, rx, rz;
    headingForward(h, fx, fz);
    headingRight(h, rx, rz);
    const double hl = kCarLenW * 0.5;
    const double hw = kCarWidW * 0.5;
    // on-sag
    x0 = cx + fx * hl + rx * hw;  z0 = cz + fz * hl + rz * hw;
    // on-sol
    x1 = cx + fx * hl - rx * hw;  z1 = cz + fz * hl - rz * hw;
    // arka-sol
    x2 = cx - fx * hl - rx * hw;  z2 = cz - fz * hl - rz * hw;
    // arka-sag
    x3 = cx - fx * hl + rx * hw;  z3 = cz - fz * hl + rz * hw;
}

}  // namespace render
}  // namespace tsc
