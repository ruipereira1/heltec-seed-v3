#!/usr/bin/env python3
"""Estima a min-entropia por amostra da fonte de jitter, pelo SP 800-90B.

  # na placa, com a build de medicao (NAO gera seeds):
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" \
         -D HSV3_JITTER_DUMP=1 build flash
  idf.py -p COM3 monitor > amostras.txt

  # no PC:
  python tools/entropia.py amostras.txt
  python tools/entropia.py --cortes      # reproduz os cortes RCT/APT do .h
  python tools/entropia.py --self-test

PORQUE E' QUE ISTO EXISTE
-------------------------
O firmware assume um bit de min-entropia por amostra de jitter e recolhe 1024
amostras para 256 bits de saida. Esse numero e' uma suposicao escrita num
comentario em port/hsv3_rng.h -- a afirmacao mais fraca do projeto inteiro, e a
unica que ninguem tinha maneira de contestar sem hardware.

Este programa transforma-a num numero medido na tua placa.

O QUE ISTO NAO E'
-----------------
Nao e' a ferramenta oficial do NIST, e nao substitui uma certificacao. O
SP 800-90B tem dez estimadores para fontes nao-IID; aqui estao cinco, e quatro
deles correm sobre a amostra de 16 bits inteira (o quinto, Markov, so' serve
para bits). Como a min-entropia final e' o MINIMO de todos os dez, um
subconjunto so pode dar um valor IGUAL OU MAIOR que o verdadeiro.

Ou seja: o numero que sai daqui e' um LIMITE SUPERIOR otimista. Se ele ja
estiver abaixo do que o firmware assume, a fonte esta pior do que isso.
Para um numero para publicar, corre a ferramenta oficial:
https://github.com/usnistgov/SP800-90B_EntropyAssessment
"""
from __future__ import annotations

import math
import os
import re
import sys
from collections import Counter, defaultdict

Z = 2.576   # 99% de confianca, como o SP 800-90B usa


# ---------------------------------------------------------------------------
# Os numeros vem do firmware, nao sao duplicados a mao. Duplicar constantes em
# dois ficheiros e' como se tornou "meio bit" numa suposicao que sobreviveu sem
# ninguem reparar -- aqui le-se sempre o port/hsv3_rng.h actual.
# ---------------------------------------------------------------------------

def _ler_constantes_firmware() -> tuple[int, int, int]:
    caminho = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "port", "hsv3_rng.h")
    with open(caminho, encoding="utf-8") as fh:
        txt = fh.read()
    num = int(re.search(r"HSV3_JITTER_H_MIN_NUM\s+(\d+)", txt).group(1))
    den = int(re.search(r"HSV3_JITTER_H_MIN_DEN\s+(\d+)", txt).group(1))
    # a mesma conta inteira de port/hsv3_rng.c: 256 * DEN/NUM * 4
    amostras = 256 * den // num * 4
    return num, den, amostras


try:
    HSV3_JITTER_H_MIN_NUM, HSV3_JITTER_H_MIN_DEN, JITTER_SAMPLES = \
        _ler_constantes_firmware()
except (OSError, AttributeError):
    # Sem o firmware ao lado (por exemplo, este ficheiro copiado sozinho):
    # cai nos valores actuais em vez de rebentar.
    HSV3_JITTER_H_MIN_NUM, HSV3_JITTER_H_MIN_DEN, JITTER_SAMPLES = 1, 1, 1024


# ---------------------------------------------------------------------------
# 6.3.1  Most Common Value
# ---------------------------------------------------------------------------

def est_mcv(s: list[int]) -> float:
    L = len(s)
    p_max = max(Counter(s).values()) / L
    p_u = min(1.0, p_max + Z * math.sqrt(p_max * (1 - p_max) / (L - 1)))
    return -math.log2(p_u)


# ---------------------------------------------------------------------------
# 6.3.3  Markov (so para amostras binarias)
# ---------------------------------------------------------------------------

def est_markov(s: list[int]) -> float:
    """Probabilidade da sequencia mais provavel de 128 amostras.

    E' o estimador que apanha correlacao de primeira ordem -- exatamente o
    risco de uma fonte que mede intervalos de tempo consecutivos.
    """
    L = len(s)
    c0 = s.count(0)
    p0 = c0 / L
    t = [[0, 0], [0, 0]]
    for a, b in zip(s, s[1:]):
        t[a][b] += 1

    P = [[0.0, 0.0], [0.0, 0.0]]
    for a in (0, 1):
        n = t[a][0] + t[a][1]
        if n == 0:
            P[a][0] = P[a][1] = 0.5
            continue
        for b in (0, 1):
            # limite superior de confianca de cada transicao
            p = t[a][b] / n
            P[a][b] = min(1.0, p + Z * math.sqrt(p * (1 - p) / max(n - 1, 1)))

    # sequencia de 128 amostras mais provavel, por programacao dinamica
    melhor = [p0, 1 - p0]
    for _ in range(127):
        novo = [0.0, 0.0]
        for b in (0, 1):
            novo[b] = max(melhor[a] * P[a][b] for a in (0, 1))
        melhor = novo
    p_max = max(melhor)
    if p_max <= 0:
        return 1.0
    return min(1.0, -math.log2(p_max) / 128)


# ---------------------------------------------------------------------------
# 6.3.6  Estrutura comum aos preditores
# ---------------------------------------------------------------------------

def _prob_sem_corrida(p: float, r: int, n: int) -> float:
    """Pr(nao ha nenhuma corrida de r acertos seguidos em n tentativas).

    Usa a forma fechada do SP 800-90B 6.3.6 em vez da recorrencia passo a
    passo: a recorrencia e O(n*r) por avaliacao e, com n = 20000 e corridas
    de uma centena, tornava a bisseccao inviavel. Aqui cada avaliacao e O(1)
    depois de achar a raiz.

    x e a raiz real de  1 - x + q*p^r*x^(r+1) = 0  em (1, 1/p).
    """
    if p <= 0 or r > n:
        return 1.0          # corrida mais longa que a sequencia: impossivel
    if p >= 1:
        return 0.0
    q = 1.0 - p

    # x*(p*x)^r em vez de p^r * x^(r+1): com r na casa dos milhares, o segundo
    # estoura em OverflowError antes de os dois factores se cancelarem.
    def f(x):
        return 1.0 - x + q * x * (p * x) ** r

    if f(1.0) <= 0.0:
        return 1.0          # q*p^r desapareceu por underflow: sem corrida

    # f e' positiva em x=1 e nao-positiva em x=1/p; bissecciona ate a travessia
    lo, hi = 1.0, 1.0 / p
    for _ in range(200):
        mid = (lo + hi) / 2
        if f(mid) > 0.0:
            lo = mid
        else:
            hi = mid
    x = (lo + hi) / 2

    denom = (r + 1 - r * x) * q
    if denom <= 0 or x <= 1.0:
        return 0.0

    # em logaritmos: x^(n+1) com n na casa das dezenas de milhar estoura
    log_p = (math.log(1.0 - p * x) - math.log(denom) - (n + 1) * math.log(x))
    if log_p > 0:
        return 1.0
    if log_p < -700:
        return 0.0
    return min(1.0, max(0.0, math.exp(log_p)))


def _p_local(r: int, n: int) -> float:
    """Menor p com Pr(nenhuma corrida de r em n) = 0.99, por bisseccao."""
    lo, hi = 0.0, 1.0
    for _ in range(50):
        mid = (lo + hi) / 2
        if _prob_sem_corrida(mid, r, n) > 0.99:
            lo = mid
        else:
            hi = mid
    return hi


def _resultado_preditor(acertos: list[bool]) -> float:
    """Min-entropia a partir da lista de acertos de um preditor."""
    N = len(acertos)
    if N == 0:
        return 1.0
    C = sum(acertos)
    p_global = C / N
    if p_global == 0:
        p_global_u = min(1.0, 1 - 0.01 ** (1 / N))
    else:
        p_global_u = min(1.0, p_global
                         + Z * math.sqrt(p_global * (1 - p_global) / (N - 1)))

    # maior corrida de acertos seguidos
    r, melhor = 0, 0
    for a in acertos:
        r = r + 1 if a else 0
        melhor = max(melhor, r)
    p_loc = _p_local(melhor + 1, N) if melhor > 0 else 0.0

    p = max(p_global_u, p_loc)
    if p <= 0:
        return 1.0
    # o max(0, ...) evita um "-0.000" quando o preditor acerta sempre
    return max(0.0, -math.log2(p))


# ---------------------------------------------------------------------------
# 6.3.7  MultiMCW   6.3.8  Lag   6.3.9  MultiMMC
# ---------------------------------------------------------------------------

def est_multimcw(s: list[int]) -> float:
    """Cada subpreditor diz o valor mais comum da sua janela anterior.

    As contagens sao mantidas de forma incremental. Recalcular um Counter
    sobre uma janela de 4095 a cada passo dava 80 milhoes de operacoes numa
    sequencia de 20 mil amostras -- o estimador nunca acabava.
    """
    janelas = [w for w in (63, 255, 1023, 4095) if w < len(s)]
    if not janelas:
        return 1.0

    inicio = max(janelas)
    # uns[j] = quantos 1 ha na janela j que termina em i-1
    uns = [sum(s[inicio - w:inicio]) for w in janelas]
    pontos = [0] * len(janelas)
    acertos = []

    for i in range(inicio, len(s)):
        previsoes = [1 if uns[j] * 2 > w else 0 for j, w in enumerate(janelas)]
        melhor = pontos.index(max(pontos))
        acertos.append(previsoes[melhor] == s[i])
        for j, p in enumerate(previsoes):
            if p == s[i]:
                pontos[j] += 1
        # desliza cada janela uma posicao
        for j, w in enumerate(janelas):
            uns[j] += s[i] - s[i - w]
    return _resultado_preditor(acertos)


def est_lag(s: list[int]) -> float:
    D = min(128, len(s) - 1)
    pontos = [0] * D
    acertos = []
    for i in range(D, len(s)):
        melhor = pontos.index(max(pontos))
        acertos.append(s[i - melhor - 1] == s[i])
        for d in range(D):
            if s[i - d - 1] == s[i]:
                pontos[d] += 1
    return _resultado_preditor(acertos)


def est_multimmc(s: list[int]) -> float:
    D = 16
    modelos = [defaultdict(Counter) for _ in range(D)]
    pontos = [0] * D
    acertos = []
    for i in range(D, len(s)):
        previsoes = []
        for d in range(D):
            ctx = tuple(s[i - d - 1:i])
            c = modelos[d].get(ctx)
            previsoes.append(c.most_common(1)[0][0] if c else None)
        melhor = pontos.index(max(pontos))
        acertos.append(previsoes[melhor] == s[i])
        for d in range(D):
            if previsoes[d] == s[i]:
                pontos[d] += 1
            modelos[d][tuple(s[i - d - 1:i])][s[i]] += 1
    return _resultado_preditor(acertos)


ESTIMADORES = [
    ("6.3.1  most common value", est_mcv),
    ("6.3.3  markov (128)", est_markov),
    ("6.3.7  multi most common in window", est_multimcw),
    ("6.3.8  lag", est_lag),
    ("6.3.9  multi markov model w/ counting", est_multimmc),
]

# Destes cinco, so o Markov (6.3.3) esta escrito so' para alfabeto binario --
# tem uma tabela de transicao 2x2 codificada a direito. Os outros quatro
# comparam VALORES, nao bits, e por isso funcionam sobre qualquer alfabeto: e'
# o que permite corre-los directamente sobre as amostras de 16 bits que o
# firmware usa desde a correcao de 04/08/2026, sem ter de as reduzir a um bit
# cada uma primeiro -- que foi exactamente o erro que a medicao apanhou.
ESTIMADORES_QUALQUER_ALFABETO = [
    (nome, f) for nome, f in ESTIMADORES if not nome.startswith("6.3.3")
]


# ---------------------------------------------------------------------------
# Leitura do despejo
# ---------------------------------------------------------------------------

def ler_amostras(caminho: str) -> list[int]:
    """As contagens cruas de 32 bits, do despejo da UART."""
    brutas = []
    with open(caminho, encoding="utf-8", errors="replace") as fh:
        for linha in fh:
            t = linha.strip()
            if len(t) == 8 and all(c in "0123456789abcdefABCDEF" for c in t):
                brutas.append(int(t, 16))
    return brutas


def cortes(H: float, W: int = 512) -> tuple[int, int]:
    """Os cortes RCT e APT do SP 800-90B 4.4 para alfa = 2^-20.

    W = 512 e' a janela do SP 800-90B para amostras NAO binarias, que e' o
    caso do firmware desde 04/08/2026 (amostras de 16 bits, nao 1 bit). Passa
    W=1024 para reproduzir os cortes da versao anterior, so-binaria.
    """
    rct = 1 + math.ceil(20.0 / H)
    p = 2.0 ** -H
    # menor C com Pr(Binomial(W, p) >= C) <= 2^-20
    alvo = 2.0 ** -20
    lo, hi = 0, W
    while lo < hi:
        mid = (lo + hi) // 2
        cauda = sum(math.comb(W, k) * p ** k * (1 - p) ** (W - k)
                    for k in range(mid, W + 1))
        if cauda <= alvo:
            hi = mid
        else:
            lo = mid + 1
    return rct, lo


def self_test() -> int:
    """Sequencias com entropia conhecida, para o estimador nao mentir."""
    import random
    falhas = []

    def ver(nome, valor, minimo, maximo):
        if not (minimo <= valor <= maximo):
            falhas.append(f"{nome}: {valor:.3f}, esperado entre "
                          f"{minimo} e {maximo}")
        print(f"  {'ok ' if minimo <= valor <= maximo else '[X]'} "
              f"{nome:<34} {valor:5.3f}")

    r = random.Random(0xC0FFEE)
    n = 20000

    # Uma fonte perfeita tem de dar perto de 1 bit.
    boa = [r.getrandbits(1) for _ in range(n)]
    ver("aleatoria: perto de 1", min(f(boa) for _, f in ESTIMADORES), 0.90, 1.0)

    # Alternada 0101...: zero entropia. Se algum estimador nao a apanhar,
    # nao serve para nada.
    alt = [i % 2 for i in range(n)]
    ver("alternada 0101: perto de 0", min(f(alt) for _, f in ESTIMADORES), 0.0, 0.10)

    # Enviesada 90/10: -log2(0.9) = 0.152
    env = [0 if r.random() < 0.9 else 1 for _ in range(n)]
    ver("enviesada 90/10: ~0.15", est_mcv(env), 0.10, 0.25)

    # Com memoria forte: repete a anterior 95% das vezes.
    mem, x = [], 0
    for _ in range(n):
        if r.random() > 0.95:
            x ^= 1
        mem.append(x)
    ver("com memoria 95%: markov baixo", est_markov(mem), 0.0, 0.35)
    ver("com memoria 95%: mcv nao ve nada", est_mcv(mem), 0.80, 1.0)

    # Os quatro estimadores de qualquer alfabeto tem de funcionar directamente
    # sobre valores de 16 bits, nao so 0/1 -- e' assim que o firmware os usa.
    r16 = random.Random(0xFACADE)
    boa16 = [r16.getrandbits(16) for _ in range(n)]
    h16 = min(f(boa16) for _, f in ESTIMADORES_QUALQUER_ALFABETO)
    ver("16 bits aleatorios: perto do maximo do estimador", h16, 3.0, 20.0)

    presa16 = [0x1234] * n
    h16fixa = min(f(presa16) for _, f in ESTIMADORES_QUALQUER_ALFABETO)
    ver("16 bits sempre iguais: zero", h16fixa, 0.0, 0.05)

    # Os cortes tem de bater com os que estao no hsv3_rng.h (H=1, W=512)
    rct, apt = cortes(1.0, W=512)
    print(f"  {'ok ' if (rct, apt) == (21, 311) else '[X]'} "
          f"{'cortes RCT/APT para H=1.0, W=512':<34} {rct}, {apt}")
    if (rct, apt) != (21, 311):
        falhas.append(f"cortes {rct},{apt} != 21,311 do hsv3_rng.h")

    print()
    if falhas:
        for f in falhas:
            print(f"  [X] {f}")
        print(f"FALHOU -- {len(falhas)}")
        return 1
    print("OK -- o estimador reage como deve a entropia conhecida")
    return 0


def main() -> int:
    args = sys.argv[1:]

    if "--self-test" in args:
        return self_test()

    if "--cortes" in args:
        print("SP 800-90B 4.4, alfa = 2^-20\n")
        print("Amostras nao binarias (o firmware desde 04/08/2026: 16 bits cada)")
        print(f"  {'H (bits/amostra)':<20} {'RCT':>6} {'APT em 512':>10}")
        for H in (0.5, 1.0, 1.5, 2.0, 2.72):
            rct, apt = cortes(H, W=512)
            marca = "  <- hsv3_rng.h" if H == 1.0 else ""
            print(f"  {H:<20.2f} {rct:>6} {apt:>10}{marca}")
        print("\nAmostras binarias (versao anterior, so a paridade)")
        print(f"  {'H (bits/amostra)':<20} {'RCT':>6} {'APT em 1024':>10}")
        for H in (0.25, 0.5, 0.75, 1.0):
            rct, apt = cortes(H, W=1024)
            print(f"  {H:<20.2f} {rct:>6} {apt:>10}")
        return 0

    if not args:
        print(__doc__)
        return 2

    caminho = args[0]
    if not os.path.exists(caminho):
        print(f"nao encontrei {caminho}", file=sys.stderr)
        return 2

    brutas = ler_amostras(caminho)
    if len(brutas) < 1000:
        print(f"so encontrei {len(brutas)} amostras em {caminho}.\n"
              f"O despejo tem uma contagem de 8 hex por linha. Sao precisas\n"
              f"pelo menos 1000, e de preferencia 100000.", file=sys.stderr)
        return 2

    # O firmware guarda os 16 bits baixos de cada contagem desde 04/08/2026 --
    # e' essa a sequencia que interessa medir, nao um bit isolado dela. A
    # versao anterior media so a paridade (bit 0); ficou aqui como diagnostico
    # porque foi assim que se descobriu que o bit 0 esta morto nesta placa
    # (o laco de espera gasta um numero par de ciclos por iteracao).
    amostras = [b & 0xffff for b in brutas]
    bit0 = [b & 1 for b in brutas]

    print()
    print("=" * 62)
    print(f"  {len(brutas)} amostras de {os.path.basename(caminho)}")
    print("=" * 62)
    print()
    distintas = len(set(amostras))
    print(f"  valores de 16 bits distintos   {distintas} de {len(amostras)}")
    print(f"  proporcao de uns no bit 0      {sum(bit0) / len(bit0):.4f}  "
          f"(ideal 0.5 -- {'MORTO' if abs(sum(bit0)/len(bit0) - 0.5) > 0.3 else 'ok'})")
    print()
    print("  min-entropia por amostra de 16 bits, pelos quatro estimadores que")
    print("  funcionam sobre qualquer alfabeto (o Markov e' so' para bits):")
    print()

    valores = []
    for nome, f in ESTIMADORES_QUALQUER_ALFABETO:
        v = f(amostras)
        valores.append(v)
        print(f"    {nome:<40} {v:6.4f}")

    H = min(valores)
    print()
    print(f"  H (minimo dos acima)  {H:.4f} bits por amostra de 16 bits")
    print()
    print(f"  Diagnostico -- so o bit 0 (metodo antigo, ate 04/08/2026):")
    print(f"    {est_mcv(bit0):.4f} bits. Se isto vier perto de 0 mas o H")
    print(f"    acima nao, e' sinal de que um bit em particular esta preso mas")
    print(f"    a amostra continua viva -- foi o que aconteceu nesta placa.")
    print()

    ASSUMIDO = HSV3_JITTER_H_MIN_NUM / HSV3_JITTER_H_MIN_DEN
    print("=" * 62)
    if H >= ASSUMIDO:
        print(f"  A medicao ({H:.3f}) NAO CONTRADIZ o assumido ({ASSUMIDO}).")
        print(f"  Com {JITTER_SAMPLES} amostras: {H * JITTER_SAMPLES:.0f} bits para 256 de saida.")
    else:
        preciso = math.ceil(256 / H) if H > 0 else 0
        print(f"  A MEDICAO ESTA ABAIXO DO ASSUMIDO.")
        print(f"    medido   {H:.4f} bits/amostra")
        print(f"    assumido {ASSUMIDO:.4f}")
        print(f"  Com {JITTER_SAMPLES} amostras saem {H * JITTER_SAMPLES:.0f} bits, nao 256.")
        print()
        print(f"  Poe HSV3_JITTER_H_MIN a {H:.3f} em port/hsv3_rng.h -- o")
        print(f"  JITTER_SAMPLES ajusta-se sozinho para {preciso * 4}.")
    rct, apt = cortes(max(H, 0.01), W=512)
    print()
    print(f"  Cortes de saude para este H (nao binario): RCT {rct}, APT {apt} em 512.")
    print("=" * 62)
    print()
    print("  Lembra-te: estes quatro estimadores sao um subconjunto dos dez do")
    print("  SP 800-90B, e a min-entropia e o minimo de todos. O valor acima")
    print("  e um LIMITE SUPERIOR -- a fonte nunca esta melhor do que isto.")
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
