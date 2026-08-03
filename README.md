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
| Invariantes do código + mutações | **feito** (38 invariantes, 18 mutações, todas apanhadas) |
| Testes de saúde da entropia (SP 800-90B) | **feito e testado** — RCT e APT correm durante a recolha |
| Min-entropia medida da fonte de jitter | **por medir na placa** — o instrumento está feito, o número não |
| Firmware | **281 344 bytes**, sha256 `c2f74f3d…` |
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
| jitter | Desvio do RC interno (~136 kHz) contra o cristal de 40 MHz. Independente do bloco RNG da Espressif |
| timing | Contagem de ciclos nos flancos do botão. Centenas de toques humanos, de graça |

O `jitter` e o `timing` são independentes do **bloco RNG** da Espressif — que é
o que interessa contra o bug da Mk3 — mas não são independentes um do outro:
ambos derivam do mesmo cristal de 40 MHz. Três fontes, dois domínios de relógio.

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
| RCT | amostras iguais seguidas — fonte presa | 41 |
| APT | demasiadas iguais numa janela — fonte enviesada | 793 em 1024 |

Os cortes são calculados para uma taxa de falso alarme de 2⁻²⁰, não escolhidos
a olho: `python tools/entropia.py --cortes` reproduz os dois, e há uma
invariante que falha se o `.h` deixar de bater certo com o cálculo.

O teste anterior era "as contagens variaram pelo menos uma em cada oito", que só
apanhava uma fonte completamente morta. Uma fonte a debitar `0101010101…`
passava, e uma que degradasse a meio da recolha passava também.

### O número que ainda é uma suposição

O firmware assume **meio bit de min-entropia por amostra** de jitter e recolhe
2048 amostras para 256 bits. Esse meio bit é uma suposição conservadora, não uma
medição — e é a afirmação mais fraca do projeto inteiro. Está declarada em
[port/hsv3_rng.h](port/hsv3_rng.h) como `HSV3_JITTER_H_MIN`, com o `JITTER_SAMPLES`
derivado dela, para não poder ser esquecida.

Para a medir na tua placa:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" \
       -D HSV3_JITTER_DUMP=1 -B build-medicao build
idf.py -B build-medicao -p COM3 flash monitor > amostras.txt

python tools/entropia.py amostras.txt
```

A build de medição **não gera seeds**. Não é um modo escondido num menu: a
função do despejo nunca regressa, e por isso o compilador elimina a cerimónia
inteira — as funções `entropy_round`, `collect_physical`, `show_hex32` e
`passphrase_typed` não existem nesse binário. Ele fica 72 KB mais pequeno.

O [`tools/entropia.py`](tools/entropia.py) implementa cinco dos dez estimadores
do SP 800-90B. Como a min-entropia é o mínimo de todos, um subconjunto só pode
dar um valor **igual ou maior** que o verdadeiro: o número que sai é um limite
superior otimista. Se já estiver abaixo do assumido, a fonte está pior que isso.
Para um número publicável, corre a
[ferramenta oficial do NIST](https://github.com/usnistgov/SP800-90B_EntropyAssessment).

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
