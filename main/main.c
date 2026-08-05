/* HELTEC-SEED-V3 -- cerimonia de geracao de seed.
 *
 * Ordem das coisas, e porque essa ordem importa:
 *
 *   1. radio_off()   antes de tudo. O SX1262 fica em reset antes de existir
 *                    qualquer segredo em memoria.
 *   2. rng_init()    tem de correr antes de qualquer uso do ADC, e o seu
 *                    insucesso PARA a cerimonia. Sem isto o esp_random() do
 *                    ESP32-S3 devolve pseudo-aleatorios sem se queixar -- a
 *                    mesma forma do bug que destruiu a Coldcard Mk3.
 *   3. aquecimento   toques suficientes para fechar a fonte de timing, que e'
 *                    uma das tres que entram no E_chip.
 *   4. E_chip        fechado e COMPROMETIDO no ecra, antes de haver um unico
 *                    lancamento. Esta ordem e' a propriedade de seguranca mais
 *                    importante do ficheiro: ver core/hsv3_entropy.h. Um chip
 *                    que so escolhesse a sua metade depois de ver os dados
 *                    podia escolher a seed inteira, e o verify.py diria OK.
 *   5. lancamentos   so agora, com o compromisso ja em papel.
 *   6. revelacao     E_chip em claro, para o verify.py confirmar que bate certo
 *                    com o compromisso anotado no passo 4.
 *
 * Nada e' escrito em flash. Os segredos vivem no g_secrets abaixo e sao
 * apagados em todos os caminhos de saida.
 */

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hsv3_bech32.h"
#include "hsv3_bip32.h"
#include "hsv3_bip39.h"
#include "hsv3_board.h"
#include "hsv3_buttons.h"
#include "hsv3_crypto.h"
#include "hsv3_entropy.h"
#include "hsv3_oled.h"
#include "hsv3_rng.h"
#include "hsv3_wordlist.h"

void hsv3_radio_off(void);

/* --- experiencia: o RST pode servir de segundo botao? -------------------- */
/*
 * A duvida concreta e' se a memoria RTC sobrevive a um reset pelo pino EN (que
 * e' o que o botao RST faz). Se sobreviver, o firmware pode guardar o estado da
 * cerimonia aqui, detetar ESP_RST_EXT no arranque e retomar -- e nesse caso o
 * RST passa a ser um segundo botao utilizavel. Se nao sobreviver, cada toque no
 * RST apaga os lancamentos ja introduzidos e a ideia morre.
 *
 * RTC_NOINIT_ATTR nao e' inicializada no arranque de proposito: e' isso que
 * permite ver se o conteudo aguentou.
 */
#define RTC_PROBE_MAGIC 0x48535633u   /* "HSV3" */
RTC_NOINIT_ATTR static uint32_t s_rtc_magic;
RTC_NOINIT_ATTR static uint32_t s_rtc_boots;

/* O resultado tem de aparecer no ecra, nao no log: a build de producao tem
 * CONFIG_ESP_CONSOLE_NONE e CONFIG_LOG_DEFAULT_LEVEL_NONE, por isso um
 * ESP_LOGW aqui nao chega a lado nenhum na placa a serio. */
static int s_rtc_survived;

/* Tudo o que e' secreto vive aqui, e so aqui. */
static struct {
    hsv3_phys_t  phys;
    hsv3_chip_t  chip;
    uint8_t      e_phys[32];
    uint8_t      e_chip[32];
    uint8_t      entropy[32];
    uint8_t      pass_entropy[32];
    uint8_t      seed[64];
    char         mnemonic[HSV3_MNEMONIC_MAX];
    char         passphrase[HSV3_MNEMONIC_MAX];
    hsv3_hdkey_t master;
} g_secrets;

static void wipe_everything(void)
{
    hsv3_wipe(&g_secrets, sizeof(g_secrets));
    /* O acumulador de timing e' estatico dentro de port/hsv3_rng.c e nao cabe
     * no g_secrets. Sem esta chamada sobrevivia a tudo o resto. */
    hsv3_rng_timing_wipe();
}

/* --- gramatica visual ---------------------------------------------------- */
/*
 * Todos os ecras da cerimonia obedecem a mesma divisao. Nao e' decoracao: numa
 * cerimonia que nao se pode pausar nem repetir, o utilizador tem de saber
 * sempre, sem pensar, onde esta e o que o botao faz.
 *
 *   linha 0     barra invertida: titulo a esquerda, passo N/8 a direita
 *               (o titulo tem 15 colunas uteis -- o resto e' cortado)
 *   linha 1     vazia (respira)
 *   linhas 2-5  conteudo -- quatro linhas de 21 caracteres
 *   linha 6     vazia, atravessada pelo filete a y=54
 *   linha 7     o que o botao faz. Sempre presente, sempre no mesmo sitio
 *
 * REGRA: nao escrever texto na linha 6. O put_char do driver ATRIBUI o byte da
 * pagina em vez de o combinar, por isso texto nessa linha apagaria o filete.
 *
 * EXCECAO DELIBERADA: cinco ecras -- DADOS/MOEDA a contar, AQUECER, PASS e as
 * paginas de SEED/PASSPHRASE -- tem titulo com fraccao propria (12/100,
 * 24/64...). Nesses usa-se ui_header_bare()/screen_bare(), sem o passo N/8,
 * porque duas fraccoes na mesma barra e' o tipo de coisa que so' se confunde
 * ao vivo, nao ao ler o codigo: confirmado em hardware a 05/08/2026.
 * Ver ui_header_bare() para o porque completo.
 */

/* Os oito passos da cerimonia, tal como aparecem no canto da barra:
 *   1 arranque  2 aquecimento  3 modo  4 entropia
 *   5 palavras  6 passphrase  7 resultado  8 apagar                     */
#define HSV3_STEPS 8
static int g_step;   /* 0 = ainda sem numeracao (antes do arranque) */

static void ui_header(const char *title)
{
    char bar[HSV3_OLED_COLS + 1];
    char step[12];
    size_t tlen, slen;

    memset(bar, ' ', HSV3_OLED_COLS);
    bar[HSV3_OLED_COLS] = '\0';

    slen = (size_t)snprintf(step, sizeof(step), "%d/%d", g_step, HSV3_STEPS);
    tlen = strlen(title);
    if (tlen > HSV3_OLED_COLS - slen - 3) tlen = HSV3_OLED_COLS - slen - 3;
    memcpy(bar + 1, title, tlen);
    if (g_step > 0) memcpy(bar + HSV3_OLED_COLS - 1 - slen, step, slen);

    hsv3_oled_text_inv(0, 0, bar);
}

/* Como ui_header(), mas sem o passo da cerimonia (N/8) a direita.
 *
 * Cinco ecras tem titulo com fraccao propria -- "DADOS 12/100", "AQUECER
 * 24/64", "PASS 12/63", "SEED 1/6" -- e o ui_header() normal poe o passo da
 * cerimonia ao lado, na mesma barra. Duas fraccoes lado a lado, sem nada a
 * dizer qual e' qual, lidas de relance num ecra de 128x64: confirmado como
 * fonte real de confusao a 05/08/2026 ("o contador de passo nao bate certo
 * com onde estas"). Nestes cinco ecras, a fraccao local ja diz tudo o que
 * interessa; o passo da cerimonia so' acrescenta ruido. */
static void ui_header_bare(const char *title)
{
    char bar[HSV3_OLED_COLS + 1];
    size_t tlen;

    memset(bar, ' ', HSV3_OLED_COLS);
    bar[HSV3_OLED_COLS] = '\0';

    tlen = strlen(title);
    if (tlen > HSV3_OLED_COLS - 2) tlen = HSV3_OLED_COLS - 2;
    memcpy(bar + 1, title, tlen);

    hsv3_oled_text_inv(0, 0, bar);
}

static void ui_hint(const char *hint)
{
    if (hint == NULL || *hint == '\0') return;
    hsv3_oled_text(0, 7, hint);
    hsv3_oled_hline(0, HSV3_OLED_WIDTH - 1, 54, 1);
}

/* --- ecras auxiliares --------------------------------------------------- */

/* Titulo, ate quatro linhas de conteudo, e a dica. A assinatura e' a mesma de
 * antes -- o que mudou e' que cada argumento passou a ter um lugar fixo. */
static void screen(const char *title, const char *l0, const char *l1,
                   const char *l2, const char *l3, const char *hint)
{
    const char *body[4] = { l0, l1, l2, l3 };

    hsv3_oled_clear();
    ui_header(title);
    for (int i = 0; i < 4; i++) {
        if (body[i] != NULL && body[i][0] != '\0') hsv3_oled_text(0, 2 + i, body[i]);
    }
    ui_hint(hint);
    hsv3_oled_flush();
}

/* Como screen(), mas com ui_header_bare() -- ver ali o porque. So' o
 * show_words() precisa disto, para as paginas "SEED 1/6" nao ganharem
 * tambem o passo da cerimonia ao lado. */
static void screen_bare(const char *title, const char *l0, const char *l1,
                        const char *l2, const char *l3, const char *hint)
{
    const char *body[4] = { l0, l1, l2, l3 };

    hsv3_oled_clear();
    ui_header_bare(title);
    for (int i = 0; i < 4; i++) {
        if (body[i] != NULL && body[i][0] != '\0') hsv3_oled_text(0, 2 + i, body[i]);
    }
    ui_hint(hint);
    hsv3_oled_flush();
}

/* Espera por confirmacao, mexendo a imagem para o painel nao marcar. */
static hsv3_btn_t wait_confirm(void)
{
    for (int t = 0; ; t++) {
        hsv3_btn_t b = hsv3_buttons_wait(2000);
        if (b != HSV3_BTN_NONE) return b;
        if (t % 10 == 9) hsv3_oled_antiburn_step();
    }
}

/* Avanca so com pressao longa.
 *
 * Nos ecras que se copiam a mao, um toque acidental que avance custa a
 * cerimonia inteira: o valor nao volta a aparecer, e sem ele o verify.py nao
 * reproduz nada. Exigir a pressao longa torna o avanco um gesto deliberado.
 *
 * Isto tambem e' seguranca, nao so conforto. O unico ataque que o compromisso
 * nao fecha e' o dispositivo obrigar a repetir cerimonias ate calhar um
 * resultado que lhe sirva -- e cada cerimonia perdida por um toque a mais e'
 * uma repeticao que o utilizador aceita sem desconfiar. */
static void wait_long(void)
{
    while (wait_confirm() != HSV3_BTN_LONG) { /* toque curto nao avanca */ }
}

/* Paragem definitiva. So se sai daqui com o botao RST. */
static void halt_forever(const char *why, const char *detail)
{
    wipe_everything();
    screen("*** PARADO ***", "", why, detail, "", "carrega RST");
    hsv3_oled_flush();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        hsv3_oled_antiburn_step();
    }
}

/* Mostra os 64 hex NUM ecra so, em grupos de quatro e com o numero da linha.
 *
 *      1 a41f 08c2 9e33 7b50
 *      2 ...
 *
 * A versao anterior partia isto em dois ecras de duas linhas corridas de 16
 * caracteres. Copiar 64 hex assim, a mao, sem perder a conta, e' o passo em que
 * esta cerimonia mais facilmente se estraga -- e um caractere trocado so
 * aparece muito depois, no verify.py, sem dizer onde foi.
 *
 * Com grupos de quatro o olho tem sempre uma ancora, e o numero da linha permite
 * conferir a copia por linhas em vez de reler os 64 de uma vez. Cabe tudo em
 * 21 colunas: 1 + 1 + 4x4 + 3 espacos = 21. */
static void show_hex32(const char *title, const uint8_t v[32])
{
    char hex[65];
    hsv3_hex32(v, hex);

    hsv3_oled_clear();
    ui_header(title);
    for (int r = 0; r < 4; r++) {
        char l[22];
        const char *p = hex + r * 16;
        snprintf(l, sizeof(l), "%d %.4s %.4s %.4s %.4s",
                 r + 1, p, p + 4, p + 8, p + 12);
        hsv3_oled_text(0, 2 + r, l);
    }
    ui_hint("copia. manter = ok");
    hsv3_oled_flush();
    wait_long();
    hsv3_wipe(hex, sizeof(hex));
}

/* --- recolha dos lancamentos -------------------------------------------- */

/* Contexto do ecra de recolha, para o callback de redesenho. */
static struct {
    hsv3_phys_kind_t kind;
    int faces;
    char base;
} g_pick;

/* Desenha o ecra durante a contagem de toques.
 *
 * O numero de toques E' o valor: 4 toques = saiu 4 no dado. Paras de tocar e
 * grava sozinho. A barra mostra quanto falta para gravar, para a pausa nao
 * apanhar ninguem de surpresa. */
/* Ecra da recolha.
 *
 *   linha 0   DADOS            12/100
 *   linha 1   [####..............]        progresso dos 100 lancamentos
 *   linhas 2-4          4                 o valor, em grande
 *   linha 6   4 toques
 *   linha 7   grava [########....]        conta-decrescente ate gravar
 *
 * O valor esta em corpo 3x porque e' a unica coisa no ecra que nao pode ser
 * mal lida: um digito errado entra na seed e so se descobre no verify.py.
 */
static void tap_redraw(int taps, int pct)
{
    char buf[22];
    size_t need = hsv3_phys_min(&g_secrets.phys);
    size_t have = g_secrets.phys.count;
    int faces = g_pick.faces;

    hsv3_oled_clear();

    /* O titulo carrega a contagem: durante os lancamentos e' a informacao que
     * mais interessa, e assim a zona de conteudo fica livre para o digito. */
    snprintf(buf, sizeof(buf), "%s %u/%u",
             g_pick.kind == HSV3_PHYS_DICE ? "DADOS" : "MOEDA",
             (unsigned)have, (unsigned)need);
    ui_header_bare(buf);
    hsv3_oled_bar(0, 9, 128, 6, need ? (int)(have * 100 / need) : 0);

    if (taps >= 1 && taps <= faces) {
        /* O valor em corpo 3x, centrado. E' a unica coisa no ecra que nao pode
         * ser mal lida: um digito errado entra na seed e so se descobre no
         * verify.py, sem dizer qual. */
        hsv3_oled_bigchar(56, 26, (char)(g_pick.base + taps - 1), 3);
        snprintf(buf, sizeof(buf), "%d toque%s", taps, taps == 1 ? "" : "s");
        hsv3_oled_text(0, 7, buf);
    } else if (taps == faces + 1) {
        hsv3_oled_text(3, 3, "APAGAR O ULTIMO");
        hsv3_oled_text(0, 5, "um toque a menos");
        hsv3_oled_text(0, 7, "apaga");
    } else if (taps == faces + 2 && have >= need) {
        hsv3_oled_text(6, 3, "TERMINAR");
        hsv3_oled_text(0, 5, "os lancamentos ficam");
        hsv3_oled_text(0, 7, "termina");
    } else {
        hsv3_oled_text(4, 3, "TOQUES A MAIS");
        hsv3_oled_text(0, 5, "nada foi gravado");
        hsv3_oled_text(0, 7, "repete");
    }

    /* A barra decrescente e' a MESMA em qualquer gesto, nao so no digito.
     * A falta dela nos gestos de 7 e 8 toques (apagar/terminar) foi o que fez
     * um "8 toques" real na placa nao terminar: sem ela, quem tenta acertar o
     * gesto nao tem como saber se falta 1 segundo ou 50 ms antes da janela de
     * pausa fechar. Confirmado em hardware a 05/08/2026.
     *
     * A barra comeca em x=56 (col 9.3): qualquer texto nesta linha tem de
     * acabar antes disso, senao sobrepoe-se -- foi o proprio erro que este
     * comentario documenta ter sido apanhado na primeira tentativa. */
    hsv3_oled_bar(56, 57, 72, 6, pct);

    hsv3_oled_flush();
}

static void collect_physical(hsv3_phys_kind_t kind)
{
    const int faces = (kind == HSV3_PHYS_DICE) ? 6 : 2;
    const char base = (kind == HSV3_PHYS_DICE) ? '1' : '0';

    hsv3_phys_init(&g_secrets.phys, kind);
    g_pick.kind = kind;
    g_pick.faces = faces;
    g_pick.base = base;

    /* As instrucoes aparecem uma vez, nao antes de cada lancamento. */
    screen(kind == HSV3_PHYS_DICE ? "LANCAMENTOS: DADOS" : "LANCAMENTOS: MOEDA",
           "", "Toca o numero de", "vezes do valor que",
           "saiu. Depois para.", "OK = comecar");
    wait_confirm();

    for (;;) {
        size_t need = hsv3_phys_min(&g_secrets.phys);
        size_t have = g_secrets.phys.count;
        char l0[22];

        /* ecra em repouso, a espera do primeiro toque do proximo lancamento */
        hsv3_oled_clear();
        snprintf(l0, sizeof(l0), "%s %u/%u",
                 kind == HSV3_PHYS_DICE ? "DADOS" : "MOEDA",
                 (unsigned)have, (unsigned)need);
        ui_header_bare(l0);
        hsv3_oled_bar(0, 9, 128, 6, need ? (int)(have * 100 / need) : 0);

        hsv3_oled_text(2, 3, "toca o valor que");
        hsv3_oled_text(2, 4, kind == HSV3_PHYS_DICE ? "saiu no dado" : "saiu na moeda");
        if (have >= need) {
            snprintf(l0, sizeof(l0), "%dx=apagar  %dx=fim", faces + 1, faces + 2);
        } else {
            snprintf(l0, sizeof(l0), "%dx = apagar o ultimo", faces + 1);
        }
        ui_hint(l0);
        hsv3_oled_flush();

        int taps = hsv3_buttons_count_taps(1200, tap_redraw);

        if (taps >= 1 && taps <= faces) {
            if (hsv3_phys_push(&g_secrets.phys, (char)(base + taps - 1)) != HSV3_OK) {
                halt_forever("lancamentos", "a mais");
            }
        } else if (taps == faces + 1) {
            if (have > 0) hsv3_phys_pop(&g_secrets.phys);
        } else if (taps == faces + 2 && have >= need) {
            return;
        } else {
            /* Contagem invalida nunca grava nada: melhor repetir do que
             * introduzir um lancamento errado que depois faz o verify.py
             * divergir sem se perceber porque. */
            screen(l0, "", "toques a mais.", "nada foi gravado.", "",
                   "repete este dado");
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
}

/* --- entropia do chip ---------------------------------------------------- */

/* Aquecimento: toques suficientes para a fonte de timing poder ser fechada.
 *
 * Este ecra existe por causa da ordem nova. O E_chip tem de ficar comprometido
 * antes do primeiro lancamento, e o timing e' uma das tres fontes que entram
 * nele -- por isso os toques tem de vir antes, nao durante os dados.
 *
 * Faz tambem de diagnostico dos botoes, que e' o unico que ha: com a consola
 * desligada (CONFIG_ESP_CONSOLE_NONE), quando um toque "nao faz nada" nao ha
 * nenhum log para consultar. Aqui ve-se a duracao medida do ultimo toque, a
 * razao do ultimo reset -- se a placa reiniciou sozinha, aparece PANIC ou WDT
 * em vez de POWERON -- e se a memoria RTC sobreviveu ao reset.
 */
static void warm_up_timing(const char *reset_why, int rtc_survived)
{
    screen("AQUECIMENTO", "", "Toca umas 30 vezes.", "Os intervalos entre",
           "toques sao uma das", "fontes. OK = comecar");
    wait_confirm();

    for (;;) {
        unsigned n = hsv3_rng_timing_count();
        char l[22];

        if (n >= HSV3_TIMING_MIN_EVENTS) break;

        hsv3_oled_clear();
        snprintf(l, sizeof(l), "AQUECER %u/%u", n, (unsigned)HSV3_TIMING_MIN_EVENTS);
        ui_header_bare(l);

        hsv3_oled_bar(0, 25, 128, 7, (int)(n * 100u / HSV3_TIMING_MIN_EVENTS));

        /* As duas linhas de diagnostico. Sem consola, e' aqui ou em lado nenhum:
         * um toque que "nao faz nada" mostra-se em ms, e uma placa que reiniciou
         * sozinha aparece como PANIC ou WDT em vez de POWERON. */
        snprintf(l, sizeof(l), "ultimo toque %d ms", hsv3_buttons_last_ms());
        hsv3_oled_text(0, 4, l);
        snprintf(l, sizeof(l), "reset %s rtc %s", reset_why, rtc_survived ? "SIM" : "NAO");
        hsv3_oled_text(0, 5, l);

        ui_hint("toca ate encher");
        hsv3_oled_flush();

        hsv3_buttons_wait(-1);
    }
}

static void gather_chip_entropy(void)
{
    uint8_t buf[32];

    screen("A RECOLHER", "ENTROPIA DO CHIP", "", "jitter do oscilador", "~2 segundos", "");
    hsv3_oled_flush();

    hsv3_chip_init(&g_secrets.chip);

    if (hsv3_rng_trng(buf) != HSV3_OK) {
        halt_forever("TRNG do chip", "nao esta activo");
    }
    hsv3_chip_set(&g_secrets.chip, HSV3_SRC_TRNG, buf);

    if (hsv3_rng_jitter(buf) != HSV3_OK) {
        halt_forever("jitter do relogio", "sem variacao");
    }
    hsv3_chip_set(&g_secrets.chip, HSV3_SRC_JITTER, buf);

    if (hsv3_rng_timing_finish(buf) != HSV3_OK) {
        halt_forever("timing dos botoes", "poucos eventos");
    }
    hsv3_chip_set(&g_secrets.chip, HSV3_SRC_TIMING, buf);

    hsv3_wipe(buf, sizeof(buf));

    /* Se qualquer fonte faltar ou vier morta, isto recusa. Nao ha degradacao. */
    if (hsv3_chip_digest(&g_secrets.chip, g_secrets.e_chip) != HSV3_OK) {
        halt_forever("entropia do chip", "recusada");
    }
}

/* --- uma ronda de entropia: compromisso, lancamentos, revelacao ----------- */
/*
 * A ordem dos tres passos e' a unica coisa neste ficheiro que nao pode ser
 * trocada por conveniencia. Se o E_chip fosse fechado depois dos lancamentos,
 * um chip malicioso escolhia E_chip = E_fisica XOR seed_do_atacante e tudo o
 * resto -- ecra, verify.py, fingerprint no Sparrow -- continuava consistente.
 * O compromisso anotado antes do primeiro dado e' o que torna a promessa "os
 * dados salvam a seed" verdadeira em vez de so parecer verdadeira.
 *
 * Ver core/hsv3_entropy.h para o ataque completo e para o que fica de fora.
 */
static void entropy_round(const char *what, hsv3_phys_kind_t kind, uint8_t out[32])
{
    uint8_t commit[32];

    /* 1. o chip fecha a sua metade primeiro, e fica preso a ela */
    gather_chip_entropy();
    if (hsv3_entropy_commit(g_secrets.e_chip, commit) != HSV3_OK) {
        halt_forever("compromisso", "falhou");
    }

    /* Titulo distinto do ecra seguinte de proposito: os dois mostravam
     * "COMPROMISSO" -- um so' o aviso, o outro o valor a serio -- e em
     * sequencia pareciam duas paginas da mesma coisa. Confirmado em hardware
     * a 05/08/2026. O REVELACAO/E_FISICA/E_CHIP mais abaixo nunca teve este
     * problema porque ja tinha titulos distintos. */
    screen("PREPARA-TE", what, "", "Anota ANTES de",
           "lancar o 1o dado", "OK = mostrar");
    wait_confirm();
    show_hex32("COMPROMISSO", commit);
    screen("ESTA ANOTADO?", "Se lancares sem o ter", "anotado, o chip deixa",
           "de estar preso a nada", "e a verificacao cai.", "OK = esta anotado");
    wait_confirm();
    hsv3_wipe(commit, sizeof(commit));

    /* 2. so agora o utilizador lanca
     *
     * O aviso abaixo nao e' decorativo. O ecra mostra SHA256(lancamentos), nao
     * os lancamentos: sem a sequencia exata anotada em papel, o verify.py nao
     * consegue reproduzir nada e a propriedade central do projeto perde-se. */
    screen("ATENCAO", what, "Escreve cada valor no",
           "papel a medida que o", "fazes.", "OK = percebi");
    wait_confirm();
    collect_physical(kind);
    if (hsv3_phys_digest(&g_secrets.phys, g_secrets.e_phys) != HSV3_OK) {
        halt_forever("lancamentos", "insuficientes");
    }

    /* 3. revelacao: o verify.py confirma que SHA256(E_chip) da o compromisso */
    screen("REVELACAO", "Anota estes dois.", "", "Com eles e o",
           "compromisso verificas", "OK = mostrar");
    wait_confirm();
    show_hex32("E_FISICA", g_secrets.e_phys);
    show_hex32("E_CHIP", g_secrets.e_chip);

    if (hsv3_entropy_combine(g_secrets.e_phys, g_secrets.e_chip, out) != HSV3_OK) {
        halt_forever("combinacao XOR", "falhou");
    }
}

/* --- palavras ------------------------------------------------------------ */

static void show_words(const char *mnemonic, const char *title, int total)
{
    char words[24][HSV3_WORD_MAXLEN + 1];
    int n = 0;
    const char *p = mnemonic;

    while (*p && n < 24) {
        int len = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && len < HSV3_WORD_MAXLEN) words[n][len++] = *p++;
        words[n][len] = '\0';
        while (*p && *p != ' ') p++;     /* seguranca: salta o resto */
        n++;
    }

    /* Com paginacao para tras: quem se perder a copiar volta atras em vez de
     * ter de repetir a cerimonia inteira. */
    const int paginas = (n + 3) / 4;

    for (int page = 0; page < paginas; ) {
        char hdr[22], l[4][22];
        snprintf(hdr, sizeof(hdr), "%s %d/%d", title, page + 1, paginas);
        const char *dica = page > 0 ? "toque=atras manter=ok" : "manter = continuar";

        for (int i = 0; i < 4; i++) {
            int w = page * 4 + i;
            if (w < n) snprintf(l[i], sizeof(l[i]), "%2d. %s", w + 1, words[w]);
            else       l[i][0] = '\0';
        }
        screen_bare(hdr, l[0], l[1], l[2], l[3], dica);

        if (wait_confirm() == HSV3_BTN_LONG) page++;
        else if (page > 0)                   page--;
    }
    (void)total;
    hsv3_wipe(words, sizeof(words));
}

/* Pergunta 3 palavras a sorte, com 3 hipoteses cada. Confirma que foram mesmo
 * copiadas, em vez de o utilizador so ter carregado em OK seis vezes. */
static void quiz_words(const uint8_t entropy[32])
{
    uint8_t r[32];

    /* So escolhe as posicoes das perguntas -- nao entra na seed. Mesmo assim
     * para: um "return" silencioso aqui saltava a confirmacao de que as 24
     * palavras foram mesmo copiadas, e o utilizador nunca saberia porque. */
    if (hsv3_rng_trng(r) != HSV3_OK) halt_forever("TRNG", "parou a meio");

    for (int q = 0; q < 3; q++) {
        uint16_t correct;
        int pos = r[q] % 24;
        if (hsv3_bip39_word_at(entropy, 32, (size_t)pos, &correct) != HSV3_OK) {
            halt_forever("BIP39", "palavra invalida");
        }

        uint16_t opts[3];
        int right = r[q + 8] % 3;
        for (int i = 0; i < 3; i++) {
            opts[i] = (i == right) ? correct
                                   : (uint16_t)((correct + 1 + r[q * 3 + i + 16]) % 2048);
        }

        int sel = 0;
        for (;;) {
            char hdr[22], l[3][22];
            snprintf(hdr, sizeof(hdr), "PALAVRA %d?", pos + 1);
            for (int i = 0; i < 3; i++) {
                snprintf(l[i], sizeof(l[i]), "%c %s",
                         i == sel ? '>' : ' ', hsv3_wordlist[opts[i]]);
            }
            screen(hdr, "", l[0], l[1], l[2], "manter = escolher");
            hsv3_btn_t b = wait_confirm();
            if (b == HSV3_BTN_LONG) break;
            if (b == HSV3_BTN_SHORT) sel = (sel + 1) % 3;
        }

        if (opts[sel] != correct) {
            screen("ERRADO", "", "volta a copiar as", "24 palavras com", "cuidado.", "OK = rever");
            wait_confirm();
            show_words(g_secrets.mnemonic, "SEED", 24);
            q = -1;   /* recomeca o questionario */
        }
    }
    hsv3_wipe(r, sizeof(r));
}

/* --- resultado ----------------------------------------------------------- */

static const struct { const char *label, *path; uint32_t ver; } ACCOUNTS[] = {
    { "BIP84 bc1q", "m/84'/0'/0'", HSV3_VER_ZPUB },
    { "BIP86 taproot", "m/86'/0'/0'", HSV3_VER_XPUB },
    { "BIP49 3...", "m/49'/0'/0'", HSV3_VER_YPUB },
    { "BIP44 1...", "m/44'/0'/0'", HSV3_VER_XPUB },
};

static void show_results(void)
{
    uint8_t fp[4];
    char line[22], xkey[HSV3_XKEY_MAX], addr[HSV3_ADDR_MAX];
    hsv3_hdkey_t acct, node;

    hsv3_bip32_fingerprint(&g_secrets.master, fp);
    snprintf(line, sizeof(line), "  %02x%02x%02x%02x", fp[0], fp[1], fp[2], fp[3]);
    screen("FINGERPRINT", "", line, "", "confere no Sparrow", "OK = continuar");
    wait_confirm();

    for (size_t i = 0; i < sizeof(ACCOUNTS) / sizeof(ACCOUNTS[0]); i++) {
        /* Saltar uma conta em silencio seria um modo degradado: o utilizador
         * ficava sem a xpub para conferir e sem saber que faltava alguma. */
        if (hsv3_bip32_derive(&g_secrets.master, ACCOUNTS[i].path, &acct) != HSV3_OK) {
            halt_forever("derivacao BIP32", ACCOUNTS[i].label);
        }
        if (hsv3_bip32_serialize(&acct, ACCOUNTS[i].ver, 0, xkey, sizeof(xkey)) != HSV3_OK) {
            halt_forever("serializacao", ACCOUNTS[i].label);
        }

        /* Uma chave estendida tem ~111 caracteres: quatro ecras de 3x21. */
        size_t len = strlen(xkey);
        for (size_t off = 0, pg = 1; off < len; off += 63, pg++) {
            char c[3][22], hdr[22];
            for (int r = 0; r < 3; r++) {
                size_t s = off + (size_t)r * 21;
                if (s >= len) { c[r][0] = '\0'; continue; }
                size_t n = len - s < 21 ? len - s : 21;
                memcpy(c[r], xkey + s, n);
                c[r][n] = '\0';
            }
            snprintf(hdr, sizeof(hdr), "%s %u", ACCOUNTS[i].label, (unsigned)pg);
            screen(hdr, c[0], c[1], c[2], "", "OK = continuar");
            wait_confirm();
        }
        hsv3_hdkey_wipe(&acct);
    }

    /* O primeiro endereco e' a ancora da verificacao: e' o que o utilizador
     * confronta com o Sparrow. Se nao sair, nao ha cerimonia. */
    if (hsv3_bip32_derive(&g_secrets.master, "m/84'/0'/0'/0/0", &node) != HSV3_OK ||
        hsv3_address_p2wpkh(node.pub, addr, sizeof(addr)) != HSV3_OK) {
        halt_forever("1o endereco", "nao derivou");
    }
    {
        char a[2][22];
        size_t len = strlen(addr);
        size_t n0 = len < 21 ? len : 21;
        memcpy(a[0], addr, n0); a[0][n0] = '\0';
        if (len > 21) { memcpy(a[1], addr + 21, len - 21); a[1][len - 21] = '\0'; }
        else a[1][0] = '\0';
        screen("1o ENDERECO", a[0], a[1], "", "confere no Sparrow",
               "OK = continuar");
        wait_confirm();
    }
    hsv3_hdkey_wipe(&node);
    hsv3_wipe(xkey, sizeof(xkey));
    hsv3_wipe(addr, sizeof(addr));
}

/* --- passphrase ---------------------------------------------------------- */
/*
 * Tres caminhos, porque nenhum serve toda a gente:
 *
 *   SEM PASSPHRASE    as 24 palavras sao tudo. Quem encontrar a folha leva as
 *                     moedas -- mas tambem nao ha um segundo segredo que se
 *                     possa perder, e perder e' o cenario mais provavel.
 *   GERADA            24 palavras BIP39 de uma segunda ronda de entropia, com
 *                     o seu proprio compromisso. Nao depende de o utilizador
 *                     inventar nada, que e' onde as passphrases costumam ser
 *                     fracas. Em troca, e' um segundo segredo do tamanho de uma
 *                     seed para guardar noutro sitio.
 *   ESCRITA POR MIM   o utilizador escolhe caractere a caractere. Nao ha
 *                     checksum nenhum: um caractere trocado abre outra carteira
 *                     em silencio, sem erro nenhum. Quem apanha isso e' o
 *                     confronto do fingerprint com o Sparrow, no fim.
 *
 * So ASCII 32..126, que e' o que hsv3_bip39_passphrase_valid() aceita: nesse
 * intervalo a normalizacao NFKD que o BIP39 exige e' a identidade, e nao ha
 * ambiguidade possivel contra o Sparrow ou a Coldcard.
 */

#define HSV3_PASS_MAX 63   /* 3 linhas de 21 caracteres no ecra */

static const char *const PASS_GROUPS[] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    /* Sem " ' ` \ |, que sao os que mais se confundem quando copiados a mao. */
    " !#$%&()*+,-./:;<=>?@[]^_{}~",
};
static const char *const PASS_GROUP_NAMES[] = {
    "abc minusculas", "ABC MAIUSCULAS", "012 digitos", "#?! simbolos",
};
#define PASS_NGROUPS ((int)(sizeof(PASS_GROUPS) / sizeof(PASS_GROUPS[0])))

/* O texto em construcao vive no g_secrets, para wipe_everything() o apanhar
 * mesmo que a cerimonia pare a meio da escrita. So o indice do grupo fica de
 * fora, e esse nao e' segredo. */
static int g_pass_group;

/* Escreve o que ja foi digitado nas linhas row..row+2. */
static void pass_draw_text(int row)
{
    const char *t = g_secrets.passphrase;
    size_t len = strlen(t);
    for (int i = 0; i < 3; i++) {
        char l[22];
        size_t off = (size_t)i * 21;
        if (off >= len) break;
        size_t n = len - off < 21 ? len - off : 21;
        memcpy(l, t + off, n);
        l[n] = '\0';
        hsv3_oled_text(0, row + i, l);
    }
}

/* Menu dos tres caminhos. Comeca na opcao gerada, que e' a recomendada. */
static void pass_menu_choice_redraw(int idx)
{
    static const char *const OPTS[3][2] = {
        { "SEM PASSPHRASE",  "so as 24 palavras" },
        { "GERADA",          "24 palavras, 2a ronda" },
        { "ESCRITA POR MIM", "sem rede de seguranca" },
    };
    hsv3_oled_clear();
    ui_header("PASSPHRASE");
    for (int i = 0; i < 3; i++) {
        char l[22];
        snprintf(l, sizeof(l), "%c %s", i == idx ? '>' : ' ', OPTS[i][0]);
        hsv3_oled_text(0, 2 + i, l);
    }
    hsv3_oled_text(2, 5, OPTS[idx][1]);
    ui_hint("manter = escolher");
    hsv3_oled_flush();
}

static void pass_menu_redraw(int idx)
{
    char l[22];
    hsv3_oled_clear();
    snprintf(l, sizeof(l), "PASS %u/%u",
             (unsigned)strlen(g_secrets.passphrase), (unsigned)HSV3_PASS_MAX);
    ui_header_bare(l);
    pass_draw_text(2);            /* linhas 2-4: o que ja esta escrito */
    if (idx < PASS_NGROUPS)       snprintf(l, sizeof(l), "> %s", PASS_GROUP_NAMES[idx]);
    else if (idx == PASS_NGROUPS) snprintf(l, sizeof(l), "> APAGAR O ULTIMO");
    else                          snprintf(l, sizeof(l), "> TERMINAR");
    hsv3_oled_text(0, 5, l);
    ui_hint("manter = escolher");
    hsv3_oled_flush();
}

static void pass_char_redraw(int idx)
{
    char ch = PASS_GROUPS[g_pass_group][idx];
    const char *t = g_secrets.passphrase;
    size_t len = strlen(t);
    char tail[22];

    hsv3_oled_clear();
    ui_header(PASS_GROUP_NAMES[g_pass_group]);

    /* Aqui mostra-se so a cauda do que ja esta escrito, nao o texto todo: com
     * 63 caracteres o texto enchia tres linhas e nao sobrava altura para o
     * caractere em corpo grande. A escolher uma letra, o que interessa e' o
     * fim da palavra. */
    if (len > 20) {
        tail[0] = '~';
        memcpy(tail + 1, t + len - 20, 20);
        tail[21] = '\0';
    } else {
        memcpy(tail, t, len);
        tail[len] = '\0';
    }
    hsv3_oled_text(0, 2, tail);

    if (ch == ' ') hsv3_oled_text(5, 4, "( espaco )");
    else           hsv3_oled_bigchar(56, 26, ch, 3);   /* linhas 3-5 */

    ui_hint("manter = escolher");
    hsv3_oled_flush();
}

/* Escreve a passphrase do utilizador em g_secrets.passphrase. */
static void passphrase_typed(void)
{
    g_secrets.passphrase[0] = '\0';

    screen("ESCREVER", "", "Escolhe o grupo,",
           "depois o caractere.", "Toque = avanca,", "manter = escolhe");
    wait_confirm();

    for (;;) {
        int sel = hsv3_buttons_select(PASS_NGROUPS + 2, 0, pass_menu_redraw);
        size_t len = strlen(g_secrets.passphrase);

        if (sel == PASS_NGROUPS) {                    /* apagar o ultimo */
            if (len > 0) g_secrets.passphrase[len - 1] = '\0';
            continue;
        }
        if (sel == PASS_NGROUPS + 1) {                /* terminar */
            if (len == 0) {
                screen("ESTA VAZIA", "", "Se e' isso que",
                       "queres, volta atras", "e escolhe SEM", "PASSPHRASE. OK");
                wait_confirm();
                continue;
            }
            break;
        }
        if (len >= HSV3_PASS_MAX) {
            screen("CHEIA", "", "63 caracteres e' o", "maximo que cabe no",
                   "ecra para confirmar.", "OK = continuar");
            wait_confirm();
            continue;
        }

        g_pass_group = sel;
        int c = hsv3_buttons_select((int)strlen(PASS_GROUPS[sel]), 0, pass_char_redraw);
        g_secrets.passphrase[len]     = PASS_GROUPS[sel][c];
        g_secrets.passphrase[len + 1] = '\0';
    }

    /* Confirmacao a serio: sem checksum, esta leitura e' a unica defesa contra
     * um caractere trocado antes de o fingerprint denunciar tudo no fim. */
    hsv3_oled_clear();
    ui_header("CONFERE");
    pass_draw_text(2);
    ui_hint("OK = esta certa");
    hsv3_oled_flush();
    wait_confirm();
}

/* --- build de medicao ---------------------------------------------------- */
#ifdef HSV3_JITTER_DUMP
/*
 * Despeja contagens cruas do jitter pela UART, para tools/entropia.py estimar
 * a min-entropia por amostra. Ver port/hsv3_rng.h para o comando completo.
 *
 * A propriedade que interessa aqui nao e' o despejo -- e' que esta funcao nunca
 * regressa. Uma build compilada para medir a fonte NAO consegue, por
 * construcao, chegar a uma cerimonia. Nao ha um modo a mais escondido num menu:
 * ha um firmware diferente, que faz outra coisa e mais nenhuma.
 *
 * Isto tambem e' preciso porque a build de medicao liga a consola pela UART,
 * e numa maquina onde a seed vive em SRAM uma consola ligada e' uma fuga.
 */
#define DUMP_BLOCO   512
#define DUMP_TOTAL   100000

static void jitter_dump_forever(void)
{
    static uint32_t bloco[DUMP_BLOCO];
    unsigned feitas = 0;

    screen("MEDICAO", "Esta build NAO gera", "seeds. So mede a",
           "fonte de jitter e", "despeja pela UART.", "");

    printf("\n# HSV3-JITTER-DUMP v1 amostras=%u\n", (unsigned)DUMP_TOTAL);
    while (feitas < DUMP_TOTAL) {
        hsv3_rng_jitter_raw(bloco, DUMP_BLOCO);
        for (int i = 0; i < DUMP_BLOCO; i++) printf("%08lx\n", (unsigned long)bloco[i]);
        feitas += DUMP_BLOCO;

        char l[22];
        snprintf(l, sizeof(l), "%u / %u", feitas, (unsigned)DUMP_TOTAL);
        screen("MEDICAO", "a recolher amostras", "cruas do jitter", l,
               "nao gera seeds", "");
    }
    printf("# HSV3-JITTER-DUMP fim\n");

    screen("MEDICAO FEITA", "Corre no PC:", "python tools/", "  entropia.py", "", "");
    for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
}
#endif

/* --- cerimonia ----------------------------------------------------------- */

void app_main(void)
{
    /* --- resultado da experiencia da memoria RTC, antes de tudo o resto --- */
    {
        s_rtc_survived = (s_rtc_magic == RTC_PROBE_MAGIC);
        if (!s_rtc_survived) { s_rtc_magic = RTC_PROBE_MAGIC; s_rtc_boots = 0; }
        s_rtc_boots++;
        ESP_LOGW("rtc-probe", "reset=%d  memoria_RTC_sobreviveu=%s  arranques=%lu",
                 (int)esp_reset_reason(), s_rtc_survived ? "SIM" : "NAO",
                 (unsigned long)s_rtc_boots);
    }

    /* 1. radio calado antes de existir qualquer segredo */
    hsv3_radio_off();

    /* 2. ecra */
    if (hsv3_oled_init() != 0) {
        /* sem ecra nao ha cerimonia possivel: nada seria verificavel */
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

#ifdef HSV3_JITTER_DUMP
    jitter_dump_forever();   /* nao regressa: esta build nao gera seeds */
#endif

    /* 3. entropia: falha aqui = paragem, nunca degradacao */
    g_step = 1;
    screen("HELTEC-SEED-V3", "", "a testar as fontes", "de entropia...", "", "");
    if (hsv3_rng_init() != HSV3_OK) {
        halt_forever("fonte de entropia", "nao arrancou");
    }

    /* 4. botao */
    g_step = 2;
    hsv3_buttons_init();

    /* 5. aquecimento: toques suficientes para fechar a fonte de timing, que tem
     * de entrar no E_chip -- e o E_chip tem de estar comprometido antes do
     * primeiro lancamento. Faz tambem de diagnostico do botao. */
    {
        static const char *const reasons[] = {
            "DESCONHECIDA", "POWERON", "EXT/RST", "SOFTWARE", "PANIC",
            "INT_WDT", "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO"
        };
        int rr = (int)esp_reset_reason();
        warm_up_timing(rr >= 0 && rr < (int)(sizeof(reasons) / sizeof(reasons[0]))
                       ? reasons[rr] : "?", s_rtc_survived);
    }

    /* 6. modo de entropia fisica */
    g_step = 3;
    hsv3_phys_kind_t kind = HSV3_PHYS_DICE;
    for (;;) {
        screen("ENTROPIA FISICA",
               kind == HSV3_PHYS_DICE ? "> DADOS   100 x d6" : "  DADOS   100 x d6",
               kind == HSV3_PHYS_DICE ? "  MOEDA   256 x" : "> MOEDA   256 x",
               "", "toque = mudar", "manter = escolher");
        hsv3_btn_t b = wait_confirm();
        if (b == HSV3_BTN_LONG) break;
        if (b != HSV3_BTN_NONE) {
            kind = (kind == HSV3_PHYS_DICE) ? HSV3_PHYS_COIN : HSV3_PHYS_DICE;
        }
    }

    /* 7. a ronda de entropia da seed: compromisso, lancamentos, revelacao */
    g_step = 4;
    screen("PAPEL E CANETA", "Sem o compromisso e a",
           "sequencia dos dados,", "nao podes verificar",
           "a seed no PC depois.", "OK = comecar");
    wait_confirm();
    entropy_round("(seed)", kind, g_secrets.entropy);
    /* A entropia final NAO e' mostrada: deriva-se das duas metades por XOR, e
     * mostra-la eram mais 64 caracteres para copiar a mao sem nada de novo. Em
     * transcricao manual, cada valor a menos e' um erro a menos. */

    /* 8. seed em 24 palavras */
    g_step = 5;
    if (hsv3_bip39_from_entropy(g_secrets.entropy, 32, g_secrets.mnemonic,
                                sizeof(g_secrets.mnemonic)) != HSV3_OK) {
        halt_forever("BIP39", "falhou");
    }
    screen("24 PALAVRAS", "Copia-as para papel", "ou metal.", "",
           "Nao para o telemovel.", "OK = mostrar");
    wait_confirm();
    show_words(g_secrets.mnemonic, "SEED", 24);
    quiz_words(g_secrets.entropy);

    /* 9. passphrase: tres caminhos, escolhidos pelo utilizador */
    g_step = 6;
    screen("PASSPHRASE", "Escolhe um caminho.", "", "Perde-la e' perder as",
           "moedas, sem recurso.", "OK = escolher");
    wait_confirm();
    switch (hsv3_buttons_select(3, 1, pass_menu_choice_redraw)) {
    case 0:
        g_secrets.passphrase[0] = '\0';
        screen("SEM PASSPHRASE", "As 24 palavras sao", "tudo. Quem encontrar",
               "a folha leva tudo.", "", "OK = continuar");
        wait_confirm();
        break;

    case 1:
        /* Segunda ronda completa: compromisso proprio, lancamentos proprios.
         * O mesmo ataque do E_chip vale aqui -- so muda o premio. */
        screen("GERADA", "Outra ronda inteira:", "primeiro o",
               "compromisso, depois", "os lancamentos.", "OK = comecar");
        wait_confirm();
        entropy_round("(passphrase)", kind, g_secrets.pass_entropy);
        if (hsv3_bip39_from_entropy(g_secrets.pass_entropy, 32, g_secrets.passphrase,
                                    sizeof(g_secrets.passphrase)) != HSV3_OK) {
            halt_forever("passphrase", "falhou");
        }
        screen("PASSPHRASE", "24 palavras.", "", "GUARDA NOUTRO SITIO,",
               "longe das 24 da seed.", "OK = mostrar");
        wait_confirm();
        show_words(g_secrets.passphrase, "PASSPHRASE", 24);
        break;

    default:
        passphrase_typed();
        break;
    }
    /* Fail-closed: se por alguma razao saiu daqui algo fora de ASCII 32..126,
     * a normalizacao NFKD deixaria de ser a identidade e o Sparrow podia
     * derivar outra carteira. Melhor parar do que gerar uma seed que so falha
     * quando ja la estiverem moedas. */
    if (hsv3_bip39_passphrase_valid(g_secrets.passphrase) != HSV3_OK) {
        halt_forever("passphrase", "caractere invalido");
    }

    /* 10. derivacao e resultado */
    g_step = 7;
    if (hsv3_bip39_seed(g_secrets.mnemonic, g_secrets.passphrase,
                        g_secrets.seed) != HSV3_OK) {
        halt_forever("PBKDF2", "falhou");
    }
    if (hsv3_bip32_master(g_secrets.seed, 64, &g_secrets.master) != HSV3_OK) {
        halt_forever("BIP32", "falhou");
    }
    show_results();

    /* 11. limpeza */
    g_step = 8;
    screen("VERIFICA NO PC", "verify.py --dice ..", "         --chip ..",
           "         --commit ..", "o --commit prende o", "OK = apagar tudo");
    wait_confirm();
    wipe_everything();
    screen("APAGADO", "Nada ficou gravado", "nesta placa.", "", "", "podes desligar");
    vTaskDelay(pdMS_TO_TICKS(5000));
    hsv3_oled_off();
    for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
}
