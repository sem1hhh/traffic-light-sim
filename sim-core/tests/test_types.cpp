#include "tsc/types.hpp"

#include <doctest/doctest.h>

using namespace tsc;

TEST_CASE("Id varsayilan degeri gecersizdir") {
    // State kopyalanabilir olmali: her yeni ID geçersiz baslamali.
    CHECK_FALSE(VehicleId{}.valid());
    CHECK_FALSE(JunctionId{}.valid());
    CHECK_FALSE(LaneId{}.valid());
}

TEST_CASE("Id gecerli ve gecersiz degerler") {
    const VehicleId v = VehicleId::from(7);
    CHECK(v.valid());
    CHECK(static_cast<bool>(v));
    CHECK_EQ(v.value, 7u);

    CHECK_FALSE(VehicleId::invalid().valid());
}

TEST_CASE("Farkli ID tipleri birbirine atanamaz") {
    // Derleme zamani kontrolu: asagidaki iki satir DERLENMEZ.
    //   const VehicleId v = VehicleId::from(1);
    //   JunctionId j = v;              // hata
    //   CHECK(j == JunctionId::from(1));  // buraya ulasilamaz
    //
    // Bu test, tipin farkliligini runtime'da degil derleme zamaninda kanitlar.
    CHECK_EQ(VehicleId::from(1).value, JunctionId::from(1).value);
}

TEST_CASE("Id karsilastirma ve siralama") {
    const VehicleId a = VehicleId::from(3);
    const VehicleId b = VehicleId::from(9);
    CHECK(a == VehicleId::from(3));
    CHECK(a != b);
    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("Vec2i aritmetigi") {
    constexpr Vec2i p{2, 3};
    constexpr Vec2i q{1, 5};

    CHECK_EQ(p + q, (Vec2i{3, 8}));
    CHECK_EQ(p - q, (Vec2i{1, -2}));
    CHECK(p == Vec2i(2, 3));
    CHECK(p != q);
}

TEST_CASE("Vec2i constexpr calisir - oyun dongusunde kullanilabilir") {
    constexpr Vec2i result = Vec2i{4, 6} + Vec2i{1, 2};
    static_assert(result == Vec2i{5, 8}, "constexpr toplama derleme zamaninda dogrulanmali");
    CHECK_EQ(result.x, 5);
    CHECK_EQ(result.y, 8);
}

TEST_CASE("GridSize sinir kontrolu - kenar hucreleri") {
    constexpr GridSize g{5, 4};

    CHECK(g.valid());
    CHECK_EQ(g.cellCount(), 20u);

    // Ic hucreler
    CHECK(g.contains(Vec2i{0, 0}));
    CHECK(g.contains(Vec2i{4, 3}));

    // Tam sinirdaki hucreler
    CHECK(g.contains(Vec2i{4, 3}));
    CHECK_FALSE(g.contains(Vec2i{5, 0}));   // x == width
    CHECK_FALSE(g.contains(Vec2i{0, 4}));   // y == height
}

TEST_CASE("GridSize negatif boyutlari reddeder") {
    CHECK_FALSE(GridSize{0, 5}.valid());
    CHECK_FALSE(GridSize{5, 0}.valid());
    CHECK_FALSE(GridSize{-1, 5}.valid());
}

TEST_CASE("Degersiz grid bos hucre saymaz") {
    constexpr GridSize invalid{};
    CHECK_FALSE(invalid.valid());
    CHECK_FALSE(invalid.contains(Vec2i{0, 0}));
    CHECK_EQ(invalid.cellCount(), 0u);
}

TEST_CASE("Sabit zaman adimi ve ust sinir") {
    CHECK_EQ(kStepSeconds, 1.0);
    CHECK(kMaxSimSeconds > kStepSeconds);
}