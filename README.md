# HELTEC-SEED-V3

Gerador de seed BIP39 de 256 bits para **Heltec WiFi LoRa 32 V3** (ESP32-S3),
escrito de raiz depois da falha de entropia da Coldcard Mk3 de julho de 2026.

O ponto central não é o dispositivo. É este:

> **Não tens de confiar neste firmware.** Anotas o que o ecrã mostra, corres
> `tools/verify.py` num PC — ele faz as perguntas, não é preciso saber nada de
> informática — e recalcula as mesmas 24 palavras com uma implementação
> independente. Se não baterem certo, não usas a seed.

Foi exatamente essa segunda opinião que faltou à Mk3: um bug no `libngu` fazia
o firmware cair silenciosamente num PRNG fraco (Yasmarang) em vez do TRNG do
STM32, reduzindo a entropia real a ~40 bits. Ninguém deu por isso durante cinco
anos, até serem varridos ~594 BTC de ~500 carteiras a 31/07/2026.

---

## Estado

| Parte | Estado |
|---|---|
| `tools/verify.py` — verificador independente | **feito e testado** (150 verificações contra vetores oficiais) |
| `core/` — entropia, compromisso, BIP39, BIP32, base58, bech32 | **feito e testado** (388 verificações) |
| Confronto C ↔ Python | **feito** (275 campos, 0 divergências) |
| `port/` — RNG, jitter, OLED, rádio desligado | **feito e testado** (32 verificações com substitutos do ESP-IDF) |
| `main/main.c` — máquina de estados da cerimónia | **compila**; a ordem da cerimónia é verificada por invariante |
| Verificação anti-rádio no build | **passa** — zero código de Wi-Fi/BT/LoRa ligado |
| Modo passo a passo do verificador | **feito e testado** (8 casos, incluindo os erros de cópia) |
| Modo de letras grandes no ecrã | **feito e testado** — hex e palavras a corpo duplo, à escolha no arranque |
| Invariantes do código + mutações | **feito** (38 invariantes, 18 mutações, todas apanhadas) |
| Testes de saúde da entropia (SP 800-90B) | **feito e testado** — RCT e APT correm durante a recolha |
| Min-entropia medida da fonte de jitter | **feito** — 1,96 bits/amostra nesta placa; a extração original só via 0,014 (corrigida) |
| Firmware | **281 888 bytes**, sha256 `c3df9b42…` |
| Build reprodutível (Docker + IDF v5.3.2 fixado) | **feito**, por correr |
| Atestação do que está gravado na placa | **feita** — app e bootloader lidos de volta, hashes iguais |

O que está marcado como testado foi mesmo executado nesta máquina. O firmware
está gravado na placa e o que lá está foi confirmado byte a byte contra o
binário compilado. **Mas gravar não é o mesmo que funcionar**: a cerimónia
nunca correu de ponta a ponta em hardware. O núcleo criptográfico e o
verificador, esses, estão testados a sério.

Para instalar: **[docs/INSTALAR.md](docs/INSTALAR.md)**.

---

## Porque é que isto é verificável

```
E_chip   = SHA256( TRNG ‖ jitter ‖ timing )       três fontes do próprio chip
C        = SHA256( E_chip )                       mostrado ANTES dos lançamentos
E_física = SHA256("415263...")                    100 lançamentos de d6 = 258.5 bits
seed     = E_física XOR E_chip                    256 bits
```

**A ordem é a propriedade de segurança, não o XOR.** O chip fecha a metade dele
e mostra o compromisso `C` primeiro. Só depois lanças os dados. No fim revela o
`E_chip`, e o `verify.py` confirma que `SHA256(E_chip)` dá o `C` que anotaste
antes de tocar nos dados.

Sem esse passo, o XOR não protege contra nada. Um firmware ou um TRNG
comprometido que veja os lançamentos primeiro escolhe

```
E_chip = E_física XOR seed_que_o_atacante_quer
```

e a cerimónia inteira sai coerente: os dois hex do ecrã batem certo, o
`verify.py` recalcula, diz OK, o fingerprint confere no Sparrow — e a seed é do
atacante. Reproduzir a conta prova que a aritmética está certa; não prova que o
chip jogou limpo. É o compromisso que prova isso, porque prende o chip a um
valor escolhido quando ainda não podia conhecer os teus lançamentos.

O que continua de fora: um dispositivo malicioso pode sempre abortar e obrigar a
repetir a cerimónia até calhar um resultado que lhe sirva. Custa-lhe uma ronda
inteira de lançamentos por cada bit que enviesa, e dá nas vistas. **Se a placa
abortar repetidamente sem razão, deita-a fora.**

Com o compromisso no papel, a promessa passa a ser verdadeira nos dois sentidos:
se o TRNG estiver comprometido, os dados lançados à mão salvam a seed; se os
dados forem enviesados, o chip salva-a.

As três fontes do chip:

| Fonte | O que é |
|---|---|
| TRNG | `esp_random()` **depois** de `bootloader_random_enable()` — sem isso é pseudo-aleatório, e é essa a família do bug da Mk3 |
| jitter | Ciclos da CPU (cristal de 40 MHz) contados enquanto o RC interno de ~136 kHz bate. Dois osciladores fisicamente separados |
| timing | Contagem de ciclos nos flancos do botão. Centenas de toques humanos, de graça |

**Correção importante.** Até 04/08/2026 o jitter media contra `esp_timer_get_time()`,
e o comentário no código dizia — como este README dizia — que estava a comparar o
RC com o cristal. Não estava: no ESP32-S3 o `esp_timer` corre em cima do systimer,
e `SYSTIMER_CLK_SRC_DEFAULT = SOC_MOD_CLK_XTAL`. Os dois lados da medição saíam do
**mesmo** oscilador.

O que sobrava era ruído de temporização de software — quantização do laço, cache,
e o tique do FreeRTOS a 1 kHz a cair numa amostra em cada 33, que é uma estrutura
periódica e portanto previsível. Agora usa `rtc_time_get()`, que lê mesmo o
contador do RC.

Isto nunca comprometeu nenhuma seed: o `E_chip` é o hash das três fontes, e a seed
é `E_física XOR E_chip` com os 258 bits dos dados a segurar tudo. Um XOR com uma
fonte fraca não enfraquece a outra. Mas era uma promessa que o código não cumpria,
e agora cumpre.

Se **qualquer** uma falhar, a cerimónia aborta. Não há caminho degradado — foi
um caminho degradado silencioso que destruiu a Mk3.

O `timing` obriga a um ecrã de aquecimento (~30 toques) antes dos lançamentos:
tem de estar fechado para o `E_chip` poder ser comprometido, e o compromisso tem
de vir antes do primeiro dado. Esse ecrã é também o único diagnóstico do botão
que existe — com a consola desligada, não há log nenhum para consultar.

### Testes de saúde, durante a recolha

O jitter corre com os dois testes do NIST SP 800-90B §4.4 **enquanto** recolhe,
que é quando ninguém está a olhar:

| Teste | O que apanha | Corte |
|---|---|---|
| RCT | amostras iguais seguidas — fonte presa | 21 |
| APT | demasiadas iguais numa janela — fonte enviesada | 311 em 512 |

Os cortes são calculados para uma taxa de falso alarme de 2⁻²⁰, não escolhidos
a olho: `python tools/entropia.py --cortes` reproduz os dois, e há uma
invariante que falha se o `.h` deixar de bater certo com o cálculo.

O teste anterior era "as contagens variaram pelo menos uma em cada oito", que só
apanhava uma fonte completamente morta. Uma fonte a debitar `0101010101…`
passava, e uma que degradasse a meio da recolha passava também.

### O número: medido, não suposto

**Medido nesta placa a 04/08/2026, com 4000 amostras reais:** 1,96 bits de
min-entropia por amostra (o mínimo dos quatro estimadores do SP 800-90B que
correm sobre a amostra inteira). O firmware assume **1 bit**, o que deixa quase
2× de margem. Está declarado em [port/hsv3_rng.h](port/hsv3_rng.h) como
`HSV3_JITTER_H_MIN`, com o `JITTER_SAMPLES` derivado dele.

Isto substitui uma correção que teve de acontecer no mesmo dia. A extração
original guardava só a **paridade** de cada contagem — 1 bit de 32. A medição
mostrou porquê isso não servia:

```
bit 0 (o que se guardava)   0,95% de uns   0,014 bits/amostra   MORTO
amostra de 16 bits inteira                 1,96  bits/amostra   viva
```

O laço de espera gasta sempre um número par de ciclos por iteração, por isso o
bit 0 nunca varia — não porque a fonte estivesse fraca, mas porque a extração
deitava fora os 31 bits que continham quase toda a variação. Corrigido: agora
entram os 16 bits baixos de cada amostra no acumulador, e os testes de saúde
olham para a amostra inteira, não para 1 bit dela.

Para repetir a medição na tua placa:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" \
       -D HSV3_JITTER_DUMP=1 -B build-medicao build
idf.py -B build-medicao -p COM3 flash monitor > amostras.txt

python tools/entropia.py amostras.txt
```

A build de medição **não gera seeds**. Não é um modo escondido num menu: a
função do despejo nunca regressa, e por isso o compilador elimina a cerimónia
inteira — as funções `entropy_round`, `collect_physical`, `show_hex32` e
`passphrase_typed` não existem nesse binário. Ele fica ~73 KB mais pequeno.

O [`tools/entropia.py`](tools/entropia.py) implementa cinco dos dez estimadores
do SP 800-90B — quatro correm sobre qualquer alfabeto (incluindo os 16 bits da
amostra); o quinto, Markov, só serve para bits e por isso fica de fora do
número principal, reportado à parte como diagnóstico. Como a min-entropia é o
mínimo de todos os dez do standard, um subconjunto só pode dar um valor
**igual ou maior** que o verdadeiro: o número que sai é um limite superior
otimista. Se já estiver abaixo do assumido, a fonte está pior que isso. Para um
número publicável, corre a
[ferramenta oficial do NIST](https://github.com/usnistgov/SP800-90B_EntropyAssessment).
O silício não é todo igual — repete a medição na tua placa antes de confiar
neste número.

---

## Correr os testes

```sh
# uma vez, com rede: descarrega vetores oficiais e a wordlist (hash verificado)
python tools/fetch_vectors.py
python tools/gen_wordlist.py
python tools/gen_vectors.py

# depois, sempre: corre tudo (precisa de: python -m pip install ziglang)
python test/correr_tudo.py
```

```
  vetores oficiais BIP39/BIP32/BIP84 + compromisso   ok        5.3s
  nucleo em C e camada port/ (zig cc)                ok        1.0s
  as duas implementacoes concordam                   ok       18.6s
  modo passo a passo do verificador                  ok        5.4s
  invariantes do codigo                              ok        0.1s
  as invariantes tem dentes (mutacoes)               ok        1.5s
  estimador de min-entropia (SP 800-90B)             ok        1.3s

OK -- a bateria toda passa (931 verificacoes)
```

O que cada um cobre:

| Bateria | O que apanha |
|---|---|
| [`verify.py --self-test`](tools/verify.py) | 150 verificações contra vetores oficiais. Duas montam o ataque adaptativo do `E_chip` e confirmam que passa sem compromisso e é apanhado com ele |
| [`test_core.c`](test/test_core.c) | 393 verificações do núcleo: BIP39, BIP32, BIP84, entropia fail-closed, compromisso, fontes com padrões presos |
| [`test_port.c`](test/test_port.c) | 38 verificações de `port/`, com substitutos do ESP-IDF: TRNG preso, jitter congelado, os testes de saúde RCT/APT, o limpar do acumulador de timing, o framebuffer do OLED |
| [`crosscheck.py`](tools/crosscheck.py) | 275 campos, C contra Python, em cerimónias aleatórias |
| [`test_verify_interativo.py`](test/test_verify_interativo.py) | 8 casos do modo passo a passo, quase todos erros de cópia reais |
| [`test_invariantes.py`](test/test_invariantes.py) | 38 propriedades que se lêem no código: a ordem da cerimónia, nenhum `return` mudo, nada em flash, o que cabe no ecrã, a wordlist partilhada, os cortes de saúde |
| [`entropia.py --self-test`](tools/entropia.py) | O estimador de min-entropia reage como deve a sequências de entropia conhecida |
| [`test_mutacoes.py`](test/test_mutacoes.py) | Parte cada invariante de propósito e exige que o teste anterior se queixe — **da coisa certa** |

A última é a que impede o resto de apodrecer. Um teste que nunca falha não é um
teste; o `test_mutacoes.py` estraga o código de 18 maneiras — troca a ordem do
compromisso, mete um `return` mudo, liga a consola, muda uma palavra da
wordlist, desalinha um corte do SP 800-90B — e exige que cada uma seja apanhada.
Já encontrou dois buracos a sério: um filtro que deixava passar linhas longas de
texto, e um emparelhamento de aspas que inventava falsos positivos.

---

## Verificar uma cerimónia

> **Anota o compromisso antes de tocar nos dados.** Se o escreveres depois — ou
> não o escreveres de todo — o chip deixa de estar preso a coisa nenhuma e a
> verificação passa a provar só que a aritmética está certa.

> **Escreve cada lançamento no papel à medida que o fazes.** O ecrã mostra
> `SHA256(lançamentos)`, não os lançamentos. Sem a sequência exata dos 100
> dígitos não consegues correr o `verify.py`, e perdes a única coisa que torna
> este projeto diferente de confiar cegamente no firmware.

**Imprime a [folha de cerimónia](docs/folha-de-cerimonia.html) antes de
começares.** Abre o ficheiro num browser e carrega em Ctrl+P. Um quadrado por
carácter, 100 casas numeradas para os lançamentos, e as secções pela ordem da
cerimónia — o compromisso é a secção 1, os dados são a 2. Quem preencher de
cima para baixo faz a coisa certa sem ter de se lembrar porquê.

Uma folha em branco é onde os erros de cópia nascem.

Do ecrã do dispositivo, anota o `COMPROMISSO` (antes), e depois `E_física`,
`E_chip` e as 24 palavras. Num PC **offline**, clica duas vezes em
**`tools/CONFERIR-A-SEED.bat`** — ou, se preferires o terminal, corre
`python tools/verify.py` sem argumentos.

Em qualquer dos casos o programa faz as perguntas, uma linha de cada vez, na
mesma forma em que o ecrã mostrou os valores:

```
      linha 1 > a41f 08c2 9e33 7b50
      linha 2 > ...
```

Não é preciso saber nada de informática, e cada erro é apanhado na linha em que
foi escrito — dígitos a mais, dígitos a menos, um `7` num dado, ou a letra `o`
onde devia estar um zero.

Quem quiser tudo de uma vez continua a poder:

```sh
python tools/verify.py --dice 4152631... --chip <64 hex> --commit <hex>
```

Bastam os primeiros 32 hex do compromisso — são 128 bits a prender o chip. Se o
`E_chip` revelado não corresponder, o programa **para** e não imprime seed
nenhuma.

Compara palavra a palavra. Depois escreve as 24 palavras e a passphrase no
Sparrow e confirma que o **fingerprint** e o **primeiro endereço** batem certo
com o que o dispositivo mostrou.

**Se alguma coisa não bater — para. Não uses a seed.**

### O papel da verificação é uma segunda cópia da seed

Os 100 dígitos mais o `E_chip` **são** a seed, e sem o checksum do BIP39 a
proteger de erros de cópia. Enquanto existirem, existem duas cópias do segredo.

- Verifica num PC sem rede, e não guardes o comando no histórico da shell.
- Depois de verificado: ou destróis esse papel, ou o tratas exatamente como
  tratas as 24 palavras. Não há meio termo.
- O compromisso, esse, não é secreto — é só um hash. Podes guardá-lo à parte.

---

## Passphrase

Três caminhos, escolhidos no dispositivo. Nenhum serve toda a gente.

| Opção | O que é | O custo |
|---|---|---|
| **Sem passphrase** | As 24 palavras são tudo | Quem encontrar a folha leva as moedas |
| **Gerada** | 24 palavras BIP39 (264 bits) de uma segunda ronda, com compromisso próprio | Um segundo segredo do tamanho de uma seed para guardar |
| **Escrita por mim** | Até 63 caracteres ASCII, escolhidos no ecrã | Sem checksum: um caractere trocado abre outra carteira, em silêncio |

Porquê 24 palavras na opção gerada: o BIP39 usa PBKDF2-HMAC-SHA512 com apenas
2048 iterações, o que é barato de atacar (~10⁹ tentativas/s numa GPU). Se alguém
encontrar a folha com as 24 palavras da seed, só lhe falta a passphrase. 4
palavras (44 bits) caem em horas. Acima de ~128 bits não há ganho real — o
secp256k1 tem esse teto — mas como é o dispositivo que a gera, não custa nada.

Na opção escrita por ti, a única rede de segurança é o confronto final: o
**fingerprint** e o **primeiro endereço** que o dispositivo mostra têm de bater
certo com o Sparrow. Se um caractere estiver errado, não bate — e é aí que se
apanha, não depois.

**Guarda-a em local diferente da seed.** Perdê-la é perder as moedas, sem
recurso — e isso é muito mais provável do que alguém partir 128 bits.

---

## O que este dispositivo não é

- **Não tem secure element.** Não resiste a extração física como uma Coldcard.
  A resposta é ser stateless: nada é escrito em flash, e depois de desligar não
  há lá nada para extrair.
- **Não assina.** Sem câmara e sem slot SD, um signatário PSBT obrigaria a
  meter PSBTs por USB — o que deitaria fora o air gap. Assinar fica para um
  SeedSigner ou uma Coldcard Mk4 restaurados a partir desta seed.
- **Não protege contra análise de consumo ou emissões EM.** Fora de âmbito.
  A mitigação é uma cerimónia única e privada.
- **Não prova, por si só, que a placa corre o firmware auditado.** Um build
  reprodutível prova que *existe* um binário bom; não prova que é esse que está
  gravado. Falta o passo de ler a flash de volta e comparar o hash — está em
  [docs/INSTALAR.md](docs/INSTALAR.md), e sem ele os pontos "open source" e
  "build reprodutível" ficam pendurados no ar.
- **Não tem zero falhas.** Ninguém pode prometer isso. O que se pode fazer é
  tirar o dispositivo do caminho crítico da confiança — e é isso que o
  compromisso mais o `verify.py` fazem.

---

## Estrutura

```
core/     lógica portátil, sem hardware. Compila no PC e no ESP32
port/     camada ESP32-S3: RNG, jitter, botões, OLED, rádio em reset
main/     máquina de estados da cerimónia
test/     testes contra vetores oficiais + build.sh (zig cc)
tools/    verify.py, crosscheck.py, geradores, check_no_radio.py
```

`core/` não inclui um único header do ESP-IDF. É por isso que os testes no PC
dizem alguma coisa sobre o que corre na placa.
