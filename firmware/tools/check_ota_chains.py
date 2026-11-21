#!/usr/bin/env python3
"""Güncelleme sunucularının gerçek sertifika zincirlerini kartın kökleriyle dener (CI'da çalışır).

Kartın yaptığını taklit eder (Arduino-ESP32 esp_crt_bundle.c + mbedTLS):
  1. Sunucunun gönderdiği zincirde yapraktan başlayıp ebeveyn aranır (konu = veren ve imza tutuyor).
  2. Ebeveyni bulunamayan en üstteki sertifikanın veren adı (DER, bayt bayt) certs/ota-roots.pem'deki
     bir kökün konu adıyla aynı olmalı ve imzası o kökün açık anahtarıyla doğrulanmalı.
Ayrıca sertifika ad denetimi (sunucu adı) yapılır. Biri tutmazsa çıkış kodu 1: yayın yapılmaz.

Kullanım: python3 tools/check_ota_chains.py api.github.com github.com <indirme sunucusu> ...
"""
import os
import re
import subprocess
import sys

from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa

HERE = os.path.dirname(os.path.abspath(__file__))
PEM = os.path.join(HERE, "..", "certs", "ota-roots.pem")


def sig_ok(child, pub):
    try:
        if isinstance(pub, rsa.RSAPublicKey):
            pub.verify(child.signature, child.tbs_certificate_bytes, padding.PKCS1v15(), child.signature_hash_algorithm)
        elif isinstance(pub, ec.EllipticCurvePublicKey):
            pub.verify(child.signature, child.tbs_certificate_bytes, ec.ECDSA(child.signature_hash_algorithm))
        else:
            return False
        return True
    except (InvalidSignature, ValueError, TypeError):
        return False


def fetch_chain(host):
    out = subprocess.run(["openssl", "s_client", "-connect", host + ":443", "-servername", host, "-showcerts"],
                         input=b"", capture_output=True, timeout=30).stdout
    blocks = re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", out, re.S)
    return [x509.load_pem_x509_certificate(b) for b in blocks]


def name_ok(leaf, host):
    try:
        names = leaf.extensions.get_extension_for_class(x509.SubjectAlternativeName).value.get_values_for_type(x509.DNSName)
    except x509.ExtensionNotFound:
        return False
    for n in names:
        if n == host or (n.startswith("*.") and host.count(".") == n.count(".") and host.endswith(n[1:])):
            return True
    return False


def check(host, roots):
    chain = fetch_chain(host)
    if not chain:
        print("%-40s ZİNCİR ALINAMADI" % host)
        return False
    path = [chain[0]]
    cur = chain[0]
    while True:
        if cur.issuer.public_bytes() == cur.subject.public_bytes() and sig_ok(cur, cur.public_key()):
            break  # kendinden imzalı: yukarısı yok
        parent = next((c for c in chain[1:] if c is not cur and c.subject.public_bytes() == cur.issuer.public_bytes()
                       and sig_ok(cur, c.public_key())), None)
        if parent is None or parent in path:
            break
        path.append(parent)
        cur = parent
    top = path[-1]
    root = next((r for r in roots if r.subject.public_bytes() == top.issuer.public_bytes()), None)
    ok = root is not None and sig_ok(top, root.public_key()) and name_ok(chain[0], host)
    names = " → ".join(c.subject.rfc4514_string().split(",")[0] for c in path)
    print("%-40s %s  %s  ⇒ kök: %s" % (host, "TAMAM" if ok else "HATA ", names,
                                        root.subject.rfc4514_string().split(",")[0] if root else top.issuer.rfc4514_string()))
    return ok


def check_ca_list(host, roots):
    """setCACert (güven listesi) kipi: mbedTLS önce güven listesinde ebeveyn arar. Yapraktan
    yukarı her sertifika için, veren adı bir kökün konu adıyla aynı ve imza o kökle doğruysa tamam
    (sunucunun gönderdiği çapraz imzalı üst sertifika kullanılmaz)."""
    chain = fetch_chain(host)
    if not chain:
        print("%-40s ZİNCİR ALINAMADI" % host)
        return False
    cur, seen, root = chain[0], [], None
    while cur is not None and cur not in seen:
        seen.append(cur)
        root = next((r for r in roots if r.subject.public_bytes() == cur.issuer.public_bytes() and sig_ok(cur, r.public_key())), None)
        if root is not None:
            break
        cur = next((c for c in chain[1:] if c.subject.public_bytes() == cur.issuer.public_bytes() and sig_ok(cur, c.public_key())), None)
    ok = root is not None and name_ok(chain[0], host)
    names = " → ".join(c.subject.rfc4514_string().split(",")[0] for c in seen)
    print("%-40s %s  %s  ⇒ kök: %s" % (host, "TAMAM" if ok else "HATA ", names, root.subject.rfc4514_string().split(",")[0] if root else "yok"))
    return ok


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--ca":  # --ca <pem> sunucular…: setCACert kipi
        roots = x509.load_pem_x509_certificates(open(sys.argv[2], "rb").read())
        results = [check_ca_list(h, roots) for h in sys.argv[3:] if h]
        if not results or not all(results):
            sys.exit("Kartın Google kökleri bu sunucuları doğrulayamıyor: certs/google-roots.pem")
        return
    roots = x509.load_pem_x509_certificates(open(PEM, "rb").read())
    hosts = [h for h in sys.argv[1:] if h]
    if not hosts:
        sys.exit("sunucu adı ver")
    results = [check(h, roots) for h in hosts]
    if not all(results):
        sys.exit("Kartın kökleri bu sunucuları doğrulayamıyor: certs/ota-roots.pem'e kök ekle, "
                 "tools/make_ota_roots.py çalıştır")


if __name__ == "__main__":
    main()
