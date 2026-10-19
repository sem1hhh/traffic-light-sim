"""Mutasyon testi vakaları — denetim.md madde 6.

Her vaka: (açıklama, hedef dosya, ARANACAK metin, YERİNE GELECEK metin)

Sözleşme ve uyarılar için `run.py` dosyasına ve README.md'ye bakın.

`aranan` metni hedef dosyada BİREBİR bulunmalıdır. Bulunmazsa vaka
"UYGULANMADI" raporlanır — bu, korumanın kaybolduğu anlamına gelir ve
sessizce geçilmez. Kod değiştikçe bu liste bayatlar.

SON KOŞU (2026-10-19): 25 vaka — **23 YAKALANDI, 2 KACIRILDI, 0 DERLENMEDI,
0 UYGULANMADI.** Geri aldıktan sonra testler yeşil.

### İki "KACIRILDI" — ikisi de doğrulanmış boşluk DEĞİL

  1. "minGreen: gecis kapisindan kaldirilir (YEDEK)".
     minGreen ÜÇ ayrı yerde zorlanır: `advanceState` geciş kapısı,
     `requestSwitch` reddi, `canSwitch`. Bu vaka yalnızca birincisini
     kaldırır. Deneyerek doğrulandı: üçü birden kaldırılınca testler
     3 assertion'da kırmızıya döner. Yani bu "KACIRILDI", savunmacı
     tekrarın tek başına silinmesidir — gerçek bir boşluk değildir.
     `requestSwitch` ve `canSwitch` vakaları YAKALANDI; asıl gate budur.

  2. "bekleme sutunu olceilmemis degerleri elemez".
     `bench_main.cpp` bir `main()`'dir ve testi YOKTUR. Doğru okuma
     "BU YOLDA HİÇ TEST YOK"tur, "testler geçti" değil.

### Önceki koşunun YANLIŞ "gerçek boşluk" kararı (düzeltildi)

`run_tests` yalnızca **değişen dosyanın klasöründeki** test ikilisini
koşturuyordu. `mergeFrom` `controllers/` altında; ama bu davranışın testi
`experiments/train_test.cpp`'de. İki mutasyon bu yüzden "KACIRILDI"
raporlanmıştı ve bunlardan biri raporda "TEK GERÇEK BOŞLUK" diye
yazılmıştı. **Yanlıştı.** `run_tests` artık ÜÇ ikilisini de koşturuyor;
iki vaka da YAKALANDI — bağımsız denetimin elle yaptığı ölçüme birebir
uyuyor (2 kırmızı / 5 kırmızı).

> Not: aynı düzeltmenin ilk denemesi `re.search` ile İLK `Status:` satırını
> okuyordu; üç ikili çalıştırılınca ilk ikisi yeşil, üçüncüsü kırmızı
> olabiliyordu ve **16 vaka** ters raporlandı. `findall` + "hepsi SUCCESS"
> şartı gerekir.
"""

CASES = [
    # --- sim-core: olcum zinciri ve kisitlar -------------------------------
    (
        "sansurlu metrik geri gelir (takilan araclari sayma)",
        "sim-core/src/world.cpp",
        "    for (const Vehicle& v : vehicles_) {\n"
        "        toplam += time_ - v.spawnTime;\n"
        "        ++sayi;\n"
        "    }",
        "    static_cast<void>(vehicles_);",
    ),
    (
        "varis orani hep 1.0 doner (sansur gorunmezlesir)",
        "sim-core/include/tsc/world.hpp",
        "        return spawned > 0 ? static_cast<double>(arrived) / static_cast<double>(spawned)\n"
        "                           : 0.0;",
        "        return 1.0;",
    ),
    (
        "bekleme suresi varis yokken 0.0 doner (eski tuzak)",
        "sim-core/include/tsc/world.hpp",
        "        if (!waitTimeMeasured || arrived == 0) {",
        "        if (!waitTimeMeasured) {",
    ),
    (
        "minGreen: gecis kapisindan kaldirilir (YEDEK — yetmemeli)",
        "sim-core/src/intersection.cpp",
        "    const bool zamanDoldu = greenElapsed_ >= timing_.minGreen;",
        "    const bool zamanDoldu = true;",
    ),
    (
        "minGreen: requestSwitch reddi kaldirilir (ASIL gate)",
        "sim-core/src/intersection.cpp",
        "    if (greenElapsed_ < timing_.minGreen) {\n"
        "        ++rejectedSwitches_;\n"
        "        return false;\n"
        "    }\n"
        "    switchRequested_ = true;",
        "    switchRequested_ = true;",
    ),
    (
        "minGreen: canSwitch hep true doner",
        "sim-core/src/intersection.cpp",
        "    return state_ == SignalState::Green && greenElapsed_ >= timing_.minGreen;",
        "    return state_ == SignalState::Green;",
    ),
    (
        "maxGreen asildiginda gecis zorunlulugu kaldirilir",
        "sim-core/src/intersection.cpp",
        "    const bool zamanAsimi = greenElapsed_ >= timing_.maxGreen;",
        "    const bool zamanAsimi = false;",
    ),

    # --- controllers: kural ve ogrenme -------------------------------------
    (
        "kural: minGreen guvenligi silinir",
        "controllers/threshold.cpp",
        "    if (obs.greenElapsed < obs.timing.minGreen) {\n"
        "        return Action::Stay;\n"
        "    }\n\n",
        "",
    ),
    (
        "kural: canSwitch guvenligi silinir",
        "controllers/threshold.cpp",
        "    if (!obs.canSwitch()) {\n"
        "        return Action::Stay;\n"
        "    }\n",
        "",
    ),
    (
        "kural: azami yesil zorunlulugu silinir",
        "controllers/threshold.cpp",
        "    if (obs.greenElapsed >= effectiveMaxGreen(obs)) {",
        "    if (false) {",
    ),
    (
        "kural: basinc farki kurali silinir",
        "controllers/threshold.cpp",
        "    if (diger >= mevcut + params_.pressureMargin && diger > params_.urgentThreshold) {",
        "    if (false && diger >= mevcut) {",
    ),
    (
        "odul isareti terslenir (pozitif)",
        "controllers/q_learning.cpp",
        "    return -static_cast<double>(bekleyen);",
        "    return static_cast<double>(bekleyen);",
    ),
    (
        "kesif sayaci kullanilmaz (her yerde kesif yapilir)",
        "controllers/q_learning.cpp",
        "    if (azStay || azSwitch) {",
        "    if (true) {",
    ),
    (
        "bilinmeyen durumda Switch secilir",
        "controllers/q_learning.cpp",
        "    if (params_.assumeUnknownStay && !isKnown(state)) {\n"
        "        return Action::Stay;\n"
        "    }\n",
        "",
    ),
    (
        "mergeFrom diger kavsagin ogrenmesini yok sayar",
        "controllers/q_learning.cpp",
        "        tablo[i] = (tablo[i] * n + other.tablo[i]) / (n + 1.0);",
        "        tablo[i] = (tablo[i] * n) / (n + 1.0);",
    ),
    (
        "bilinen maskesi AND ile birlesir (OR yerine)",
        "controllers/q_learning.cpp",
        "            kStateKnown_[i] = kStateKnown_[i] || other.kStateKnown_[i];",
        "            kStateKnown_[i] = kStateKnown_[i] && other.kStateKnown_[i];",
    ),

    # --- experiments: olcum ve deney hatti ----------------------------------
    (
        "degerlendirme yine modele geri yazar (model degisir)",
        "experiments/train.cpp",
        "        cfg.evalFirstSeed + static_cast<std::uint64_t>(k), false);",
        "        cfg.evalFirstSeed + static_cast<std::uint64_t>(k), true);",
    ),
    (
        "degerlendirme tohumu egitim tohumuna cakiser (sizinti)",
        "experiments/train.hpp",
        "    std::uint64_t evalFirstSeed = 9001;",
        "    std::uint64_t evalFirstSeed = 1;",
    ),
    (
        "kavsaklar birlestirilmez (sonuncusunun ogrenmesi kalir)",
        "experiments/train.cpp",
        "            birlestirilmis.mergeFrom(c);",
        "            birlestirilmis = c.copy();",
    ),
    (
        "BULGU 1: degerlendirme tohum sayisi bayragi yok sayilir",
        "experiments/train_main.cpp",
        "    const TrainConfig k = evaluationConfig(opt.episodes, opt.evalSeeds);",
        "    const TrainConfig k = evaluationConfig(opt.episodes, 5);",
    ),
    (
        "runOnce tohumu kaydirilir (seed + 1)",
        "experiments/bench.cpp",
        "    World world{net, cfg, seed};",
        "    World world{net, cfg, seed + 1};",
    ),
    (
        "runMany her kosuda ayni tohumu kullanir",
        "experiments/bench.cpp",
        "        RunResult r = runOnce(scenario, factory, firstSeed + static_cast<std::uint64_t>(i));",
        "        RunResult r = runOnce(scenario, factory, firstSeed);",
    ),
    (
        "BULGU 3: guven araligi normal kuantile duser",
        "experiments/stats.cpp",
        "    return studentTQuantile(0.975, df);",
        "    static_cast<void>(df);\n    return 1.959963984540054;",
    ),
    (
        "BULGU 3: degenerate bayragi kurulmaz (NaN degerler duruyor)",
        "experiments/stats.cpp",
        "        r.degenerate = true;",
        "        r.degenerate = false;",
    ),
    (
        "bekleme sutunu olceilmemis degerleri elemez",
        "experiments/bench_main.cpp",
        "        if (r.averageWaitTime >= 0.0) {   // -1.0 isareti ele",
        "        if (true) {   // -1.0 isareti ELEME",
    ),
]
