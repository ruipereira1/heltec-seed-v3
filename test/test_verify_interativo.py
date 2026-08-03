"""Testa o modo passo a passo do verify.py como se fosse uma pessoa a escrever.

  python test/test_verify_interativo.py

Cada caso alimenta o stdin com as respostas e confirma o que sai. Nao chegam os
casos felizes: o que interessa aqui sao os erros que se cometem mesmo, com a
folha na mao e a vista cansada -- escrever a letra 'o' em vez do zero, contar
mal os digitos, copiar tambem o numero da linha do ecra. Em cada um deles o
programa tem de explicar o que esta errado e deixar repetir, nunca aceitar em
silencio nem rebentar com um traceback do Python.

O caso que mais importa e o ultimo tipo: um compromisso que nao bate certo tem
de dar uma paragem clara, nao um relatorio com um aviso pequeno no meio.
"""
import hashlib
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, 'tools', 'verify.py')

DADOS = ''.join('123456'[i % 6] for i in range(100))
ECHIP = hashlib.sha256(b'e_chip de teste').digest()
COMMIT = hashlib.sha256(ECHIP).hexdigest()


def grupos(hexstr):
    """Os 64 hex como as 4 linhas que o ecra mostra."""
    out = []
    for r in range(4):
        p = hexstr[r * 16:(r + 1) * 16]
        out.append(f'{p[0:4]} {p[4:8]} {p[8:12]} {p[12:16]}')
    return out


def correr(nome, respostas, espera_saida, espera_codigo):
    entrada = '\n'.join(respostas) + '\n'
    r = subprocess.run([sys.executable, VERIFY], input=entrada,
                       capture_output=True, text=True,
                       cwd=os.path.join(ROOT, 'tools'))
    saida = r.stdout + r.stderr
    ok = True
    if r.returncode != espera_codigo:
        print(f'  [X] {nome}: codigo {r.returncode}, esperado {espera_codigo}')
        ok = False
    for e in espera_saida:
        if e not in saida:
            print(f'  [X] {nome}: falta na saida -> {e!r}')
            ok = False
    if 'Traceback' in saida:
        print(f'  [X] {nome}: rebentou com um traceback do Python')
        ok = False
    print(f'  [{"ok" if ok else "X"}] {nome}')
    if not ok:
        print('  ---- saida ----')
        print('\n'.join('       ' + l for l in saida.splitlines()[-25:]))
    return ok


d = [DADOS[i * 10:(i + 1) * 10] for i in range(10)]
g = grupos(COMMIT)
c = grupos(ECHIP.hex())

falhas = 0

falhas += not correr(
    'cerimonia certa, sem passphrase',
    [''] + ['1'] + g + d + c + ['1'],
    ['O compromisso bate certo', '24 PALAVRAS', 'fingerprint mestre',
     '1o endereco', 'O QUE FAZER AGORA'], 0)

# o ataque adaptativo: o E_chip revelado nao corresponde ao compromisso
mau = ('f' if COMMIT[0] != 'f' else '0') + COMMIT[1:]
falhas += not correr(
    'compromisso que nao bate',
    ['', '1'] + grupos(mau) + d + c,
    ['PARA. NAO USES ESTA SEED', 'deita a placa fora'], 1)

falhas += not correr(
    'letra o em vez de zero (e corrige)',
    ['', '1', g[0].replace(g[0][0], 'o', 1), g[0]] + g[1:] + d + c + ['1'],
    ['Escreveste a letra', '0 (zero)', 'O compromisso bate certo'], 0)

falhas += not correr(
    'linha de lancamentos com 9 digitos (e corrige)',
    ['', '1'] + g + [d[0][:9]] + d + c + ['1'],
    ['faltam ou sobram 1', 'O compromisso bate certo'], 0)

falhas += not correr(
    'copia o numero da linha junto (aceite)',
    ['', '1'] + [f'{i + 1} {g[i]}' for i in range(4)] + d + c + ['1'],
    ['O compromisso bate certo', 'fingerprint mestre'], 0)

falhas += not correr(
    'lancamento com um 7 (e corrige)',
    ['', '1'] + g + ['1234567890', d[0]] + d[1:] + c + ['1'],
    ['Um dado so pode dar 1, 2, 3, 4, 5 ou 6', 'O compromisso bate certo'], 0)

falhas += not correr(
    'sem compromisso anotado -> aviso',
    ['', '2'] + d + c + ['1'],
    ['AVISO: nao foi conferido nenhum compromisso', '24 PALAVRAS'], 0)

falhas += not correr(
    'fim de ficheiro a meio', [''], ['Interrompido'], 1)

print()
if falhas:
    print(f'FALHOU -- {falhas} casos')
    sys.exit(1)
print('OK -- todos os 8 casos do modo passo a passo passaram')
