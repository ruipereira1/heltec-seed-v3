#!/usr/bin/env python3
"""Confronta o nucleo em C contra o verify.py em Python.

Sao duas implementacoes escritas de forma independente: o firmware usa mbedTLS,
o verify.py implementa o secp256k1 e o BIP32 de raiz. Se concordarem em milhares
de entradas aleatorias, e porque nenhuma tem um bug que a outra tambem nao
tenha. E o mais perto de uma prova que se consegue sem auditoria externa.

Corre test/build.sh antes, para o binario existir.

  python tools/crosscheck.py [n]
"""
import os
import random
import string
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
DUMP = os.path.join(ROOT, "test", "build", "hsv3_dump.exe")

sys.path.insert(0, HERE)
import verify as V  # noqa: E402

FIELDS = ("e_phys", "e_chip", "commit", "entropy", "mnemonic", "fingerprint",
          "bip84", "bip86", "bip49", "bip44", "addr0")

PATHS = {"bip84": ("m/84'/0'/0'", "zpub"), "bip86": ("m/86'/0'/0'", "xpub"),
         "bip49": ("m/49'/0'/0'", "ypub"), "bip44": ("m/44'/0'/0'", "xpub")}


def run_c(dice, chip, passphrase, kind="--dice"):
    cmd = [DUMP, kind, dice, "--chip", chip]
    if passphrase:
        cmd += ["--passphrase", passphrase]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"hsv3_dump falhou: {res.stderr.strip()}")
    out = {}
    for line in res.stdout.splitlines():
        key, _, val = line.partition(" ")
        out[key] = val
    return out


def run_py(dice, chip, passphrase, wordlist, kind="--dice"):
    e_phys = (V.dice_to_entropy(dice) if kind == "--dice"
              else V.coin_to_entropy(dice))
    e_chip = bytes.fromhex(chip)
    entropy = V.combine(e_phys, e_chip)
    mnemonic = V.entropy_to_mnemonic(entropy, wordlist)
    master = V.HDKey.from_seed(V.mnemonic_to_seed(mnemonic, passphrase))
    out = {
        "e_phys": e_phys.hex(), "e_chip": e_chip.hex(),
        "commit": V.commit_of(e_chip).hex(), "entropy": entropy.hex(),
        "mnemonic": mnemonic, "fingerprint": master.fingerprint.hex(),
    }
    for label, (path, kindstr) in PATHS.items():
        out[label] = master.derive(path).serialize(kindstr)
    out["addr0"] = master.derive("m/84'/0'/0'/0/0").address_p2wpkh()
    return out


def main() -> int:
    if not os.path.exists(DUMP):
        print(f"falta {DUMP}\ncorre primeiro:  sh test/build.sh all", file=sys.stderr)
        return 2

    n = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    wordlist = V.load_wordlist()
    rnd = random.Random(0xC01DCA5E)   # deterministico, para o resultado ser repetivel
    ascii_ok = string.ascii_letters + string.digits + " !#$%&()*+,-./:;<=>?@[]^_{}~"
    bad = 0

    print(f"{n} cerimonias aleatorias, {len(FIELDS)} campos cada "
          f"= {n * len(FIELDS)} comparacoes C vs Python\n")

    for i in range(n):
        coin_mode = (i % 4 == 3)
        if coin_mode:
            kind, digits = "--coin", "".join(rnd.choice("01") for _ in range(rnd.randint(256, 300)))
        else:
            kind, digits = "--dice", "".join(rnd.choice("123456") for _ in range(rnd.randint(100, 130)))
        chip = os.urandom(32).hex()
        passphrase = "" if i % 3 == 0 else "".join(
            rnd.choice(ascii_ok) for _ in range(rnd.randint(1, 40)))

        c = run_c(digits, chip, passphrase, kind)
        p = run_py(digits, chip, passphrase, wordlist, kind)

        for f in FIELDS:
            if c.get(f) != p.get(f):
                bad += 1
                print(f"  [X] cerimonia {i} campo {f}\n"
                      f"      C:      {c.get(f)}\n"
                      f"      Python: {p.get(f)}")
        if (i + 1) % 5 == 0:
            print(f"  {i + 1}/{n} ok" if not bad else f"  {i + 1}/{n} ({bad} divergencias)")

    print()
    if bad:
        print(f"DIVERGEM em {bad} campos -- NAO usar este firmware")
        return 1
    print(f"OK -- as duas implementacoes concordam em {n * len(FIELDS)} campos")
    print("     (entropia, compromisso, 24 palavras, fingerprint, 4 xpubs")
    print("      e o 1o endereco)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
