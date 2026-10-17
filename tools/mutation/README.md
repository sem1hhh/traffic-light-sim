# Mutasyon testi

Testlerin gerçekten neyi ölçtüğünü doğrulamak için kullanılır: kodu **bilerek**
bozup testlerin kırmızıya döndüğünü görürüz. Testler yeşil kalıyorsa test
yoktur.

Bu betikler daha önce `/tmp` altında yaşıyordu; oturum ortasında bayattı ve
tekrarlanamıyordu (denetim.md madde 6).

## Çalıştırma

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"
python3 tools/mutation/run.py            # tüm vakalar
python3 tools/mutation/run.py 'kesif'    # adında 'kesif' geçenler
```

## Durumlar

| Çıktı | Anlamı |
|---|---|
| `YAKALANDI` | testler kırmızı → koruma çalışıyor |
| `KACIRILDI` | testler yeşil → **bir test boşluğu var** |
| `UYGULANMADI` | aranan metin dosyada yok → vaka bayat, **koruma kayıp** |
| `DERLENMEDI` | derleme kırıldı → test sonucu **geçersiz** |
| `TEST YOK` | o yolda hiç test yok → doğrulanamıyor |

`UYGULANMADI` bir hatadır, geçilmez: kod değiştikçe vakalar bayatlar ve
"korumayı kaybettiğin yer" ancak bu uyarıdan anlaşılır.

## İki bilinen tuzak (AGENTS.md)

* **8.7 — derlenmeyen mutasyon "geçti" sanılır.** Derleme ayrı kontrol edilir;
  başarısızsa asla `YAKALANDI`/`KACIRILDI` yazmaz, derleme çıktısının son
  satırlarını basar.
* **8.13 — bayat ikili.** Statik kütüphane değişse de CMake bağlamayı
  tetiklemeyebilir; eski ikili çalışır ve mutasyonu görmez. Test ikilileri
  derlemeden **önce silinir**.

## Yol tuzağı

Proje yolu boşluk ve Türkçe karakter içerir (`Müh. Tasarım Proje`).
Bu yüzden `subprocess` `shell=True` ile değil **liste formunda** çağrılır;
tek dize verilen yol kabuk tarafından parçalanıyor ve derleme sessizce
başarısız oluyordu (bütün vakalar "DERLENMEDI" çıktı).

## Durum (2026-10-18)

25 vaka: **22 YAKALANDI, 3 KACIRILDI, 0 DERLENMEDI**. Geri aldıktan sonra
testler yeşil. Üç "KACIRILDI"nın ikisi doğrulanmış boşluk değil —
gerekçeler `cases.py`'nin başlık açıklamasında.

Kapatılması gereken **gerçek** boşluk:

> `controllers/q_learning.cpp::mergeFrom` içinde `kStateKnown_` birleşimi
> ezilirse hiçbir test kırmızıya dönmüyor.

Vaka eklemek için `cases.py`'ye `(açıklama, dosya, aranan, yeni)` satırı
ekleyin. Yeni davranış için **önce test, sonra kod** (AGENTS.md §6).
