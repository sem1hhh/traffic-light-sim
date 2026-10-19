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

Her koşumda `sim-core` + `controllers` + `experiments` test ikililerinin
**hepsi** çalışır (yola göre filtre yok — bkz. aşağıdaki eski hata).

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

## Durum (2026-10-19)

25 vaka: **23 YAKALANDI, 2 KACIRILDI, 0 DERLENMEDI, 0 UYGULANMADI.** Geri
aldıktan sonra testler yeşil. İki "KACIRILDI" da doğrulanmış boşluk
**değil** — gerekçeleri `cases.py`'nin başlık açıklamasında:

> `minGreen` geciş kapısı (üç yerde zorlanan savunmacı tekrarın biri) ve
> `bench_main.cpp` (testi olmayan bir `main()`).

### Eski "gerçek boşluk" kararı yanlıştı

Önceki sürüm `run_tests` **değişen dosyanın klasöründeki** ikiliyi
koşturuyordu. `mergeFrom` `controllers/` altında olduğu için onun testi
olan `experiments/train_test.cpp` hiç çalışmıyor ve iki mutasyon
"KACIRILDI" çıkıyordu — bunlardan biri "TEK GERÇEK BOŞLUK" diye
raporlanmıştı. Artık **üç ikili de her mutasyonda** koşturuluyor; iki vaka
da YAKALANDI (bağımsız denetimin elle ölçümüyle aynı: 2 ve 5 kırmızı).

> Aynı düzeltmenin ilk denemesi bir başka tuzak da yakaladı: `re.search`
> **ilk** `Status:` satırını döndürür, üç ikili sırayla yazılınca ilk ikisi
> yeşil kalıp üçüncüsü kırmızı olabilir. 16 vaka ters raporlandı. Doğrusu
> `re.findall` + **hepsi SUCCESS** şartıdır.

Vaka eklemek için `cases.py`'ye `(açıklama, dosya, aranan, yeni)` satırı
ekleyin. Yeni davranış için **önce test, sonra kod** (AGENTS.md §6).
