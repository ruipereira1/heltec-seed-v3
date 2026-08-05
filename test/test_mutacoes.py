"""Parte cada invariante de proposito e confirma que o test_invariantes.py ve.

  python test/test_mutacoes.py

Um teste que nunca falha nao e um teste, e um comentario que custa a correr.
O test_invariantes.py verifica coisas que nao se conseguem provocar sem mexer
no codigo -- a ordem da cerimonia, medidas de ecra, a wordlist partilhada -- e
por isso nunca o vimos a falhar por uma razao real.

Aqui faz-se isso a serio: copia-se a arvore, estraga-se uma coisa de cada vez,
e exige-se que o teste se queixe daquilo exato. Se alguma mutacao passar
despercebida, e essa invariante que nao esta a ser verificada.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# So os ficheiros que o test_invariantes.py le.
FICHEIROS = [
    "main/main.c",
    "port/hsv3_rng.c", "port/hsv3_oled.c", "port/hsv3_buttons.c",
    "core/hsv3_entropy.c", "core/hsv3_wordlist.c",
    "tools/verify.py", "tools/english.txt", "tools/CONFERIR-A-SEED.bat",
    "tools/entropia.py", "port/hsv3_rng.h", "sdkconfig.defaults",
    "CMakeLists.txt", "third_party/MBEDTLS-PIN.txt",
]

# (nome, ficheiro, [(antes, depois), ...], texto que a queixa tem de conter)
MUTACOES = [
    ("o E_chip passa a ser fechado DEPOIS dos lancamentos",
     "main/main.c",
     [("    gather_chip_entropy();\n", ""),
      ("    collect_physical(kind);\n",
       "    collect_physical(kind);\n    gather_chip_entropy();\n")],
     "o E_chip e fechado antes dos lancamentos"),

    ("o compromisso deixa de ser mostrado antes dos dados",
     "main/main.c",
     [('    show_hex32("COMPROMISSO", commit);\n', "")],
     "o compromisso e MOSTRADO antes dos lancamentos"),

    ("uma falha passa a ser um return mudo",
     "main/main.c",
     [("""    if (hsv3_entropy_commit(g_secrets.e_chip, commit) != HSV3_OK) {
        halt_forever("compromisso", "falhou");
    }""",
       """    if (hsv3_entropy_commit(g_secrets.e_chip, commit) != HSV3_OK) {
        return;
    }""")],
     "nenhuma falha acaba em return/continue mudo"),

    ("o wipe deixa de apanhar o acumulador de timing",
     "main/main.c",
     [("    hsv3_rng_timing_wipe();\n", "")],
     "o wipe apanha o acumulador de timing"),

    ("um titulo passa a nao caber na barra",
     "main/main.c",
     [('screen("APAGADO"', 'screen("APAGADO E BEM APAGADO MESMO"')],
     "nenhum titulo passa de"),

    ("uma linha de texto passa a nao caber no ecra",
     "main/main.c",
     [('"Nada ficou gravado"',
       '"Nada ficou gravado nesta placa nem em lado nenhum"')],
     "nenhuma linha de texto passa de"),

    ("passa a escrever-se texto na linha do filete",
     "main/main.c",
     [('    hsv3_oled_text(0, 7, hint);',
       '    hsv3_oled_text(0, 6, hint);')],
     "nada escreve texto na linha 6"),

    ("o firmware passa a escrever em flash",
     "main/main.c",
     [("    hsv3_radio_off();", "    nvs_flash_init();\n    hsv3_radio_off();")],
     "nao escreve em flash nem liga radio"),

    ("a consola volta a ficar ligada",
     "sdkconfig.defaults",
     [("CONFIG_ESP_CONSOLE_NONE=y", "CONFIG_ESP_CONSOLE_NONE=n")],
     "CONFIG_ESP_CONSOLE_NONE=y"),

    ("a wordlist do firmware deixa de ser a do verificador",
     "core/hsv3_wordlist.c",
     [('"abandon"', '"abandom"')],
     "a wordlist do firmware e byte a byte a do verify.py"),

    ("o verify.py deixa de conferir o compromisso antes de imprimir",
     "tools/verify.py",
     [("""        if args.commit:
            check_commit(e_chip, args.commit)
        entropy = combine(e_fis, e_chip)""",
       """        entropy = combine(e_fis, e_chip)""")],
     "main(): o compromisso e conferido antes de imprimir"),

    ("o verify.py ganha um caractere que rebenta numa consola Windows",
     "tools/verify.py",
     [('def sha256(b: bytes) -> bytes:',
       'def sha256(b: bytes) -> bytes:  # acentuação')],
     "verify.py e ASCII puro"),

    ("o verify.py deixa de apanhar excecoes inesperadas",
     "tools/verify.py",
     [("    except Exception as exc:", "    except NotImplementedError as exc:")],
     "verify.py nao deixa escapar nenhuma excecao"),

    ("o compromisso passa a aceitar prefixos curtos de mais",
     "tools/verify.py",
     [("    if len(want) < 32:", "    if len(want) < 0:")],
     "check_commit exige pelo menos 128 bits"),

    # Tirar o guarda, nao so o comentario: a chamada tem de ficar mesmo de
    # fora do #ifdef para isto ser uma fuga. A primeira versao desta mutacao
    # so mexia no comentario e passava despercebida -- com razao.
    ("o despejo de medicao escapa do #ifdef",
     "main/main.c",
     [("""#ifdef HSV3_JITTER_DUMP
    jitter_dump_forever();   /* nao regressa: esta build nao gera seeds */
#endif""",
       "    jitter_dump_forever();")],
     "jitter_dump_forever so existe sob #ifdef"),

    ("o despejo de medicao passa a regressar",
     "main/main.c",
     [("    for (;;) vTaskDelay(pdMS_TO_TICKS(60000));\n}\n#endif",
       "}\n#endif")],
     "jitter_dump_forever nunca regressa"),

    ("o corte RCT deixa de bater com o SP 800-90B",
     "port/hsv3_rng.h",
     [("#define HSV3_RCT_CUTOFF   21", "#define HSV3_RCT_CUTOFF   64")],
     "o corte RCT do firmware bate com o SP 800-90B"),

    ("o corte APT deixa de bater com o SP 800-90B",
     "port/hsv3_rng.h",
     [("#define HSV3_APT_CUTOFF  311", "#define HSV3_APT_CUTOFF  900")],
     "o corte APT do firmware bate com o SP 800-90B"),

    # Este e' o bug real que aconteceu ao ligar a barra de progresso aos
    # gestos de apagar/terminar: o texto da linha 7 ficou comprido de mais e
    # sobrepos-se a barra. So foi apanhado por releitura manual -- esta
    # mutacao prova que, agora, o test_invariantes.py tambem o apanharia.
    ("o texto da linha 7 volta a ser comprido de mais e sobrepoe a barra",
     "main/main.c",
     [('hsv3_oled_text(0, 7, "termina");',
       'hsv3_oled_text(0, 7, "termina a cerimonia agora");')],
     "sobrepunha a barra"),

    # O bug real: o ecra do aquecimento volta a mostrar as duas fraccoes
    # (AQUECER N/64 e o passo N/8) na mesma barra, porque alguem trocou
    # ui_header_bare por ui_header "so' para testar uma coisa" e esqueceu-se
    # de voltar atras.
    ("um dos ecras de fraccao propria volta a mostrar tambem o passo N/8",
     "main/main.c",
     [('snprintf(l, sizeof(l), "AQUECER %u/%u", n, (unsigned)HSV3_TIMING_MIN_EVENTS);\n'
       '        ui_header_bare(l);',
       'snprintf(l, sizeof(l), "AQUECER %u/%u", n, (unsigned)HSV3_TIMING_MIN_EVENTS);\n'
       '        ui_header(l);')],
     "os ecras com fraccao propria usam ui_header_bare"),

    # O bug real que o utilizador apanhou em hardware: o ecra de aviso do
    # compromisso volta a ter o mesmo titulo do ecra que mostra o valor a
    # serio, e em sequencia parecem duas paginas da mesma coisa.
    ("o aviso do compromisso volta a ter o mesmo titulo do valor",
     "main/main.c",
     [('screen("PREPARA-TE", what, "", "Anota ANTES de",',
       'screen("COMPROMISSO", what, "", "Anota ANTES de",')],
     "nenhum titulo se repete dentro de uma ronda de entropia"),
]


def arvore(dst):
    for rel in FICHEIROS:
        d = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, rel), d)


def correr_invariantes(raiz):
    env = dict(os.environ, HSV3_ROOT=raiz)
    r = subprocess.run([sys.executable,
                        os.path.join(ROOT, "test", "test_invariantes.py")],
                       capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


falhas = 0

# Primeiro: sem mutacao nenhuma, tem de passar. Se isto falhar, o resto nao
# significa nada -- estariamos a ver as invariantes a queixarem-se do ruido.
with tempfile.TemporaryDirectory() as tmp:
    arvore(tmp)
    codigo, saida = correr_invariantes(tmp)
    if codigo != 0:
        print("  [X] a arvore intacta ja falha -- as mutacoes nao provam nada")
        print("\n".join("      " + l for l in saida.splitlines()[-15:]))
        sys.exit(1)
    print("  [ok] arvore intacta passa")

for nome, ficheiro, edicoes, esperado in MUTACOES:
    with tempfile.TemporaryDirectory() as tmp:
        arvore(tmp)
        alvo = os.path.join(tmp, ficheiro)
        with open(alvo, encoding="utf-8", errors="surrogateescape") as fh:
            texto = fh.read()

        aplicadas = 0
        for antes, depois in edicoes:
            if antes not in texto:
                print(f"  [X] {nome}")
                print(f"      a mutacao nao encaixa no codigo actual:")
                print(f"      {antes.strip().splitlines()[0][:70]!r}")
                falhas += 1
                break
            texto = texto.replace(antes, depois, 1)
            aplicadas += 1
        else:
            with open(alvo, "w", encoding="utf-8",
                      errors="surrogateescape", newline="") as fh:
                fh.write(texto)

            codigo, saida = correr_invariantes(tmp)
            if codigo == 0:
                print(f"  [X] {nome}")
                print("      o test_invariantes.py NAO deu por isso")
                falhas += 1
            elif esperado not in saida:
                print(f"  [X] {nome}")
                print(f"      falhou, mas por outra razao; esperava ver:")
                print(f"      {esperado!r}")
                falhas += 1
            else:
                print(f"  [ok] {nome}")

print()
if falhas:
    print(f"FALHOU -- {falhas} mutacoes passaram despercebidas")
    sys.exit(1)
print(f"OK -- as {len(MUTACOES)} mutacoes foram todas apanhadas")
print("     as invariantes tem dentes: partir cada uma da queixa, e a certa.")
