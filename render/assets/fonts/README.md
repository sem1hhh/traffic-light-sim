# assets/fonts

## FreeSans (FreeSans.ttf, FreeSansBold.ttf, FreeSansOblique.ttf, FreeSansBoldOblique.ttf)

GlistEngine'in kendi ornek uygulamalarindan (`samples/gisSpline`,
`samples/gisScreenSpaceReflection`) alindi; motorun `gGUIManager` bu dort
dosyayi **sabit adlarla** yukler. Eksik oldugunda konsola
`gFont: Freetype error` yazilir (2026-10-22'de 4 kez goruldu).

- Kaynak: GNU FreeFont (FreeSans)
- Lisans: GPLv3+ **font embedding exception** ile
  (<https://www.gnu.org/software/freefont/license.html>)
- Kullanim: yalnizca arayuz metni (HUD / menu) icin; gomulu (embedded) font
  olarak uygulamayla birlikte dagitilir.

> Not: Projenin 3B modelleri CC0 (Kenney). Fontlar CC0 degildir; raporda
> "3B modeller CC0" ifadesi fontlari kapsamaz, ayri belirtilmelidir.
