"""Invariantes que nenhum teste unitario apanha, verificadas sobre o codigo.

  python test/test_invariantes.py

Ha propriedades neste projeto que nao sao uma funcao a devolver um valor -- sao
a ORDEM em que as coisas acontecem, ou uma medida que tem de caber num ecra.
Nao se testam chamando codigo; testam-se lendo-o.

A que mais importa e a primeira: o compromisso do chip tem de ser fechado ANTES
dos lancamentos. Se alguem trocar essas duas linhas por conveniencia, todos os
outros testes continuam a passar -- as 24 palavras saem certas, o verify.py diz
OK, o fingerprint bate no Sparrow -- e o projeto perde a unica coisa que tem.
Este ficheiro existe para essa troca nao passar em silencio.
"""
import hashlib
import io
import os
import re
import sys

# HSV3_ROOT permite apontar isto a uma copia da arvore. E' o que o
# test_mutacoes.py usa para partir cada invariante de proposito e confirmar que
# este ficheiro se queixa -- um teste que nunca falha e pior do que nenhum.
ROOT = os.environ.get("HSV3_ROOT") or os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))

falhas = []
checks = 0


def ver(nome, cond, detalhe=""):
    global checks
    checks += 1
    if not cond:
        falhas.append(f"{nome}\n      {detalhe}" if detalhe else nome)
        print(f"  [X] {nome}")
        if detalhe:
            for l in detalhe.splitlines():
                print(f"      {l}")
    return bool(cond)


def ler(rel):
    with io.open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def literais_c(src):
    """(posicao, conteudo) de cada literal de string do ficheiro.

    Emparelhar aspas com um regex nao serve: numa linha como

        snprintf(l, n, "reset %s", why ? "SIM" : "NAO");

    o regex apanha o que esta ENTRE o fim de um literal e o inicio do
    seguinte, e depois queixa-se de que ", why ? " tem 28 colunas. Foi isso
    que aconteceu -- por isso vai a mao, com comentarios e escapes tratados.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            i = src.find("\n", i)
            if i < 0:
                break
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            i = src.find("*/", i + 2)
            if i < 0:
                break
            i += 2
        elif c == "'":
            i += 1
            while i < n and src[i] != "'":
                i += 2 if src[i] == "\\" else 1
            i += 1
        elif c == '"':
            ini, i = i + 1, i + 1
            while i < n and src[i] != '"':
                i += 2 if src[i] == "\\" else 1
            out.append((ini, src[ini:i]))
            i += 1
        else:
            i += 1
    return out


def corpo(src, assinatura):
    """O corpo de uma funcao C, contando chavetas."""
    i = src.index(assinatura)
    i = src.index("{", i)
    nivel, j = 0, i
    while j < len(src):
        if src[j] == "{":
            nivel += 1
        elif src[j] == "}":
            nivel -= 1
            if nivel == 0:
                return src[i:j + 1]
        j += 1
    raise AssertionError(f"nao encontrei o fim de {assinatura}")


MAIN = ler("main/main.c")


# ---------------------------------------------------------------------------
print("A ORDEM DA CERIMONIA")
# ---------------------------------------------------------------------------

ronda = corpo(MAIN, "static void entropy_round(")

i_chip = ronda.find("gather_chip_entropy()")
i_commit = ronda.find("hsv3_entropy_commit(")
i_mostra = ronda.find('show_hex32("COMPROMISSO"')
i_dados = ronda.find("collect_physical(")
i_revela = ronda.find('show_hex32("E_CHIP"')

ver("o E_chip e fechado antes dos lancamentos",
    0 <= i_chip < i_dados,
    "gather_chip_entropy() tem de vir antes de collect_physical().\n"
    "Se vier depois, um chip comprometido escolhe\n"
    "  E_chip = E_fisica XOR seed_do_atacante\n"
    "e a cerimonia inteira sai coerente na mesma.")

ver("o compromisso e calculado antes dos lancamentos",
    0 <= i_commit < i_dados)

ver("o compromisso e MOSTRADO antes dos lancamentos",
    0 <= i_mostra < i_dados,
    "de nada serve calcular o compromisso se o utilizador so o ve depois:\n"
    "o que prende o chip e estar anotado em papel antes do primeiro dado.")

ver("o E_chip so e revelado depois dos lancamentos",
    i_dados < i_revela,
    "revelar o E_chip antes dos dados deixaria o utilizador escolher os\n"
    "lancamentos em funcao dele, que e o mesmo ataque ao contrario.")

ver("a ronda pede confirmacao de que o compromisso ficou anotado",
    'screen("ESTA ANOTADO?"' in ronda)


# ---------------------------------------------------------------------------
print("FAIL-CLOSED: nenhum caminho degradado em silencio")
# ---------------------------------------------------------------------------

# Nenhuma verificacao de erro pode acabar num return/continue mudo. Procura-se
# "!= HSV3_OK" e confirma-se que o que se segue para a cerimonia.
mudos = []
for m in re.finditer(r"!=\s*HSV3_OK\)\s*(?:\{)?\s*([a-z_]+)", MAIN):
    seguinte = m.group(1)
    if seguinte in ("return", "continue", "break"):
        linha = MAIN[:m.start()].count("\n") + 1
        mudos.append(f"main.c:{linha} -> {seguinte}")
ver("nenhuma falha acaba em return/continue mudo", not mudos,
    "\n".join(mudos))

# O halt tem de apagar tudo ANTES de mostrar seja o que for.
halt = corpo(MAIN, "static void halt_forever(")
ver("halt_forever apaga antes de mostrar",
    halt.index("wipe_everything()") < halt.index("screen("))

ver("o wipe apanha o acumulador de timing, que vive fora do g_secrets",
    "hsv3_rng_timing_wipe()" in corpo(MAIN, "static void wipe_everything("))


# ---------------------------------------------------------------------------
print("NADA EM FLASH, NADA PELO CABO")
# ---------------------------------------------------------------------------

proibido = ["nvs_", "esp_partition_", "fopen(", "esp_vfs_", "esp_ota_",
            "esp_wifi_", "esp_bt_", "esp_bluedroid_"]
for fonte in ("main/main.c", "port/hsv3_rng.c", "port/hsv3_oled.c",
              "port/hsv3_buttons.c", "core/hsv3_entropy.c"):
    txt = ler(fonte)
    achados = [p for p in proibido if p in txt]
    ver(f"{fonte} nao escreve em flash nem liga radio", not achados,
        f"encontrado: {achados}")

sdk = ler("sdkconfig.defaults")
for chave in ("CONFIG_ESP_CONSOLE_NONE=y", "CONFIG_LOG_DEFAULT_LEVEL_NONE=y"):
    ver(f"sdkconfig.defaults tem {chave}", chave in sdk,
        "com a consola ligada, um coredump numa maquina onde a seed vive em\n"
        "SRAM e um despejo da seed para o cabo USB.")


# ---------------------------------------------------------------------------
print("O QUE CABE NO ECRA")
# ---------------------------------------------------------------------------

COLS = 21
# a barra do titulo perde 1 coluna de margem, 1 de separacao e 3 do passo N/8
TITULO_MAX = COLS - 3 - 3

longos = []
for m in re.finditer(r'\b(?:screen|ui_header)\(\s*"([^"]*)"', MAIN):
    if len(m.group(1)) > TITULO_MAX:
        linha = MAIN[:m.start()].count("\n") + 1
        longos.append(f'main.c:{linha}  {len(m.group(1))} col  "{m.group(1)}"')
ver(f"nenhum titulo passa de {TITULO_MAX} colunas", not longos, "\n".join(longos))

# Linhas de conteudo e de dica.
#
# O unico bloco de literais que nao sao texto de ecra sao as tabelas de
# caracteres da passphrase. Excluem-se pela posicao no ficheiro, nao pela
# forma: um filtro por forma ("ignora strings compridas sem espacos") era largo
# de mais e deixava passar linhas de texto a serio -- foi o test_mutacoes.py
# que deu por isso.
ini = MAIN.index("static const char *const PASS_GROUPS[]")
fim = MAIN.index("#define PASS_NGROUPS")

longas = []
for pos, s in literais_c(MAIN):
    if len(s) <= COLS:
        continue
    if ini <= pos < fim:            # tabela de caracteres, nao e' texto de ecra
        continue
    if "%" in s or "\\" in s:       # formato: o comprimento real nao se sabe aqui
        continue
    if "hsv3_" in s:                # nomes de funcao em mensagens de erro
        continue
    linha = MAIN[:pos].count("\n") + 1
    longas.append(f'main.c:{linha}  {len(s)} col  "{s}"')
ver(f"nenhuma linha de texto passa de {COLS} colunas", not longas,
    "\n".join(longas))

# O ecra de hex tem de caber exatamente: 1 + 1 + 4x4 + 3 espacos = 21.
exemplo = "1 " + " ".join(["abcd"] * 4)
ver("a linha do ecra de hex mede exatamente 21 colunas", len(exemplo) == COLS,
    f"mediu {len(exemplo)}")

# A regra do layout: nada de texto na linha 6, onde passa o filete.
ver("nada escreve texto na linha 6 (onde esta o filete)",
    not re.search(r"hsv3_oled_text(?:_inv)?\(\s*[^,]+,\s*6\s*,", MAIN),
    "o put_char do driver ATRIBUI o byte da pagina; texto na linha 6\n"
    "apagaria o filete. Ha um teste em test_port.c que prova isso.")


# ---------------------------------------------------------------------------
print("A BUILD DE MEDICAO NAO GERA SEEDS")
# ---------------------------------------------------------------------------

# Tudo o que e' do despejo tem de estar dentro de #ifdef HSV3_JITTER_DUMP.
# Se escapar uma linha, a build normal ganha codigo que nao devia ter.
def dentro_de_ifdef(src, simbolo):
    dentro, fora = False, []
    for n, linha in enumerate(src.splitlines(), 1):
        t = linha.strip()
        if t.startswith("#ifdef HSV3_JITTER_DUMP") or t.startswith("#if defined(HSV3_JITTER_DUMP"):
            dentro = True
            continue
        if t.startswith("#endif") and dentro:
            dentro = False
            continue
        if simbolo in t and not dentro and not t.startswith(("*", "/*", "//", "#")):
            fora.append(f"linha {n}: {t[:64]}")
    return fora

for fonte, simbolo in (("main/main.c", "jitter_dump_forever"),
                       ("port/hsv3_rng.c", "hsv3_rng_jitter_raw"),
                       ("port/hsv3_rng.h", "hsv3_rng_jitter_raw")):
    fugas = dentro_de_ifdef(ler(fonte), simbolo)
    ver(f"{fonte}: {simbolo} so existe sob #ifdef", not fugas, "\n".join(fugas))

dump = corpo(MAIN, "static void jitter_dump_forever(")
ver("jitter_dump_forever nunca regressa",
    "for (;;)" in dump,
    "e' isto que faz o compilador eliminar a cerimonia inteira desta build:\n"
    "sem o laco infinito, o codigo que gera seeds continuava alcancavel.")

ver("o CMake avisa quando a build de medicao esta ligada",
    "HSV3_JITTER_DUMP ligado" in ler("CMakeLists.txt"))


# ---------------------------------------------------------------------------
print("OS NUMEROS DA FONTE DE JITTER")
# ---------------------------------------------------------------------------

RNG_H = ler("port/hsv3_rng.h")
sys.path.insert(0, os.path.join(ROOT, "tools"))

# Os cortes de saude no firmware tem de ser os que o SP 800-90B da para a
# min-entropia assumida. Estao escritos a mao no .h; aqui recalculam-se.
try:
    import entropia
    num = int(re.search(r"HSV3_JITTER_H_MIN_NUM\s+(\d+)", RNG_H).group(1))
    den = int(re.search(r"HSV3_JITTER_H_MIN_DEN\s+(\d+)", RNG_H).group(1))
    H = num / den
    rct_h = int(re.search(r"HSV3_RCT_CUTOFF\s+(\d+)", RNG_H).group(1))
    apt_h = int(re.search(r"HSV3_APT_CUTOFF\s+(\d+)", RNG_H).group(1))
    rct_c, apt_c = entropia.cortes(H)
    ver(f"o corte RCT do firmware bate com o SP 800-90B (H={H})",
        rct_h == rct_c, f"no .h {rct_h}, calculado {rct_c}")
    ver(f"o corte APT do firmware bate com o SP 800-90B (H={H})",
        apt_h == apt_c, f"no .h {apt_h}, calculado {apt_c}")
except (ImportError, AttributeError) as exc:
    ver("os cortes do firmware sao verificaveis", False, str(exc))

ver("o JITTER_SAMPLES deriva do H assumido, nao e um numero solto",
    "HSV3_JITTER_H_MIN_DEN / HSV3_JITTER_H_MIN_NUM" in ler("port/hsv3_rng.c"))

ver("o .h diz como medir o H na placa",
    "tools/entropia.py" in RNG_H,
    "o numero e uma suposicao ate alguem o medir; o caminho para o medir\n"
    "tem de estar ao lado dele, nao num ficheiro qualquer.")


# ---------------------------------------------------------------------------
print("A BIBLIOTECA CRIPTOGRAFICA E A QUE FOI FIXADA")
# ---------------------------------------------------------------------------

# O mbedTLS nao esta no repositorio, e' clonado. "clona a v3.6.2" nao chega --
# uma tag pode ser movida, um commit nao. Sem esta verificacao, uma arvore
# local diferente gerava seeds com outra biblioteca sem ninguem dar por isso.
pin = os.path.join(ROOT, "third_party", "MBEDTLS-PIN.txt")
if not os.path.exists(pin):
    ver("third_party/MBEDTLS-PIN.txt existe", False)
else:
    esperado = re.search(r"^commit\s+([0-9a-f]{40})", ler("third_party/MBEDTLS-PIN.txt"),
                         re.M).group(1)
    mb = os.path.join(ROOT, "third_party", "mbedtls")
    if not os.path.isdir(os.path.join(mb, ".git")):
        # Numa arvore sem o clone (a copia que o test_mutacoes.py faz, por
        # exemplo) nao ha o que comparar. Nao e' falha.
        print("      (sem clone do mbedTLS nesta arvore -- nada a comparar)")
    else:
        import subprocess
        r = subprocess.run(["git", "-C", mb, "rev-parse", "HEAD"],
                           capture_output=True, text=True)
        obtido = r.stdout.strip()
        ver("o clone do mbedTLS esta no commit fixado", obtido == esperado,
            f"fixado  {esperado}\nno disco {obtido}\n"
            f"corre:  git -C third_party/mbedtls checkout {esperado}")


# ---------------------------------------------------------------------------
print("A WORDLIST E A MESMA NOS DOIS LADOS")
# ---------------------------------------------------------------------------

CANONICO = "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda"
with open(os.path.join(ROOT, "tools", "english.txt"), "rb") as fh:
    bruto = fh.read().replace(b"\r\n", b"\n")
ver("english.txt e a wordlist BIP39 oficial",
    hashlib.sha256(bruto).hexdigest() == CANONICO,
    f"obtido {hashlib.sha256(bruto).hexdigest()}")

palavras_txt = bruto.decode("ascii").split()
palavras_c = re.findall(r'"([a-z]+)"', ler("core/hsv3_wordlist.c"))
ver("core/hsv3_wordlist.c tem 2048 palavras", len(palavras_c) == 2048,
    f"tem {len(palavras_c)}")
ver("a wordlist do firmware e byte a byte a do verify.py",
    palavras_c == palavras_txt,
    "sao geradas do mesmo ficheiro por tools/gen_wordlist.py -- se divergirem,\n"
    "o firmware e o verificador mostram palavras diferentes para a mesma seed.")


# ---------------------------------------------------------------------------
print("O VERIFICADOR")
# ---------------------------------------------------------------------------

V = ler("tools/verify.py")

nao_ascii = sorted({c for c in V if ord(c) > 126})
ver("verify.py e ASCII puro", not nao_ascii,
    f"caracteres: {nao_ascii}\n"
    "um UnicodeEncodeError numa consola Windows a meio de uma verificacao\n"
    "seria uma falha a serio.")

ver("verify.py nao deixa escapar nenhuma excecao",
    "except Exception as exc:" in V and "Nao uses a seed com base nesta" in V)

ver("sem argumentos, verify.py pergunta em vez de exigir flags",
    "return interativo(wordlist)" in V)


def func_py(src, nome):
    """O corpo de uma funcao Python de nivel superior."""
    i = src.index(f"\ndef {nome}(")
    j = src.find("\ndef ", i + 1)
    return src[i:j if j > 0 else len(src)]


# Os dois modos tem de conferir o compromisso antes de imprimir seja o que for.
# Um relatorio impresso e' um relatorio que se le -- e quem le as 24 palavras
# ja nao volta atras para ver se havia um aviso mais acima.
for modo in ("interativo", "main"):
    b = func_py(V, modo)
    ver(f"{modo}(): o compromisso e conferido antes de imprimir",
        "check_commit(" in b and b.index("check_commit(") < b.index("report("),
        "check_commit tem de correr antes de report()")

ver("check_commit exige pelo menos 128 bits", "len(want) < 32" in V)

ver("o .bat de dois cliques existe",
    os.path.exists(os.path.join(ROOT, "tools", "CONFERIR-A-SEED.bat")))


# ---------------------------------------------------------------------------
print()
if falhas:
    print(f"FALHOU -- {len(falhas)} de {checks} invariantes")
    sys.exit(1)
print(f"OK -- todas as {checks} invariantes se mantem")
print("     ordem da cerimonia, fail-closed, nada em flash,")
print("     medidas do ecra, wordlist partilhada, verificador.")
