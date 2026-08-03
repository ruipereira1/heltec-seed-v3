#!/usr/bin/env python3
"""Corre a bateria toda e diz uma coisa so no fim: passa ou nao passa.

  python test/correr_tudo.py

Existe porque testes que e preciso lembrar de correr sao testes que nao se
correm. Se algum falhar, isto sai com codigo 1 e mostra a saida desse.

Um teste que nao pode correr nesta maquina conta como falha, nao como sucesso
silencioso -- e a mesma regra que o firmware segue com as fontes de entropia.
"""
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable


def achar_sh():
    """O sh do Git para Windows nem sempre esta no PATH do PowerShell."""
    for nome in ("sh", "bash"):
        c = shutil.which(nome)
        if c:
            return c
    for c in (r"C:\Program Files\Git\bin\sh.exe",
              r"C:\Program Files\Git\usr\bin\sh.exe",
              r"C:\Program Files (x86)\Git\bin\sh.exe"):
        if os.path.exists(c):
            return c
    return None


SH = achar_sh()

BATERIA = [
    ("vetores oficiais BIP39/BIP32/BIP84 + compromisso",
     [PY, "tools/verify.py", "--self-test"]),
    ("nucleo em C e camada port/ (zig cc)",
     ([SH, "test/build.sh", "all"] if SH else None),
     "falta o 'sh'. Instala o Git para Windows, ou corre "
     "'sh test/build.sh all' num terminal que o tenha."),
    ("as duas implementacoes concordam",
     [PY, "tools/crosscheck.py", "25"]),
    ("modo passo a passo do verificador",
     [PY, "test/test_verify_interativo.py"]),
    ("invariantes do codigo",
     [PY, "test/test_invariantes.py"]),
    ("as invariantes tem dentes (mutacoes)",
     [PY, "test/test_mutacoes.py"]),
    ("estimador de min-entropia (SP 800-90B)",
     [PY, "tools/entropia.py", "--self-test"]),
]

largura = max(len(p[0]) for p in BATERIA) + 2
resultados = []

print()
for passo in BATERIA:
    nome, cmd = passo[0], passo[1]
    porque = passo[2] if len(passo) > 2 else ""
    print(f"  {nome:<{largura}} ", end="", flush=True)

    if cmd is None:
        print("SALTADO")
        resultados.append((nome, False, f"Nao correu: {porque}"))
        continue

    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        saida, ok = r.stdout + r.stderr, r.returncode == 0
    except OSError as exc:
        saida, ok = f"nao consegui executar {cmd[0]}: {exc}", False
    print(f"{'ok' if ok else 'FALHOU':<7} {time.time() - t0:5.1f}s")
    resultados.append((nome, ok, saida))

maus = [(n, s) for n, ok, s in resultados if not ok]

print()
if maus:
    for nome, saida in maus:
        print("=" * 72)
        print(f"  {nome}")
        print("=" * 72)
        print(saida.strip())
        print()
    print(f"FALHOU -- {len(maus)} de {len(BATERIA)}")
    sys.exit(1)

# O numero total sai das saidas, para ser um facto e nao uma afirmacao de fe.
CONTAR = re.compile(r"todas as (\d+)|todos os (\d+)|(\d+) campos|as (\d+) mutacoes")
total = sum(int(next(g for g in m.groups() if g))
            for _, _, saida in resultados
            for m in CONTAR.finditer(saida))

print(f"OK -- a bateria toda passa ({total} verificacoes)")
print()
print("  O que isto NAO cobre, e convem nao esquecer:")
print("    - a cerimonia nunca correu de ponta a ponta em hardware")
print("    - o build reproduzivel em Docker esta por correr")
print("    - ninguem de fora auditou este codigo")
