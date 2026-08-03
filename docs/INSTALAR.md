# Instalar na placa

## Antes de tudo: a placa não está a ser detetada

Na verificação feita nesta máquina apareceu apenas `COM1`, que é uma porta ACPI
legacy da motherboard — **não é o Heltec**.

Uma Heltec V3 ligada por USB aparece como uma destas:

- `Silicon Labs CP210x USB to UART Bridge (COMx)`
- `USB JTAG/serial debug unit` (USB nativo do ESP32-S3)

Resolve por esta ordem:

1. **Troca o cabo.** É de longe a causa mais comum. Muitos cabos USB-C só têm
   as linhas de alimentação, sem dados. Usa um cabo que saibas que transfere
   ficheiros.
2. **Instala o driver CP210x** da Silicon Labs (procura "CP210x VCP driver").
   Depois desliga e volta a ligar a placa.
3. **Confirma que tem energia** — o LED da placa deve acender.

Para reconfirmar a qualquer momento:

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+|CP210|USB JTAG' } | Select-Object Name
```

---

## Instalar o ESP-IDF

O firmware não compila sem ele. São ~2 GB.

**Windows:** descarrega o *ESP-IDF Windows Installer* (versão **v5.3.2** — é a
que o `Dockerfile` fixa) e escolhe o alvo `esp32s3`. O instalador cria um atalho
"ESP-IDF PowerShell"; usa-o para os comandos abaixo.

Versão mais antiga que 5.2 não serve: o driver `i2c_master` que o OLED usa só
existe a partir daí.

---

## Compilar e gravar

```powershell
cd C:\Users\ruiva\Desktop\heltec-seed-v3
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash        # troca COM5 pela porta real
```

O `idf.py build` corre no fim o `tools/check_no_radio.py`. **Se ele falhar, o
build é recusado** — é intencional: significa que algum símbolo de Wi-Fi,
Bluetooth ou LoRa entrou no firmware, e isso não pode acontecer numa máquina
que gera chaves privadas.

Se a placa não entrar em modo de gravação sozinha: mantém o **PRG** carregado,
carrega e larga o **RST**, e só depois larga o PRG.

---

## Build reprodutível (recomendado antes de usar a sério)

Isto permite-te confirmar que o binário que gravas corresponde ao código-fonte:

```sh
docker build -t heltec-seed-v3 .
docker run --rm -v "$PWD/out:/out" heltec-seed-v3
```

Compara o SHA256 impresso com o de `build/heltec-seed-v3.bin` do teu build
local. Se forem diferentes, alguma coisa no teu ambiente alterou o firmware.

---

## Confirmar que é esse o firmware que a placa corre

O build reprodutível prova que **existe** um binário bom. Não prova que é esse
que está gravado. Sem este passo, "open source" e "build reprodutível" não se
ligam a nada: uma placa que te chegue já gravada, ou um `flash` que corra com
outro ficheiro, passa despercebida.

Lê a flash de volta e compara:

```powershell
# tamanho exato do binário que compilaste
$n = (Get-Item build\heltec-seed-v3.bin).Length

# lê da placa o mesmo número de bytes, a partir do offset da app (0x10000)
python -m esptool --chip esp32s3 -p COM5 read_flash 0x10000 $n lido.bin

(Get-FileHash build\heltec-seed-v3.bin -Algorithm SHA256).Hash
(Get-FileHash lido.bin -Algorithm SHA256).Hash
```

Os dois hashes têm de ser iguais. Se não forem, **não faças a cerimónia** — a
placa não está a correr o firmware que auditaste.

Duas ressalvas honestas:

- Isto atesta a partição da aplicação, não o bootloader. Para atestar o
  bootloader lê `0x0` com o tamanho de `build/bootloader/bootloader.bin` e
  compara da mesma forma.
- Um firmware malicioso já gravado pode, em teoria, mentir na leitura. Este
  passo apanha uma gravação errada ou uma placa recebida já preparada; não
  apanha um atacante que controle o chip por baixo do esptool. Contra isso, o
  que resta é gravar tu próprio a partir de uma placa em branco.

---

## Primeira coisa a fazer depois de gravar

**Imprime primeiro a [folha de cerimónia](folha-de-cerimonia.html)** — abre-a
num browser e Ctrl+P, A4. É onde vais escrever, e a ordem das secções é a ordem
dos ecrãs.

**Não gastes lançamentos de dados a sério na primeira cerimónia.** Faz uma de
teste com uma sequência conhecida e confirma que o resultado bate certo:

1. Corre a cerimónia e anota o `COMPROMISSO` **antes** de introduzir os
   lançamentos — é esse o passo que estás a treinar.
2. Introduz 100 vezes o valor `1`.
3. Anota o `E_CHIP` que aparece na revelação.
4. No PC, clica duas vezes em `tools/CONFERIR-A-SEED.bat` e responde às
   perguntas. (Ou, no terminal: `python tools/verify.py`.)

Se preferires dar tudo de uma vez:

```sh
python tools/verify.py --dice 1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111 --chip <o hex do ecrã> --commit <o hex anotado no passo 1>
```

As 24 palavras têm de ser exatamente as do ecrã, e o script não pode queixar-se
do compromisso. Se estiver tudo certo, o dispositivo está correto e podes fazer
a cerimónia a sério. **Se não estiver, para** — há um bug, e nesse caso o
`verify.py` fez exatamente aquilo para que existe.

Se o `verify.py` disser `COMPROMISSO NAO BATE CERTO`, não é um erro de cópia
benigno: é o sinal de que o `E_chip` mudou depois de o dispositivo ver os teus
lançamentos. Confere primeiro se copiaste bem; se copiaste, deita a placa fora.

Depois da cerimónia real, escreve as palavras e a passphrase no Sparrow
(offline) e confirma que o **fingerprint** e o **primeiro endereço** batem
certo com o ecrã.

Só depois disso é que a seed serve para receber fundos.
