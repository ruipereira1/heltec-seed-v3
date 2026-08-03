#!/usr/bin/env python3
"""Descarrega os vetores oficiais e a wordlist BIP39, verificando o que da.

Corre uma vez, com rede. Depois o projeto compila e testa offline.

  python tools/fetch_vectors.py
"""
import hashlib
import json
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
VEC = os.path.join(ROOT, "test", "vectors")

WORDLIST_URL = "https://raw.githubusercontent.com/bitcoin/bips/master/bip-0039/english.txt"
WORDLIST_SHA256 = "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda"
BIP39_URL = "https://raw.githubusercontent.com/trezor/python-mnemonic/master/vectors.json"
BIP32_URL = "https://raw.githubusercontent.com/bitcoin/bips/master/bip-0032.mediawiki"


def get(url: str) -> bytes:
    print(f"  GET {url}")
    return urllib.request.urlopen(url, timeout=60).read()


def main() -> int:
    os.makedirs(VEC, exist_ok=True)

    # --- wordlist, com hash verificado ---
    raw = get(WORDLIST_URL).replace(b"\r\n", b"\n")
    digest = hashlib.sha256(raw).hexdigest()
    if digest != WORDLIST_SHA256:
        print(f"ERRO: english.txt com hash inesperado\n  obtido:   {digest}\n"
              f"  esperado: {WORDLIST_SHA256}", file=sys.stderr)
        return 1
    with open(os.path.join(HERE, "english.txt"), "wb") as fh:
        fh.write(raw)
    print(f"  OK english.txt ({len(raw.split())} palavras, sha256 confere)")

    # --- vetores BIP39 ---
    data = get(BIP39_URL)
    parsed = json.loads(data)
    if len(parsed.get("english", [])) < 24:
        print("ERRO: vectors.json sem os 24 vetores ingleses", file=sys.stderr)
        return 1
    with open(os.path.join(VEC, "bip39_vectors.json"), "wb") as fh:
        fh.write(data)
    print(f"  OK bip39_vectors.json ({len(parsed['english'])} vetores)")

    # --- vetores BIP32, extraidos do texto do proprio BIP ---
    txt = get(BIP32_URL).decode("utf-8", "replace")
    out, seed = [], None
    for line in txt.splitlines():
        s = line.strip()
        m = re.match(r"^Seed \(hex\):\s*([0-9a-fA-F]+)", s)
        if m:
            seed = m.group(1)
            continue
        m = re.match(r"^\*\s*Chain (\S+)", s)
        if m:
            chain = m.group(1).replace("<sub>H</sub>", "'").replace("<sub>h</sub>", "'")
            out.append({"seed": seed, "chain": chain})
            continue
        m = re.match(r"^\*\*\s*ext (pub|prv):\s*([0-9A-Za-z]+)", s)
        if m and out:
            out[-1]["x" + m.group(1)] = m.group(2)
    out = [v for v in out if v.get("seed") and v.get("xpub") and v.get("xprv")]
    if len(out) < 15:
        print(f"ERRO: so {len(out)} vetores BIP32 extraidos; o formato do BIP mudou?",
              file=sys.stderr)
        return 1
    with open(os.path.join(VEC, "bip32_vectors.json"), "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=1)
    print(f"  OK bip32_vectors.json ({len(out)} caminhos, "
          f"{len({v['seed'] for v in out})} seeds)")

    print("\nAgora:  python tools/gen_wordlist.py && python tools/gen_vectors.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
