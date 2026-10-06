#!/usr/bin/env python3
"""Generates the e2e IdP signing keys. TEST ONLY: the output is committed, labelled, and must
never protect anything real. Stdlib only; run from anywhere:

    python3 tests/e2e/fixtures/gen_keys.py [--force]

Writes idp_rsa_2.json (a second RSA-2048 key, for rotation tests) and idp_ec.json (P-256, ES256).
idp_rsa.json (the primary RSA key) is older and is left alone.
"""
import json
import pathlib
import secrets
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from vbtest import jwscrypto  # noqa: E402

COMMENT = "TEST-ONLY key for tests/e2e (vbtest/mock_keycloak.py). Public in git; never use it for anything real."


def is_probable_prime(n, rounds=40):
    if n < 4:
        return n in (2, 3)
    if n % 2 == 0:
        return False
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(rounds):
        x = pow(secrets.randbelow(n - 3) + 2, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


def prime(bits):
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | (1 << (bits - 2)) | 1
        if is_probable_prime(c):
            return c


def rsa_key(bits=2048, e=65537):
    while True:
        p, q = prime(bits // 2), prime(bits // 2)
        n = p * q
        if p != q and n.bit_length() == bits and (p - 1) % e and (q - 1) % e:
            lam = (p - 1) * (q - 1)
            return n, e, pow(e, -1, lam)


def write(name, data, force):
    path = HERE / name
    if path.exists() and not force:
        print("keeping existing", path)
        return
    path.write_text(json.dumps(dict({"_comment": COMMENT}, **data), indent=2) + "\n")
    print("wrote", path)


if __name__ == "__main__":
    force = "--force" in sys.argv
    n, e, d = rsa_key()
    write("idp_rsa_2.json", {"kid": "e2e-key-2", "n": "%x" % n, "e": "%x" % e, "d": "%x" % d}, force)
    priv = secrets.randbelow(jwscrypto.N - 1) + 1
    x, y = jwscrypto.public_point(priv)
    write("idp_ec.json", {"kid": "e2e-ec-1", "crv": "P-256", "x": "%064x" % x, "y": "%064x" % y,
                          "d": "%064x" % priv}, force)
