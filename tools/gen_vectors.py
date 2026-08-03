#!/usr/bin/env python3
"""Gera test/vectors_generated.h a partir dos vetores oficiais em test/vectors/.

Evita meter um parser de JSON dentro do firmware so para correr testes, e
garante que o codigo C e o verify.py sao medidos exatamente contra os mesmos
numeros oficiais.

  python tools/gen_vectors.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
VEC = os.path.join(ROOT, "test", "vectors")
DST = os.path.join(ROOT, "test", "vectors_generated.h")

# Vetor oficial do BIP-84 (o mesmo que o verify.py ja confirma).
BIP84 = {
    "mnemonic": ("abandon abandon abandon abandon abandon abandon abandon "
                 "abandon abandon abandon abandon about"),
    "zpub": ("zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1A"
             "DqtfSdVCToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs"),
    "addr0": "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",
    "addr1": "bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g",
    "change0": "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el",
}


def c_str(s: str) -> str:
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main() -> int:
    bip39_path = os.path.join(VEC, "bip39_vectors.json")
    bip32_path = os.path.join(VEC, "bip32_vectors.json")
    for p in (bip39_path, bip32_path):
        if not os.path.exists(p):
            print(f"ERRO: falta {p}. Corre tools/fetch_vectors.py", file=sys.stderr)
            return 1

    with open(bip39_path, encoding="utf-8") as fh:
        bip39 = json.load(fh)["english"]
    with open(bip32_path, encoding="utf-8") as fh:
        bip32 = json.load(fh)

    out = [
        "/* GERADO POR tools/gen_vectors.py -- NAO EDITAR A MAO.",
        " *",
        " * Vetores oficiais:",
        " *   BIP39  trezor/python-mnemonic vectors.json (passphrase \"TREZOR\")",
        " *   BIP32  extraidos do proprio bip-0032.mediawiki",
        " *   BIP84  do bip-0084.mediawiki",
        " */",
        "#ifndef HSV3_VECTORS_GENERATED_H",
        "#define HSV3_VECTORS_GENERATED_H",
        "",
        "typedef struct { const char *entropy, *mnemonic, *seed, *xprv; } bip39_vec_t;",
        "typedef struct { const char *seed, *path, *xpub, *xprv; } bip32_vec_t;",
        "",
        f"#define BIP39_VEC_COUNT {len(bip39)}",
        "static const bip39_vec_t BIP39_VECTORS[BIP39_VEC_COUNT] = {",
    ]
    for ent, mn, seed, xprv in bip39:
        out.append(f"    {{ {c_str(ent)}, {c_str(mn)},")
        out.append(f"      {c_str(seed)}, {c_str(xprv)} }},")
    out += [
        "};",
        "",
        f"#define BIP32_VEC_COUNT {len(bip32)}",
        "static const bip32_vec_t BIP32_VECTORS[BIP32_VEC_COUNT] = {",
    ]
    for v in bip32:
        out.append(f"    {{ {c_str(v['seed'])}, {c_str(v['chain'])},")
        out.append(f"      {c_str(v['xpub'])}, {c_str(v['xprv'])} }},")
    out += [
        "};",
        "",
        f"#define BIP84_MNEMONIC {c_str(BIP84['mnemonic'])}",
        f"#define BIP84_ZPUB     {c_str(BIP84['zpub'])}",
        f"#define BIP84_ADDR0    {c_str(BIP84['addr0'])}",
        f"#define BIP84_ADDR1    {c_str(BIP84['addr1'])}",
        f"#define BIP84_CHANGE0  {c_str(BIP84['change0'])}",
        "",
        "#endif /* HSV3_VECTORS_GENERATED_H */",
        "",
    ]

    with open(DST, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(out))

    print(f"escrito {os.path.relpath(DST, ROOT)}")
    print(f"  BIP39: {len(bip39)} vetores")
    print(f"  BIP32: {len(bip32)} vetores")
    print("  BIP84: 1 vetor (zpub + 3 enderecos)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
