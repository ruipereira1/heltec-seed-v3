#!/usr/bin/env python3
"""Gera core/hsv3_wordlist.c a partir de tools/english.txt.

A wordlist nunca e escrita a mao: e sempre gerada deste ficheiro, cujo SHA256 e
verificado contra o valor canonico do repositorio dos BIPs. Assim o firmware e
o verify.py usam por construcao exatamente as mesmas 2048 palavras.

  python tools/gen_wordlist.py
"""
import hashlib
import os
import sys

CANONICAL_SHA256 = "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda"

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "english.txt")
DST = os.path.join(HERE, "..", "core", "hsv3_wordlist.c")


def main() -> int:
    with open(SRC, "rb") as fh:
        raw = fh.read().replace(b"\r\n", b"\n")

    digest = hashlib.sha256(raw).hexdigest()
    if digest != CANONICAL_SHA256:
        print(f"ERRO: english.txt nao e a oficial.\n  obtido:   {digest}\n"
              f"  esperado: {CANONICAL_SHA256}", file=sys.stderr)
        return 1

    words = raw.decode("ascii").split()
    if len(words) != 2048:
        print(f"ERRO: {len(words)} palavras, esperadas 2048", file=sys.stderr)
        return 1

    longest = max(len(w) for w in words)
    if longest > 8:
        print(f"ERRO: palavra com {longest} caracteres, o array assume max 8",
              file=sys.stderr)
        return 1

    lines = [
        "/* GERADO POR tools/gen_wordlist.py -- NAO EDITAR A MAO.",
        " *",
        " * Wordlist BIP39 inglesa oficial, 2048 palavras.",
        f" * SHA256 de english.txt: {CANONICAL_SHA256}",
        " *",
        " * Array plano de largura fixa em vez de ponteiros: indexa-se direto,",
        " * fica todo em flash e nao precisa de relocacao no arranque.",
        " */",
        '#include "hsv3_wordlist.h"',
        "",
        "const char hsv3_wordlist[HSV3_WORDLIST_SIZE][HSV3_WORD_MAXLEN + 1] = {",
    ]
    for i in range(0, 2048, 4):
        row = ", ".join(f'"{w}"' for w in words[i:i + 4])
        lines.append(f"    {row},")
    lines.append("};")
    lines.append("")

    with open(DST, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(lines))

    print(f"escrito {os.path.normpath(DST)}")
    print(f"2048 palavras, maior tem {longest} caracteres, "
          f"{2048 * 9} bytes em flash")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
