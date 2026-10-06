"""Pure-Python RS256 and ES256 for the auth e2e tests (stdlib only, TEST keys only).

Slow and not constant-time on purpose: it signs a handful of tokens per test with keys that
live in tests/e2e/fixtures/. Never use it for anything real.

    RS256: RSASSA-PKCS1-v1_5 / SHA-256 (RFC 8017 §8.2)
    ES256: ECDSA P-256 / SHA-256 with RFC 6979 deterministic k, raw r||s JWS encoding (RFC 7518 §3.4)
"""
import base64
import hashlib
import hmac

# DER prefix of DigestInfo for SHA-256 (RFC 8017 §9.2 note 1).
SHA256_PREFIX = bytes.fromhex("3031300d060960864801650304020105000420")

# NIST P-256 (secp256r1) domain parameters.
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def b64u(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def b64u_decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def b64u_int(i, length=None):
    return b64u(i.to_bytes(length or (i.bit_length() + 7) // 8, "big"))


# -- RSA ---------------------------------------------------------------------
def rs256_sign(signing_input, n, d):
    k = (n.bit_length() + 7) // 8
    t = SHA256_PREFIX + hashlib.sha256(signing_input).digest()
    em = b"\x00\x01" + b"\xff" * (k - len(t) - 3) + b"\x00" + t
    return pow(int.from_bytes(em, "big"), d, n).to_bytes(k, "big")


def rs256_verify(signing_input, signature, n, e):
    """Independent of rs256_sign: recovers EM from the signature and compares it whole."""
    k = (n.bit_length() + 7) // 8
    if len(signature) != k:
        return False
    em = pow(int.from_bytes(signature, "big"), e, n).to_bytes(k, "big")
    t = SHA256_PREFIX + hashlib.sha256(signing_input).digest()
    return em == b"\x00\x01" + b"\xff" * (k - len(t) - 3) + b"\x00" + t


# -- P-256 -------------------------------------------------------------------
def _add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    (x1, y1), (x2, y2) = p1, p2
    if x1 == x2:
        if (y1 + y2) % P == 0:
            return None
        lam = (3 * x1 * x1 + A) * pow(2 * y1, -1, P) % P
    else:
        lam = (y2 - y1) * pow(x2 - x1, -1, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return x3, (lam * (x1 - x3) - y1) % P


def mul(k, point=G):
    result = None
    while k:
        if k & 1:
            result = _add(result, point)
        point = _add(point, point)
        k >>= 1
    return result


def public_point(d):
    return mul(d)


def _bits2int(data, qlen=256):
    v = int.from_bytes(data, "big")
    extra = len(data) * 8 - qlen
    return v >> extra if extra > 0 else v


def rfc6979_k(d, digest, hashfn=hashlib.sha256):
    """Deterministic nonce, RFC 6979 §3.2 (qlen = hlen = 256 for P-256/SHA-256)."""
    x = d.to_bytes(32, "big")
    h1 = (_bits2int(digest) % N).to_bytes(32, "big")
    v = b"\x01" * 32
    k = b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h1, hashfn).digest()
    v = hmac.new(k, v, hashfn).digest()
    k = hmac.new(k, v + b"\x01" + x + h1, hashfn).digest()
    v = hmac.new(k, v, hashfn).digest()
    while True:
        v = hmac.new(k, v, hashfn).digest()
        cand = _bits2int(v)
        if 1 <= cand < N:
            return cand
        k = hmac.new(k, v + b"\x00", hashfn).digest()
        v = hmac.new(k, v, hashfn).digest()


def ecdsa_sign(digest, d):
    """(r, s) over a 32-byte digest, low-s not enforced (Keycloak/Java does not either)."""
    z = _bits2int(digest) % N
    k = rfc6979_k(d, digest)
    while True:
        r = mul(k)[0] % N
        s = pow(k, -1, N) * (z + r * d) % N
        if r and s:
            return r, s
        k = (k % (N - 1)) + 1


def ecdsa_verify(digest, r, s, point):
    if not (1 <= r < N and 1 <= s < N):
        return False
    z = _bits2int(digest) % N
    w = pow(s, -1, N)
    pt = _add(mul(z * w % N), mul(r * w % N, point))
    return pt is not None and pt[0] % N == r


def es256_sign(signing_input, d):
    r, s = ecdsa_sign(hashlib.sha256(signing_input).digest(), d)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def es256_verify(signing_input, signature, point):
    if len(signature) != 64:
        return False
    return ecdsa_verify(hashlib.sha256(signing_input).digest(), int.from_bytes(signature[:32], "big"),
                        int.from_bytes(signature[32:], "big"), point)
