#!/usr/bin/env python3
"""Verificador independente do HELTEC-SEED-V3.

Recalcula, num PC, exatamente o que o dispositivo mostrou no ecra. Se as 24
palavras aqui nao forem iguais as do ecra, NAO uses a seed.

Este ficheiro e deliberadamente autonomo: so usa a biblioteca padrao do Python
(hashlib, hmac, unicodedata). Nao ha 'pip install', nao ha rede, e o secp256k1 e
o BIP32 estao implementados aqui de raiz. O objetivo e ser uma segunda
implementacao *independente* do firmware em C -- se as duas concordarem, o
resultado esta certo. Foi exatamente essa segunda opiniao que faltou a Coldcard
Mk3 em julho de 2026.

USO
---
  # reproduzir uma cerimonia a partir do que anotaste do ecra
  python verify.py --dice 4152631... --chip <64 hex> --commit <hex>

  # so a partir da entropia final, se preferires
  python verify.py --entropy <64 hex>

  # correr os vetores oficiais BIP39/BIP32/BIP84/BIP86
  python verify.py --self-test

O --commit e o valor que o dispositivo mostrou ANTES de teres lancado o primeiro
dado. Sem ele, este script continua a reproduzir as 24 palavras, mas deixa de
provar a unica coisa que interessa: que o chip nao escolheu a metade dele depois
de ver os teus lancamentos. Ver commit_of() mais abaixo.

O unico ficheiro externo necessario e english.txt (wordlist BIP39 oficial),
que tem de estar ao lado deste script.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import sys
import unicodedata

# ---------------------------------------------------------------------------
# RIPEMD-160
# ---------------------------------------------------------------------------
# O OpenSSL 3.x deixou de expor o ripemd160 em muitas distribuicoes, por isso
# nao se pode depender do hashlib. Segue a implementacao pura, para o script
# correr em qualquer maquina.

_RMD_R = [
    *range(16),
    7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
    1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
    4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13,
]
_RMD_RP = [
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
    6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
    8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
    12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11,
]
_RMD_S = [
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
    7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
    11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
    9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6,
]
_RMD_SP = [
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
    9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
    15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
    8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11,
]
_RMD_K = [0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E]
_RMD_KP = [0x50A28BE6, 0x5C4DD124, 0x6D703EF3, 0x7A6D76E9, 0x00000000]


def _rol(x: int, n: int) -> int:
    x &= 0xFFFFFFFF
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def _rmd_f(j: int, x: int, y: int, z: int) -> int:
    if j < 16:
        return x ^ y ^ z
    if j < 32:
        return (x & y) | (~x & z)
    if j < 48:
        return (x | ~y) ^ z
    if j < 64:
        return (x & z) | (y & ~z)
    return x ^ (y | ~z)


def ripemd160(data: bytes) -> bytes:
    """RIPEMD-160 puro, sem depender do OpenSSL."""
    h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0]
    msg = bytearray(data)
    bit_len = (len(data) * 8) & 0xFFFFFFFFFFFFFFFF
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0x00)
    msg += bit_len.to_bytes(8, "little")

    for off in range(0, len(msg), 64):
        blk = msg[off:off + 64]
        x = [int.from_bytes(blk[i * 4:i * 4 + 4], "little") for i in range(16)]
        a, b, c, d, e = h
        ap, bp, cp, dp, ep = h
        for j in range(80):
            rnd = j // 16
            t = _rol((a + _rmd_f(j, b, c, d) + x[_RMD_R[j]] + _RMD_K[rnd]) & 0xFFFFFFFF,
                     _RMD_S[j])
            t = (t + e) & 0xFFFFFFFF
            a, e, d, c, b = e, d, _rol(c, 10), b, t
            tp = _rol((ap + _rmd_f(79 - j, bp, cp, dp) + x[_RMD_RP[j]] + _RMD_KP[rnd]) & 0xFFFFFFFF,
                      _RMD_SP[j])
            tp = (tp + ep) & 0xFFFFFFFF
            ap, ep, dp, cp, bp = ep, dp, _rol(cp, 10), bp, tp
        t = (h[1] + c + dp) & 0xFFFFFFFF
        h[1] = (h[2] + d + ep) & 0xFFFFFFFF
        h[2] = (h[3] + e + ap) & 0xFFFFFFFF
        h[3] = (h[4] + a + bp) & 0xFFFFFFFF
        h[4] = (h[0] + b + cp) & 0xFFFFFFFF
        h[0] = t
    return b"".join(v.to_bytes(4, "little") for v in h)


def sha256(b: bytes) -> bytes:
    return hashlib.sha256(b).digest()


def hash160(b: bytes) -> bytes:
    return ripemd160(sha256(b))


def double_sha256(b: bytes) -> bytes:
    return sha256(sha256(b))


# ---------------------------------------------------------------------------
# Base58Check
# ---------------------------------------------------------------------------

_B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def b58encode_check(payload: bytes) -> str:
    data = payload + double_sha256(payload)[:4]
    n = int.from_bytes(data, "big")
    out = ""
    while n > 0:
        n, r = divmod(n, 58)
        out = _B58[r] + out
    return "1" * (len(data) - len(data.lstrip(b"\x00"))) + out


# ---------------------------------------------------------------------------
# bech32 / bech32m (BIP-173 e BIP-350), para conferir enderecos com o Sparrow
# ---------------------------------------------------------------------------

_BECH32 = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
_BECH32M_CONST = 0x2BC830A3


def _bech32_polymod(values):
    gen = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for v in values:
        top = chk >> 25
        chk = ((chk & 0x1FFFFFF) << 5) ^ v
        for i in range(5):
            chk ^= gen[i] if ((top >> i) & 1) else 0
    return chk


def _bech32_hrp_expand(hrp: str):
    return [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]


def _convertbits(data, frombits, tobits, pad=True):
    acc = bits = 0
    ret = []
    maxv = (1 << tobits) - 1
    for value in data:
        acc = (acc << frombits) | value
        bits += frombits
        while bits >= tobits:
            bits -= tobits
            ret.append((acc >> bits) & maxv)
    if pad and bits:
        ret.append((acc << (tobits - bits)) & maxv)
    return ret


def segwit_address(hrp: str, witver: int, witprog: bytes) -> str:
    data = [witver] + _convertbits(witprog, 8, 5)
    const = _BECH32M_CONST if witver else 1
    polymod = _bech32_polymod(_bech32_hrp_expand(hrp) + data + [0, 0, 0, 0, 0, 0]) ^ const
    checksum = [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]
    return hrp + "1" + "".join(_BECH32[d] for d in data + checksum)


# ---------------------------------------------------------------------------
# secp256k1 (aritmetica afim -- lenta mas curta e auditavel)
# ---------------------------------------------------------------------------

P = 2**256 - 2**32 - 977
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8


def _pt_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        lam = (3 * x1 * x1) * pow(2 * y1, P - 2, P) % P
    else:
        lam = (y2 - y1) * pow(x2 - x1, P - 2, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return (x3, (lam * (x1 - x3) - y1) % P)


def _pt_mul(k: int, point=None):
    point = point or (GX, GY)
    result = None
    while k:
        if k & 1:
            result = _pt_add(result, point)
        point = _pt_add(point, point)
        k >>= 1
    return result


def ser_p(point) -> bytes:
    """Ponto comprimido SEC1, 33 bytes."""
    x, y = point
    return bytes([2 + (y & 1)]) + x.to_bytes(32, "big")


def pubkey(privkey: bytes) -> bytes:
    return ser_p(_pt_mul(int.from_bytes(privkey, "big")))


# ---------------------------------------------------------------------------
# BIP-32
# ---------------------------------------------------------------------------

VERSIONS = {
    "xprv": 0x0488ADE4, "xpub": 0x0488B21E,   # BIP44 / generico
    "yprv": 0x049D7878, "ypub": 0x049D7CB2,   # BIP49  (SLIP-0132)
    "zprv": 0x04B2430C, "zpub": 0x04B24746,   # BIP84  (SLIP-0132)
    "tprv": 0x04358394, "tpub": 0x043587CF,   # testnet
}

HARDENED = 0x80000000


class HDKey:
    __slots__ = ("priv", "pub", "chain", "depth", "parent_fp", "index")

    def __init__(self, priv, pub, chain, depth=0, parent_fp=b"\x00" * 4, index=0):
        self.priv = priv
        self.pub = pub if pub is not None else pubkey(priv)
        self.chain = chain
        self.depth = depth
        self.parent_fp = parent_fp
        self.index = index

    @classmethod
    def from_seed(cls, seed: bytes) -> "HDKey":
        i = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
        k = int.from_bytes(i[:32], "big")
        if k == 0 or k >= N:
            raise ValueError("seed invalida para BIP32 (probabilidade ~2^-127)")
        return cls(i[:32], None, i[32:])

    @property
    def fingerprint(self) -> bytes:
        return hash160(self.pub)[:4]

    def child(self, index: int) -> "HDKey":
        if index & HARDENED:
            if self.priv is None:
                raise ValueError("derivacao endurecida exige chave privada")
            data = b"\x00" + self.priv + index.to_bytes(4, "big")
        else:
            data = self.pub + index.to_bytes(4, "big")
        i = hmac.new(self.chain, data, hashlib.sha512).digest()
        il = int.from_bytes(i[:32], "big")
        if il >= N:
            raise ValueError("IL >= n; avancar para o indice seguinte")
        if self.priv is not None:
            k = (il + int.from_bytes(self.priv, "big")) % N
            if k == 0:
                raise ValueError("chave filha nula; avancar para o indice seguinte")
            return HDKey(k.to_bytes(32, "big"), None, i[32:],
                         self.depth + 1, self.fingerprint, index)
        point = _pt_add(_pt_mul(il), _decompress(self.pub))
        if point is None:
            raise ValueError("ponto no infinito; avancar para o indice seguinte")
        return HDKey(None, ser_p(point), i[32:], self.depth + 1, self.fingerprint, index)

    def derive(self, path: str) -> "HDKey":
        node = self
        for part in path.strip().split("/"):
            if part in ("m", "M", ""):
                continue
            hard = part[-1] in ("'", "h", "H")
            node = node.child(int(part.rstrip("'hH")) + (HARDENED if hard else 0))
        return node

    def serialize(self, kind: str) -> str:
        ver = VERSIONS[kind]
        key = (b"\x00" + self.priv) if kind.endswith("prv") else self.pub
        payload = (ver.to_bytes(4, "big") + bytes([self.depth]) + self.parent_fp
                   + self.index.to_bytes(4, "big") + self.chain + key)
        return b58encode_check(payload)

    def address_p2wpkh(self, hrp: str = "bc") -> str:
        return segwit_address(hrp, 0, hash160(self.pub))


def _decompress(sec: bytes):
    x = int.from_bytes(sec[1:], "big")
    y = pow((x * x * x + 7) % P, (P + 1) // 4, P)
    if (y & 1) != (sec[0] & 1):
        y = P - y
    return (x, y)


# ---------------------------------------------------------------------------
# BIP-39
# ---------------------------------------------------------------------------

def load_wordlist(path: str | None = None) -> list[str]:
    path = path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "english.txt")
    with open(path, "r", encoding="utf-8") as fh:
        words = fh.read().split()
    if len(words) != 2048:
        raise SystemExit(f"english.txt invalida: {len(words)} palavras, esperadas 2048")
    return words


def entropy_to_mnemonic(entropy: bytes, wordlist: list[str]) -> str:
    if len(entropy) not in (16, 20, 24, 28, 32):
        raise ValueError(f"entropia tem de ter 16..32 bytes, tem {len(entropy)}")
    bits = "".join(f"{b:08b}" for b in entropy)
    checksum = f"{sha256(entropy)[0]:08b}"[:len(entropy) * 8 // 32]
    bits += checksum
    return " ".join(wordlist[int(bits[i:i + 11], 2)] for i in range(0, len(bits), 11))


def mnemonic_to_entropy(mnemonic: str, wordlist: list[str]) -> bytes:
    words = mnemonic.split()
    if len(words) % 3 or not 12 <= len(words) <= 24:
        raise ValueError(f"numero de palavras invalido: {len(words)}")
    idx = {w: i for i, w in enumerate(wordlist)}
    try:
        bits = "".join(f"{idx[w]:011b}" for w in words)
    except KeyError as exc:
        raise ValueError(f"palavra fora da wordlist BIP39: {exc.args[0]}") from None
    ent_len = len(bits) * 32 // 33 // 8
    entropy = int(bits[:ent_len * 8], 2).to_bytes(ent_len, "big")
    expected = f"{sha256(entropy)[0]:08b}"[:ent_len * 8 // 32]
    if bits[ent_len * 8:] != expected:
        raise ValueError("checksum BIP39 invalido")
    return entropy


def mnemonic_to_seed(mnemonic: str, passphrase: str = "") -> bytes:
    m = unicodedata.normalize("NFKD", mnemonic).encode("utf-8")
    s = unicodedata.normalize("NFKD", "mnemonic" + passphrase).encode("utf-8")
    return hashlib.pbkdf2_hmac("sha512", m, s, 2048, 64)


# ---------------------------------------------------------------------------
# Construcao de entropia do HELTEC-SEED-V3
# ---------------------------------------------------------------------------
# Tem de bater byte a byte com core/entropy.c. Qualquer divergencia aqui e um
# bug -- e o objetivo deste ficheiro e precisamente apanha-la.

def dice_to_entropy(rolls: str) -> bytes:
    """SHA256 dos lancamentos em ASCII. Convencao Coldcard/SeedSigner."""
    clean = "".join(rolls.split())
    if not clean:
        raise ValueError("nenhum lancamento fornecido")
    bad = sorted(set(clean) - set("123456"))
    if bad:
        raise ValueError(f"caracteres invalidos nos dados: {''.join(bad)} (so 1-6)")
    if len(clean) < 100:
        raise ValueError(
            f"apenas {len(clean)} lancamentos; sao precisos 100 "
            f"(log2(6)*100 = 258.5 bits). O firmware recusa abaixo disso.")
    return sha256(clean.encode("ascii"))


def coin_to_entropy(flips: str) -> bytes:
    """SHA256 dos lancamentos de moeda em ASCII '0'/'1'."""
    clean = "".join(flips.split())
    bad = sorted(set(clean) - set("01"))
    if bad:
        raise ValueError(f"caracteres invalidos na moeda: {''.join(bad)} (so 0-1)")
    if len(clean) < 256:
        raise ValueError(f"apenas {len(clean)} lancamentos; sao precisos 256")
    return sha256(clean.encode("ascii"))


def combine(e_fisica: bytes, e_chip: bytes) -> bytes:
    """seed256 = E_fisica XOR E_chip."""
    if len(e_fisica) != 32 or len(e_chip) != 32:
        raise ValueError("ambas as metades tem de ter 32 bytes")
    return bytes(a ^ b for a, b in zip(e_fisica, e_chip))


def commit_of(e_chip: bytes) -> bytes:
    """C = SHA256(E_chip) -- o compromisso mostrado antes dos lancamentos.

    E ISTO QUE FAZ A VERIFICACAO VALER ALGUMA COISA
    -----------------------------------------------
    Reproduzir as 24 palavras a partir de E_fisica e E_chip prova que a conta
    esta certa. Nao prova que o chip jogou limpo. Se o dispositivo so escolhesse
    a metade dele DEPOIS de ver os teus lancamentos, um TRNG comprometido fazia

        E_chip = E_fisica XOR seed_que_o_atacante_quer

    e este script imprimia exatamente as palavras que o ecra mostrou, com o
    fingerprint certo e o endereco certo. Tudo consistente, tudo do atacante.

    O compromisso fecha isso: o valor foi anotado por ti antes de o primeiro
    dado rolar, logo o E_chip revelado no fim nao pode ter dependido dos teus
    lancamentos -- mudar de ideias exigiria uma segunda preimagem de SHA256.

    E so SHA256 de 32 bytes crus: da para conferir com um sha256sum qualquer,
    sem confiar neste ficheiro.
    """
    if len(e_chip) != 32:
        raise ValueError("E_chip tem de ter 32 bytes")
    return sha256(e_chip)


def check_commit(e_chip: bytes, commit_hex: str) -> None:
    """Aborta se o E_chip revelado nao corresponder ao compromisso anotado.

    Aceita um prefixo: o ecra mostra os 64 hex, mas quem so copiou os primeiros
    32 continua com 128 bits a prender o chip, que chega e sobra.
    """
    want = "".join(commit_hex.split()).lower()
    if any(c not in "0123456789abcdef" for c in want):
        raise ValueError("--commit tem de ser hexadecimal")
    if len(want) < 32:
        raise ValueError(
            f"--commit tem so {len(want)} hex; sao precisos pelo menos 32 "
            f"(128 bits) para prender o chip a serio")
    got = commit_of(e_chip).hex()
    if not got.startswith(want):
        raise ValueError(
            "COMPROMISSO NAO BATE CERTO -- PARA JA.\n"
            f"  anotaste antes dos lancamentos: {want}\n"
            f"  SHA256 do E_chip revelado:      {got[:len(want)]}\n"
            "\n"
            "  O dispositivo mudou a metade dele depois de ver os teus dados.\n"
            "  E exatamente o ataque que o compromisso existe para apanhar.\n"
            "  NAO USES ESTA SEED. Nao mandes moedas para estes enderecos.")


PATHS = [
    ("BIP84 nativo segwit", "m/84'/0'/0'", "zpub", "bc1q..."),
    ("BIP86 taproot",       "m/86'/0'/0'", "xpub", "bc1p..."),
    ("BIP49 nested segwit", "m/49'/0'/0'", "ypub", "3..."),
    ("BIP44 legacy",        "m/44'/0'/0'", "xpub", "1..."),
]


def report(entropy: bytes, passphrase: str, wordlist: list[str],
           e_fisica: bytes | None = None, e_chip: bytes | None = None,
           commit_hex: str | None = None) -> None:
    mnemonic = entropy_to_mnemonic(entropy, wordlist)
    seed = mnemonic_to_seed(mnemonic, passphrase)
    master = HDKey.from_seed(seed)

    line = "=" * 72
    print(line)
    print("HELTEC-SEED-V3 -- verificacao independente")
    print(line)
    if e_fisica is not None:
        if commit_hex is not None:
            n = len("".join(commit_hex.split()))
            print(f"compromisso       {commit_of(e_chip).hex()}")
            print(f"                  ^ CONFERIDO contra os {n} hex que anotaste")
            print("                    antes de lancares o primeiro dado")
        print(f"E_fisica (dados)  {e_fisica.hex()}")
        print(f"E_chip            {e_chip.hex()}")
        print("                  -------- XOR --------")
    print(f"entropia final    {entropy.hex()}")
    print(f"passphrase        {'(vazia)' if not passphrase else repr(passphrase)}")
    print()
    print(f"{len(mnemonic.split())} PALAVRAS -- compara uma a uma com o ecra:")
    words = mnemonic.split()
    for i in range(0, len(words), 4):
        print("   " + "  ".join(f"{i+j+1:2d}.{w:<9s}" for j, w in enumerate(words[i:i + 4])))
    print()
    print(f"fingerprint mestre   {master.fingerprint.hex()}   <-- confere com o Sparrow")
    print()
    for label, path, kind, hint in PATHS:
        acct = master.derive(path)
        first = acct.derive("0/0")
        print(f"  {label}")
        print(f"    caminho        {path}")
        print(f"    {kind:<14s} {acct.serialize(kind)}")
        if kind in ("zpub", "xpub") and "84" in path:
            print(f"    1o endereco    {first.address_p2wpkh()}")
        elif "86" in path:
            print(f"    (taproot: endereco {hint} nao derivado aqui)")
        print()
    print(line)
    print("Se alguma palavra, o fingerprint ou o 1o endereco nao baterem")
    print("certo com o dispositivo -- PARA. Nao uses a seed.")
    if e_fisica is not None and commit_hex is None:
        print(line)
        print("AVISO: nao foi conferido nenhum compromisso.")
        print("As 24 palavras acima reproduzem o que o ecra mostrou, mas isso")
        print("so prova que a aritmetica esta certa. Um chip comprometido que")
        print("tivesse visto os teus lancamentos antes de escolher o E_chip")
        print("podia ter fixado a seed inteira, e este relatorio sairia igual.")
        print("Na proxima cerimonia, anota o compromisso ANTES de lancares os")
        print("dados. E o unico passo que prende o aparelho a metade dele.")
    print(line)


# ---------------------------------------------------------------------------
# Auto-teste com vetores oficiais
# ---------------------------------------------------------------------------

def self_test(wordlist: list[str]) -> int:
    fails = []

    def check(name, got, want):
        if got != want:
            fails.append(f"{name}\n     obtido:   {got}\n     esperado: {want}")

    # --- RIPEMD-160 (vetores do artigo original) ---
    check("ripemd160('')", ripemd160(b"").hex(),
          "9c1185a5c5e9fc54612808977ee8f548b2258d31")
    check("ripemd160('abc')", ripemd160(b"abc").hex(),
          "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc")
    check("ripemd160('a'*1000000)", ripemd160(b"a" * 1000000).hex(),
          "52783243c1697bdbe16d37f97f68f08325dc1528")

    # --- BIP-39: os 24 vetores oficiais da Trezor, passphrase "TREZOR" ---
    # Cada vetor e [entropia, mnemonica, seed, xprv raiz], por isso testa de uma
    # vez o BIP39 e a raiz BIP32.
    here = os.path.dirname(os.path.abspath(__file__))
    vec_path = os.path.join(here, "..", "test", "vectors", "bip39_vectors.json")
    n_bip39 = 0
    if os.path.exists(vec_path):
        import json
        with open(vec_path, encoding="utf-8") as fh:
            vectors = json.load(fh)["english"]
        for ent_hex, want_mn, want_seed, want_xprv in vectors:
            ent = bytes.fromhex(ent_hex)
            got_mn = entropy_to_mnemonic(ent, wordlist)
            check(f"BIP39 mnemonic {ent_hex[:12]}..", got_mn, want_mn)
            check(f"BIP39 roundtrip {ent_hex[:12]}..",
                  mnemonic_to_entropy(got_mn, wordlist).hex(), ent_hex)
            seed = mnemonic_to_seed(got_mn, "TREZOR")
            check(f"BIP39 seed {ent_hex[:12]}..", seed.hex(), want_seed)
            check(f"BIP32 raiz {ent_hex[:12]}..",
                  HDKey.from_seed(seed).serialize("xprv"), want_xprv)
            n_bip39 += 4
    else:
        fails.append(f"vetores oficiais BIP39 em falta: {vec_path}")

    # --- BIP-32: os 5 vetores oficiais do BIP, extraidos do proprio spec ---
    # Inclui o vetor 4, que cobre o caso de "chave filha invalida, avancar indice",
    # e o vetor 3, que apanha implementacoes que nao fazem zero-padding do IL.
    bip32_path = os.path.join(here, "..", "test", "vectors", "bip32_vectors.json")
    n_bip32 = 0
    if os.path.exists(bip32_path):
        import json as _json
        with open(bip32_path, encoding="utf-8") as fh:
            bip32_vectors = _json.load(fh)
        cache: dict[str, HDKey] = {}
        for v in bip32_vectors:
            root = cache.setdefault(v["seed"], HDKey.from_seed(bytes.fromhex(v["seed"])))
            node = root.derive(v["chain"])
            tag = f'BIP32 {v["seed"][:8]}.. {v["chain"]}'
            check(f"{tag} xpub", node.serialize("xpub"), v["xpub"])
            check(f"{tag} xprv", node.serialize("xprv"), v["xprv"])
            n_bip32 += 2
    else:
        fails.append(f"vetores oficiais BIP32 em falta: {bip32_path}")

    # --- BIP-84 (vetor oficial do BIP) ---
    mn84 = ("abandon abandon abandon abandon abandon abandon abandon abandon "
            "abandon abandon abandon about")
    m84 = HDKey.from_seed(mnemonic_to_seed(mn84, ""))
    check("BIP84 zpub conta 0", m84.derive("m/84'/0'/0'").serialize("zpub"),
          "zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVC"
          "ToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs")
    check("BIP84 1o endereco", m84.derive("m/84'/0'/0'/0/0").address_p2wpkh(),
          "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu")
    check("BIP84 2o endereco", m84.derive("m/84'/0'/0'/0/1").address_p2wpkh(),
          "bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g")

    # --- construcao HELTEC-SEED-V3 ---
    e_fis = dice_to_entropy("1" * 100)
    check("E_fisica(100x'1')", e_fis.hex(),
          sha256(b"1" * 100).hex())
    check("XOR identidade", combine(e_fis, bytes(32)).hex(), e_fis.hex())
    check("XOR involucao", combine(combine(e_fis, b"\xa5" * 32), b"\xa5" * 32).hex(),
          e_fis.hex())

    # a construcao tem de recusar entropia fraca em vez de degradar
    for bad, why in [("1" * 99, "menos de 100 lancamentos"),
                     ("1" * 99 + "7", "digito invalido"),
                     ("", "vazio")]:
        try:
            dice_to_entropy(bad)
            fails.append(f"dice_to_entropy aceitou entrada invalida ({why})")
        except ValueError:
            pass

    # --- compromisso do chip ---
    demo = bytes.fromhex("a5" * 32)
    good = commit_of(demo).hex()
    check("compromisso = SHA256(E_chip)", good, sha256(demo).hex())

    for label, arg in [("inteiro", good), ("prefixo de 32 hex", good[:32])]:
        try:
            check_commit(demo, arg)
        except ValueError:
            fails.append(f"check_commit recusou um compromisso valido ({label})")

    flipped = ("0" if good[0] != "0" else "1") + good[1:]
    for bad, why in [(good[:31], "prefixo curto de mais (menos de 128 bits)"),
                     (flipped, "primeiro digito trocado"),
                     ("zz" + good[2:], "nao e hexadecimal")]:
        try:
            check_commit(demo, bad)
            fails.append(f"check_commit aceitou um compromisso invalido ({why})")
        except ValueError:
            pass

    # --- o ataque que o compromisso existe para apanhar ---
    # Um chip honesto fecha o E_chip e compromete-se com ele antes de ver nada:
    honesto = sha256(b"E_chip de um chip honesto")
    compromisso = commit_of(honesto).hex()

    # Um chip malicioso espera pelos lancamentos e escolhe a metade dele para
    # fixar a seed que quiser. Sem compromisso, a cerimonia sai toda coerente.
    rolls = "".join("123456"[i % 6] for i in range(100))
    fis = dice_to_entropy(rolls)
    alvo = sha256(b"a seed que o atacante quer")
    malicioso = combine(fis, alvo)
    check("ataque adaptativo: o XOR entrega mesmo a seed do atacante",
          combine(fis, malicioso).hex(), alvo.hex())

    # Com compromisso, e apanhado -- que e a razao de o --commit existir.
    try:
        check_commit(malicioso, compromisso)
        fails.append("check_commit deixou passar o E_chip trocado depois dos "
                     "lancamentos -- e exatamente o ataque adaptativo")
    except ValueError:
        pass

    total = 3 + n_bip39 + n_bip32 + 3 + 6 + 6 + 2
    if fails:
        print(f"FALHOU -- {len(fails)} de {total} verificacoes\n")
        for f in fails:
            print(f"  [X] {f}")
        return 1
    print(f"OK -- todas as {total} verificacoes passaram")
    print(f"     ripemd160 (3)")
    print(f"     BIP39 + raiz BIP32 ({n_bip39}, os 24 vetores oficiais da Trezor)")
    print(f"     BIP32 ({n_bip32}, os 5 vetores oficiais do BIP, incl. v3 e v4)")
    print(f"     BIP84 zpub + enderecos (3)")
    print(f"     construcao de entropia XOR e recusa de entrada fraca (6)")
    print(f"     compromisso do chip: aceita o valido, recusa o resto (6)")
    print(f"     ataque adaptativo do E_chip apanhado pelo compromisso (2)")
    return 0


# ---------------------------------------------------------------------------
# Modo passo a passo
# ---------------------------------------------------------------------------
# Escrever "--dice 415263... --chip <64 hex> --commit <64 hex>" numa linha de
# comandos e' pedir de mais a quem acabou de copiar tudo a mao para uma folha.
# E quem nao consegue verificar, nao verifica -- e entao o projeto inteiro nao
# vale nada, porque a verificacao e' a unica coisa que ele tem.
#
# Por isso: sem argumentos, o script pergunta. Uma linha de cada vez, na mesma
# forma em que o ecra a mostrou (quatro grupos de quatro), com o erro apanhado
# na linha em que foi escrito e nao no fim.

LARGURA = 64


def _titulo(txt: str) -> None:
    print()
    print("=" * LARGURA)
    print(f"  {txt}")
    print("=" * LARGURA)


def _passo(n: int, total: int, txt: str) -> None:
    print()
    print("-" * LARGURA)
    print(f"  PASSO {n} DE {total}   {txt}")
    print("-" * LARGURA)


def _ler(prompt: str) -> str:
    """input() que nunca rebenta: Ctrl-C e fim-de-ficheiro saem com jeito."""
    try:
        return input(prompt)
    except (EOFError, KeyboardInterrupt):
        print("\n\nInterrompido. Nao foi verificado nada.")
        raise SystemExit(1) from None


def _explica_hex(s: str) -> str | None:
    """Devolve uma explicacao se a linha nao servir, ou None se estiver boa.

    As confusoes de leitura de letra manuscrita tem mensagem propria: quem
    escreveu um 'o' quis quase de certeza dizer zero, mas quem corrige isso
    tem de ser a pessoa, nao o programa. Um verificador que adivinha deixa de
    ser um verificador.
    """
    trocas = {"o": "0 (zero)", "O": "0 (zero)", "l": "1 (um)", "I": "1 (um)",
              "i": "1 (um)", "S": "5 (cinco)", "s": "5 (cinco)", "z": "2 (dois)"}
    for c in s:
        if c in trocas:
            return (f"Escreveste a letra '{c}'. Em hexadecimal so existem os "
                    f"algarismos 0 a 9 e as letras a, b, c, d, e, f.\n"
                    f"      Se calhar querias dizer {trocas[c]}.")
    maus = sorted({c for c in s if c not in "0123456789abcdef"})
    if maus:
        return (f"Estes caracteres nao existem em hexadecimal: {' '.join(maus)}\n"
                f"      So podes usar 0 a 9 e as letras a, b, c, d, e, f.")
    return None


def _ler_hex32(nome: str) -> bytes:
    """As quatro linhas de um ecra de 64 hex, uma de cada vez."""
    print(f"  No aparelho, o ecra {nome} tinha quatro linhas numeradas,")
    print("  cada uma com quatro grupos de quatro. Escreve-as aqui, uma de")
    print("  cada vez. Os espacos nao interessam.")
    print()
    partes = []
    for i in range(4):
        while True:
            bruto = _ler(f"      linha {i + 1} > ").strip().lower()
            s = "".join(bruto.split())
            # quem copiar tambem o numero da linha ("1 a41f ...") nao e' punido
            if s.startswith(str(i + 1)) and len(s) == 17:
                s = s[1:]
            if not s:
                print("      (nao escreveste nada)")
                continue
            problema = _explica_hex(s)
            if problema:
                print(f"      {problema}")
                continue
            if len(s) != 16:
                print(f"      Essa linha tem {len(s)} caracteres e devia ter 16")
                print("      (quatro grupos de quatro). Conta outra vez e repete.")
                continue
            partes.append(s)
            break
    return bytes.fromhex("".join(partes))


def _ler_dados() -> str:
    print("  Agora os 100 lancamentos, dez de cada vez.")
    print("  Escreve so os numeros que sairam, sem espacos ou com eles.")
    print()
    todos = ""
    for i in range(10):
        while True:
            s = "".join(_ler(f"      lancamentos {i * 10 + 1:>3} a "
                             f"{i * 10 + 10:>3} > ").split())
            if not s:
                print("      (nao escreveste nada)")
                continue
            maus = sorted({c for c in s if c not in "123456"})
            if maus:
                print(f"      Um dado so pode dar 1, 2, 3, 4, 5 ou 6.")
                print(f"      Escreveste tambem: {' '.join(maus)}")
                continue
            if len(s) != 10:
                print(f"      Escreveste {len(s)} numeros; faltam ou sobram "
                      f"{abs(10 - len(s))}. Repete esta linha.")
                continue
            todos += s
            break
    return todos


def _escolher(pergunta: str, opcoes: list[str]) -> int:
    print(f"  {pergunta}")
    print()
    for i, o in enumerate(opcoes):
        print(f"      {i + 1}. {o}")
    print()
    while True:
        s = _ler(f"      escreve 1 a {len(opcoes)} > ").strip()
        if s.isdigit() and 1 <= int(s) <= len(opcoes):
            return int(s) - 1
        print(f"      Escreve so um numero de 1 a {len(opcoes)}.")


def _parar(motivo: str, detalhe: str) -> int:
    print()
    print("#" * LARGURA)
    print("#" + " " * (LARGURA - 2) + "#")
    print("#" + "  PARA. NAO USES ESTA SEED.".ljust(LARGURA - 2) + "#")
    print("#" + " " * (LARGURA - 2) + "#")
    print("#" * LARGURA)
    print()
    print(f"  {motivo}")
    print()
    for linha in detalhe.splitlines():
        print(f"  {linha}")
    print()
    print("  Nao mandes moedas para nenhum endereco desta cerimonia.")
    print()
    return 1


def interativo(wordlist: list[str]) -> int:
    _titulo("HELTEC-SEED-V3 - conferir a seed")
    print()
    print("  Isto compara o que ficou no papel com o que o aparelho mostrou.")
    print("  Nao e preciso perceber nada de informatica: eu pergunto, tu")
    print("  escreves o que esta na folha, e no fim digo se bate certo.")
    print()
    print("  Vais precisar da folha da cerimonia.")
    _ler("  Carrega ENTER quando a tiveres a frente. ")

    # --- 1. compromisso ---------------------------------------------------
    _passo(1, 4, "O COMPROMISSO")
    print()
    print("  E o primeiro valor que anotaste, ANTES de lancares os dados.")
    print("  E ele que prova que o aparelho nao escolheu a seed depois de")
    print("  ver os teus lancamentos.")
    print()
    tem_commit = _escolher("Anotaste o compromisso?",
                           ["Sim, esta aqui na folha",
                            "Nao anotei"]) == 0
    commit = None
    if tem_commit:
        print()
        commit = _ler_hex32("COMPROMISSO").hex()

    # --- 2. lancamentos ---------------------------------------------------
    _passo(2, 4, "OS LANCAMENTOS")
    print()
    dados = _ler_dados()
    e_fis = dice_to_entropy(dados)

    # --- 3. E_chip --------------------------------------------------------
    _passo(3, 4, "O VALOR E_CHIP")
    print()
    e_chip = _ler_hex32("E_CHIP")

    if commit is not None:
        try:
            check_commit(e_chip, commit)
        except ValueError as exc:
            return _parar(
                "O COMPROMISSO NAO BATE CERTO.",
                f"{exc}\n\n"
                "Confere primeiro se copiaste bem as quatro linhas dos dois\n"
                "ecras -- e o mais provavel. Se copiaste bem, entao o aparelho\n"
                "mudou a metade dele depois de ver os teus lancamentos, que e\n"
                "exatamente aquilo que este passo existe para apanhar.\n"
                "Nesse caso, deita a placa fora.")
        print()
        print("  >> O compromisso bate certo. O aparelho jogou limpo.")

    # --- 4. passphrase ----------------------------------------------------
    _passo(4, 4, "A PASSPHRASE")
    print()
    escolha = _escolher("Que passphrase usaste?",
                        ["Nenhuma",
                         "As 24 palavras que o aparelho gerou",
                         "Uma que escrevi eu"])
    passphrase = ""
    if escolha == 1:
        print()
        print("  Escreve as 24 palavras da PASSPHRASE, separadas por espacos.")
        print("  (nao sao as 24 da seed -- sao as outras)")
        print()
        while True:
            p = " ".join(_ler("      > ").split()).lower()
            if len(p.split()) == 24:
                passphrase = p
                break
            print(f"      Contei {len(p.split())} palavras; tem de haver 24.")
    elif escolha == 2:
        print()
        print("  Escreve a passphrase exatamente como a introduziste,")
        print("  com as maiusculas e minusculas certas.")
        print()
        passphrase = _ler("      > ")

    entropy = combine(e_fis, e_chip)
    report(entropy, passphrase, wordlist, e_fis, e_chip, commit)

    print()
    print("  O QUE FAZER AGORA")
    print("  -----------------")
    print("  1. Compara as 24 palavras acima, uma a uma, com as do papel.")
    print("  2. Abre o Sparrow SEM INTERNET e escreve la as 24 palavras")
    print("     e a passphrase.")
    print("  3. Confirma que o fingerprint e o primeiro endereco sao os")
    print("     mesmos que estao aqui em cima.")
    print()
    print("  Se as tres coisas baterem certo, a seed esta boa.")
    print("  Se alguma nao bater, PARA e nao mandes moedas para la.")
    print()
    return 0


# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dice", help="lancamentos de d6 tal como os introduziste (digitos 1-6)")
    ap.add_argument("--coin", help="lancamentos de moeda (digitos 0-1), alternativa a --dice")
    ap.add_argument("--chip", help="E_chip: os 64 hex que o dispositivo mostrou")
    ap.add_argument("--commit", help="compromisso anotado ANTES dos lancamentos "
                                     "(hex; bastam os primeiros 32)")
    ap.add_argument("--entropy", help="entropia final em hex, se ja a tiveres")
    ap.add_argument("--passphrase", default="", help="passphrase BIP39 (por omissao, vazia)")
    ap.add_argument("--wordlist", help="caminho para english.txt")
    ap.add_argument("--self-test", action="store_true",
                    help="corre os vetores oficiais e sai")
    ap.add_argument("--passo-a-passo", action="store_true",
                    help="faz perguntas em vez de exigir argumentos "
                         "(e o que acontece se nao indicares nada)")
    args = ap.parse_args()

    wordlist = load_wordlist(args.wordlist)

    if args.self_test:
        return self_test(wordlist)

    # Sem argumentos nenhuns, pergunta. Quem chega aqui as escuras nao devia
    # ter de descobrir sozinho que precisa de --dice, --chip e --commit.
    if args.passo_a_passo or not (args.dice or args.coin or args.entropy):
        return interativo(wordlist)

    e_fis = e_chip = None
    if args.entropy:
        entropy = bytes.fromhex(args.entropy.strip())
        if len(entropy) != 32:
            ap.error("--entropy tem de ter 64 caracteres hex (32 bytes)")
        if args.commit:
            ap.error("--commit so faz sentido com --dice/--coin e --chip: "
                     "o compromisso e sobre o E_chip, que --entropy ja nao tem")
    elif args.dice or args.coin:
        if not args.chip:
            ap.error("--chip e obrigatorio com --dice/--coin "
                     "(sao os 64 hex do ecra da revelacao)")
        e_fis = dice_to_entropy(args.dice) if args.dice else coin_to_entropy(args.coin)
        e_chip = bytes.fromhex(args.chip.strip())
        if len(e_chip) != 32:
            ap.error("--chip tem de ter 64 caracteres hex (32 bytes)")
        # Antes de qualquer outra coisa: se o chip nao honrou o compromisso, o
        # resto do relatorio nao interessa nada e nao deve sequer ser impresso.
        if args.commit:
            check_commit(e_chip, args.commit)
        entropy = combine(e_fis, e_chip)
    else:
        ap.error("indica --dice/--coin com --chip, ou --entropy, ou --self-test")

    report(entropy, args.passphrase, wordlist, e_fis, e_chip, args.commit)
    return 0


if __name__ == "__main__":
    # Nada aqui pode acabar num traceback do Python. Quem esta a verificar uma
    # seed com a folha na mao precisa de uma frase que se entenda, nao de uma
    # pilha de chamadas -- e um erro incompreensivel leva a decisao errada com
    # a mesma facilidade com que leva a nenhuma decisao.
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except ValueError as exc:
        print(f"\nerro: {exc}\n", file=sys.stderr)
        raise SystemExit(2) from None
    except FileNotFoundError as exc:
        print(f"\nFalta um ficheiro: {exc.filename}\n"
              f"O english.txt tem de estar na mesma pasta que este programa.\n",
              file=sys.stderr)
        raise SystemExit(2) from None
    except Exception as exc:                                  # noqa: BLE001
        print(f"\nAlgo correu mal e nao foi verificado nada: "
              f"{type(exc).__name__}: {exc}\n"
              f"Nao uses a seed com base nesta execucao.\n", file=sys.stderr)
        raise SystemExit(2) from None
