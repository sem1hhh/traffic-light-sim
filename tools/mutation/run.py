#!/usr/bin/env python3
"""Mutasyon testi çalıştırıcı — denetim.md madde 6.

Her vakayı tek tek uygular, yeniden derler, ilgili test ikilisini
koşturur ve mutasyonun YAKALANIP YAKALANMADIĞINA bakar.

Raporlanan durumlar (bunlardan biri):
    YAKALANDI    testler kırmızı → koruma çalışıyor
    KACIRILDI    testler yeşil   → BİR TEST BOŞLUĞU VAR, kapatılmadan geçilmez
    UYGULANMADI  aranan metin dosyada yok → vaka bayatlamış, KORUMA KAYIP
    DERLENMEDI   derleme kırıldı → test sonucu GEÇERSİZ, "yeşil" sayılmaz

Iki bilinen tuzağa karşı koruma (AGENTS.md):
  * 8.7  Derlenmeyen mutasyon "geçti" gibi görünür. Bu betik derlemeyi ayrı
         kontrol eder; derlenmezse asla "YAKALANDI"/"KACIRILDI" DEMEZ.
  * 8.13 Statik kütüphane değişince ikili yeniden bağlanmayabilir; eski
         ikili çalışır ve YANLIŞ sonuç üretir. Bu yüzden test ikilileri
         derlemeden önce silinir.

Kullanım:
    python3 tools/mutation/run.py            # tüm vakalar
    python3 tools/mutation/run.py 'kesif'    # adında 'kesif' geçen vakalar
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from cases import CASES  # noqa: E402

BUILD_DIR = os.path.join(ROOT, "build")

# Değişen yola göre hangi test ikilisi koşturulur. Birden fazla eşleşirse
# hepsi koşturulur: bir mutasyonun birden fazla testte yakalanması beklenir.
TEST_BINARIES = {
    "sim-core/": os.path.join(BUILD_DIR, "tests", "run_tests"),
    "controllers/": os.path.join(BUILD_DIR, "controllers", "run_controller_tests"),
    "experiments/": os.path.join(BUILD_DIR, "experiments", "run_experiment_tests"),
}


def sh(args, cwd=ROOT):
    """Komutu liste formunda çalıştırır.

    `shell=True` BİLEREK KULLANILMIYOR: proje yolu boşluk ve Türkçe karakter
    içeriyor ("Müh. Tasarım Proje"); tırnaksız tek dize olarak verilen yol
    kabuk tarafından parçalanır ve derleme sessizce başarısız olur. (Bir kez
    yaşandı: tüm vakalar "DERLENMEDI" çıktı, sebep buydu — AGENTS.md 8.18.)
    """
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


def git_clean():
    return sh(["git", "status", "--porcelain", "--", "sim-core", "controllers", "experiments"]).stdout.strip() == ""


def restore(paths):
    """Kaynağı geri alır ve GERÇEKTEN temizlendiğini doğrular."""
    sh(["git", "checkout", "--"] + list(paths))
    if not git_clean():
        print("!! KAYNAK GERI ALINAMADI — elle temizleyin:", " ".join(paths))
        sys.exit(3)
    return True


def apply_case(dosya, eski, yeni):
    yol = os.path.join(ROOT, dosya)
    with open(yol, encoding="utf-8") as f:
        s = f.read()
    if eski not in s:
        return False
    with open(yol, "w", encoding="utf-8") as f:
        f.write(s.replace(eski, yeni, 1))
    return True


def force_relink():
    """Bayat ikili tuzağına karşı: test ikilileri silinir.

    Statik kütüphane değişse bile CMake bağlamayı tetiklemeyebilir; eski
    ikili çalışır ve MUTASYONU GÖRMEYİP "KACIRILDI" der. (AGENTS.md 8.13 —
    iki kez yaşandı.) İkilinin kendisini silmek bağlamayı zorunlu kılar.
    """
    for p in TEST_BINARIES.values():
        if os.path.exists(p):
            os.remove(p)


def run_tests(changed_paths):
    """Değişen yola göre test ikililerini koşturur. (basari, cikti) döner."""
    uygun = sorted({p for pref, p in TEST_BINARIES.items()
                    if any(cp.startswith(pref) for cp in changed_paths)})
    if not uygun:
        return None, ""
    tum = ""
    for b in uygun:
        if not os.path.exists(b):
            return False, "test ikili yok: " + b
        r = sh([b])
        tum += r.stdout
        tum += r.stderr
    m = re.search(r"Status: (\w+)", tum)
    if not m:
        return False, "Status satiri bulunamadi"
    return (m.group(1) == "SUCCESS"), tum


def main():
    filtre = sys.argv[1] if len(sys.argv) > 1 else ""
    vakalar = [c for c in CASES if filtre in c[0]]
    if not vakalar:
        print("Eslesen vaka yok:", filtre)
        return 1

    if not os.path.isdir(BUILD_DIR):
        print("build/ yok; once: cmake -S . -B build")
        return 1
    if not git_clean():
        print("Calisma agaci kirli; once commit/restore yapin.")
        return 1

    sayim = {"YAKALANDI": 0, "KACIRILDI": 0, "UYGULANMADI": 0, "DERLENMEDI": 0}
    aciklar = []

    for ad, dosya, eski, yeni in vakalar:
        if not apply_case(dosya, eski, yeni):
            print("%-56s UYGULANMADI" % ad)
            sayim["UYGULANMADI"] += 1
            aciklar.append((ad, "UYGULANMADI — vaka bayat, koruma kayip"))
            restore([dosya])
            continue

        force_relink()
        b = sh(["cmake", "--build", BUILD_DIR])
        if b.returncode != 0:
            print("%-56s DERLENMEDI" % ad)
            # Derleme hatasi GOSTERILIR: derlenmeyen mutasyonun test
            # sonucu gecersizdir; "yesil" sayilmaz. (AGENTS.md 8.7)
            for satir in (b.stdout + b.stderr).splitlines()[-6:]:
                print("      " + satir)
            sayim["DERLENMEDI"] += 1
            aciklar.append((ad, "DERLENMEDI — test sonucu gecerli degil"))
            restore([dosya])
            continue

        ok, cikti = run_tests([dosya])
        if ok is None:
            # Bu yola ait test yok: mutasyon dogrulanamaz. "GECTI" DEGILDIR.
            print("%-56s TEST YOK" % ad)
            sayim["KACIRILDI"] += 1
            aciklar.append((ad, "TEST YOK — bu yolda test bulunmuyor"))
        elif ok:
            print("%-56s KACIRILDI" % ad)
            sayim["KACIRILDI"] += 1
            aciklar.append((ad, "KACIRILDI — testler yeşil kaldi"))
        else:
            print("%-56s YAKALANDI" % ad)
            sayim["YAKALANDI"] += 1

        restore([dosya])

    # Son bir dogrulama: her sey geri alindiktan sonra testler yeşil mi?
    force_relink()
    sh(["cmake", "--build", BUILD_DIR])
    ok, _ = run_tests(["sim-core/", "controllers/", "experiments/"])

    print("\n--- OZET ---")
    print("YAKALANDI   : %d" % sayim["YAKALANDI"])
    print("KACIRILDI   : %d" % sayim["KACIRILDI"])
    print("UYGULANMADI : %d" % sayim["UYGULANMADI"])
    print("DERLENMEDI  : %d" % sayim["DERLENMEDI"])
    print("Geri almadan SONRA testler: %s" % ("YESIL" if ok else "KIRMIZI"))
    if aciklar:
        print("\nKAPATILMASI GEREKENLER:")
        for ad, neden in aciklar:
            print("  - %s  [%s]" % (ad, neden))
    return 0


if __name__ == "__main__":
    sys.exit(main())
