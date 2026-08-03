# Substitutos do ESP-IDF, só para os testes no PC

O `core/` não inclui um único header do ESP-IDF, e é por isso que os testes no
PC dizem alguma coisa sobre o que corre na placa. O `port/` é o oposto: existe
precisamente para falar com o silício.

Mas a lógica que mais interessa no `port/` — quando é que uma fonte de entropia
é recusada, o que fica na memória depois de a apagar, o que o `put_char` escreve
no framebuffer — não depende do silício nenhum. Depende só de aritmética.

Estes ficheiros são o mínimo para essa lógica compilar e correr no PC:

- `esp_random.h` / `bootloader_random.h` — um gerador controlável, para se poder
  forçar o caso "a fonte está presa" e confirmar que o firmware **recusa**.
- `esp_timer.h` / `esp_cpu.h` — relógios que avançam de forma controlada, para o
  laço do jitter terminar e para se poder simular contagens que nunca variam.
- `driver/i2c_master.h` / `driver/gpio.h` — não fazem nada. O que se testa é o
  framebuffer antes de ser enviado, não o envio.

**Não são um simulador do ESP32-S3.** Um teste que passe aqui não prova que
funciona na placa; prova que a lógica está certa. É a mesma fronteira que o
`core/` já tinha, agora aplicada a mais um bocado do código.

O controlo dos substitutos está em `hsv3_stub_ctl.h`.
