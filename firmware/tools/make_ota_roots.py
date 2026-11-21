#!/usr/bin/env python3
"""certs/ota-roots.pem -> src/sys/ota_roots.h

Güncelleme (GitHub) bağlantılarında kullanılan kök sertifikaları, Arduino-ESP32'nin
WiFiClientSecure::setCACertBundle() biçimine çevirir: yalnızca konu adı (DER) ve açık anahtar
(SubjectPublicKeyInfo DER) saklanır, konu adına göre sıralanır (kartta ikili arama yapılır).

Biçim:  [uint16 sayı] + her kök için [uint16 ad_uzunluğu][uint16 anahtar_uzunluğu][ad][anahtar]
        (büyük uçlu; ESP-IDF gen_crt_bundle.py ile aynı)

Kullanım: python3 tools/make_ota_roots.py           (başlığı yazar)
          python3 tools/make_ota_roots.py --check   (başlık PEM ile uyuşmuyorsa hata verir; CI)
"""
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
PEM = os.path.join(HERE, "..", "certs", "ota-roots.pem")
OUT = os.path.join(HERE, "..", "src", "sys", "ota_roots.h")


def build():
    certs = x509.load_pem_x509_certificates(open(PEM, "rb").read())
    items = []
    for c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        items.append((name, key, c.subject.rfc4514_string()))
    items.sort(key=lambda t: t[0])
    blob = bytearray(len(items).to_bytes(2, "big"))
    for name, key, _ in items:
        blob += len(name).to_bytes(2, "big") + len(key).to_bytes(2, "big") + name + key
    lines = [
        "#pragma once",
        "// OTOMATİK ÜRETİLDİ: tools/make_ota_roots.py (kaynak: certs/ota-roots.pem). Elle düzenleme.",
        "// Güncelleme bağlantılarında (api.github.com, github.com, indirme sunucusu) güvenilen kökler,",
        "// WiFiClientSecure::setCACertBundle() biçiminde. Sırası (konu adına göre) önemlidir.",
    ]
    lines += ["//   " + d for _, _, d in items]
    lines.append("static const uint8_t OTA_ROOTS[] = {")
    for i in range(0, len(blob), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in blob[i:i + 16]) + ",")
    lines.append("};")
    lines.append("static const size_t OTA_ROOTS_N = %d;  // kök sayısı" % len(items))
    return "\n".join(lines) + "\n", len(blob)


def main():
    text, size = build()
    if "--check" in sys.argv:
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if cur != text:
            sys.exit("src/sys/ota_roots.h certs/ota-roots.pem ile uyuşmuyor: tools/make_ota_roots.py çalıştır")
        print("ota_roots.h güncel (%d bayt)" % size)
        return
    open(OUT, "w", encoding="utf-8").write(text)
    print("yazıldı: %s (%d bayt)" % (OUT, size))


if __name__ == "__main__":
    main()
