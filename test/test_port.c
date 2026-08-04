/* Testes da camada port/ no PC, com os substitutos de test/stubs/.
 *
 * Ate agora o port/ tinha zero testes: compilava, e mais nada. Mas duas coisas
 * la dentro sao logica pura e sao das mais importantes do projeto:
 *
 *   1. QUANDO E' QUE UMA FONTE DE ENTROPIA E' RECUSADA. Foi um caminho
 *      degradado silencioso que destruiu a Coldcard Mk3. Aqui forca-se a fonte
 *      a ficar presa e confirma-se que o firmware devolve erro em vez de
 *      entregar entropia fraca -- que na placa nao se consegue provocar a
 *      pedido.
 *
 *   2. O QUE FICA NA MEMORIA DEPOIS DE APAGAR. O acumulador de timing vive
 *      fora da estrutura de segredos do main; se o wipe nao o apanhar,
 *      sobrevive a tudo o resto e ninguem da por isso.
 *
 * Inclui os .c em vez de os ligar, para chegar tambem ao que e' estatico --
 * o framebuffer do OLED so se pode inspecionar assim.
 */
#include <stdio.h>
#include <string.h>

#include "hsv3_stub_ctl.h"

hsv3_stub_t hsv3_stub;

void hsv3_stub_reset(void)
{
    memset(&hsv3_stub, 0, sizeof(hsv3_stub));
    hsv3_stub.rng_state = 0x12345678u;
    hsv3_stub.cycles = 1000u;
}

#include "../port/hsv3_oled.c"   /* NOLINT -- de proposito: ver o cabecalho */
#include "../port/hsv3_rng.c"    /* NOLINT */

static int checks = 0;
static int fails = 0;

static void ok(const char *name, int cond)
{
    checks++;
    if (!cond) { printf("  [X] %s\n", name); fails++; }
}

/* --------------------------------------------------------------------- */
/* Entropia: a regra e' recusar, nunca degradar                          */
/* --------------------------------------------------------------------- */

static void test_rng_fail_closed(void)
{
    uint8_t out[32];

    /* Antes de hsv3_rng_init(), o TRNG nao pode devolver nada. */
    hsv3_stub_reset();
    s_rng_ready = 0;
    ok("trng antes de init -> erro", hsv3_rng_trng(out) == HSV3_ERR_BACKEND);

    /* Fonte presa: esp_random() devolve sempre o mesmo valor. E' a forma do
     * bug da Mk3 -- e tem de parar a cerimonia, nao segui-la. */
    hsv3_stub_reset();
    hsv3_stub.rng_stuck = 1;
    ok("init com fonte presa -> erro", hsv3_rng_init() == HSV3_ERR_BACKEND);
    ok("e o trng continua fechado", hsv3_rng_trng(out) == HSV3_ERR_BACKEND);

    /* Fonte a variar: arranca. */
    hsv3_stub_reset();
    ok("init com fonte a variar -> ok", hsv3_rng_init() == HSV3_OK);
    ok("trng depois de init -> ok", hsv3_rng_trng(out) == HSV3_OK);

    /* Duas leituras seguidas nao podem dar o mesmo. */
    {
        uint8_t a[32], b[32];
        hsv3_rng_trng(a);
        hsv3_rng_trng(b);
        ok("duas leituras do trng diferem", memcmp(a, b, 32) != 0);
    }
}

static void test_jitter(void)
{
    uint8_t out[32], outro[32];

    /* Contagem de ciclos congelada = nao ha jitter nenhum. Recusa. */
    hsv3_stub_reset();
    hsv3_stub.cycles_frozen = 1;
    ok("jitter sem variacao -> erro", hsv3_rng_jitter(out) == HSV3_ERR_BACKEND);

    /* Contagem a variar: produz, e produz coisas diferentes de cada vez. */
    hsv3_stub_reset();
    ok("jitter com variacao -> ok", hsv3_rng_jitter(out) == HSV3_OK);
    ok("jitter -> ok outra vez", hsv3_rng_jitter(outro) == HSV3_OK);
    ok("duas recolhas de jitter diferem", memcmp(out, outro, 32) != 0);
}

/* Os testes de saude do SP 800-90B, com sequencias escolhidas a mao.
 *
 * Correm DURANTE a recolha do jitter, que e' quando ninguem esta a olhar. Uma
 * fonte que arranque bem e degrade a meio passava incolume no teste antigo. */
static void test_health(void)
{
    hsv3_health_t h;

    /* RCT: exatamente no corte tem de falhar, um antes ainda passa. */
    hsv3_health_init(&h);
    int passou = 1;
    for (int i = 0; i < HSV3_RCT_CUTOFF - 1; i++) {
        if (hsv3_health_feed(&h, 1) != HSV3_OK) passou = 0;
    }
    ok("RCT: um abaixo do corte ainda passa", passou);
    ok("RCT: no corte falha", hsv3_health_feed(&h, 1) == HSV3_ERR_BACKEND);

    /* Uma amostra diferente reinicia a contagem de repeticoes. */
    hsv3_health_init(&h);
    passou = 1;
    for (int i = 0; i < HSV3_RCT_CUTOFF * 3; i++) {
        if (hsv3_health_feed(&h, (uint8_t)(i & 1)) != HSV3_OK) passou = 0;
    }
    ok("RCT: alternado nunca dispara", passou);

    /* APT: 0101... da 50% de iguais a referencia, muito abaixo do corte. */
    hsv3_health_init(&h);
    passou = 1;
    for (int i = 0; i < HSV3_APT_WINDOW * 2; i++) {
        if (hsv3_health_feed(&h, (uint8_t)(i & 1)) != HSV3_OK) passou = 0;
    }
    ok("APT: alternado passa a janela toda", passou);

    /* Uma fonte enviesada que nunca repete o suficiente para o RCT: um zero a
     * cada 20 uns, ou seja 973 uns em 1024. O RCT (corte 41) nao ve nada; o
     * APT (corte 793) ve.
     *
     * O zero vai no FIM de cada grupo de 20, de proposito: a referencia do APT
     * e' a primeira amostra da janela, e se a primeira fosse o valor raro esta
     * janela nao disparava. Nao e' um defeito do teste -- e' como o APT
     * funciona, e por isso e' que ele corre janela apos janela em vez de uma
     * so. */
    hsv3_health_init(&h);
    int apanhou = 0;
    for (int i = 0; i < HSV3_APT_WINDOW; i++) {
        if (hsv3_health_feed(&h, (uint8_t)(i % 20 == 19 ? 0 : 1)) != HSV3_OK) {
            apanhou = 1;
            break;
        }
    }
    ok("APT: apanha enviesamento que escapa ao RCT", apanhou);

    /* E o inverso: uma fonte equilibrada nao pode dar falso alarme. */
    hsv3_health_init(&h);
    passou = 1;
    uint32_t x = 0x13579bdfu;
    for (int i = 0; i < HSV3_APT_WINDOW * 4; i++) {
        x = x * 1664525u + 1013904223u;
        if (hsv3_health_feed(&h, (uint8_t)((x >> 31) & 1)) != HSV3_OK) passou = 0;
    }
    ok("saude: fonte equilibrada nao da falso alarme", passou);
}

static void test_timing(void)
{
    uint8_t out[32];

    hsv3_stub_reset();
    hsv3_rng_timing_wipe();

    ok("timing comeca a zero", hsv3_rng_timing_count() == 0);
    ok("timing vazio -> recusa", hsv3_rng_timing_finish(out) == HSV3_ERR_RANGE);

    /* Um flanco a menos do minimo ainda tem de recusar. O ecra de aquecimento
     * depende deste limiar para saber quando pode deixar seguir. */
    for (unsigned i = 0; i < HSV3_TIMING_MIN_EVENTS - 1; i++) {
        hsv3_rng_timing_feed(0x1000u + i * 7919u);
    }
    ok("timing com um flanco a menos -> recusa",
       hsv3_rng_timing_count() == HSV3_TIMING_MIN_EVENTS - 1
       && hsv3_rng_timing_finish(out) == HSV3_ERR_RANGE);

    hsv3_rng_timing_feed(0xdeadbeefu);
    ok("timing no limiar -> ok",
       hsv3_rng_timing_count() == HSV3_TIMING_MIN_EVENTS
       && hsv3_rng_timing_finish(out) == HSV3_OK);

    /* Fechar nao pode esvaziar o acumulador: a ronda da passphrase volta a
     * fecha-lo, com os toques que entretanto entraram. */
    {
        uint8_t depois[32];
        ok("fechar nao apaga o acumulador",
           hsv3_rng_timing_finish(depois) == HSV3_OK
           && memcmp(out, depois, 32) == 0);
        hsv3_rng_timing_feed(0x55aa55aau);
        ok("mais um flanco muda o resultado",
           hsv3_rng_timing_finish(depois) == HSV3_OK
           && memcmp(out, depois, 32) != 0);
    }

    /* E o wipe tem mesmo de deixar o pool a zeros -- nao basta esquecer a
     * contagem. Isto vive fora do g_secrets do main; se ficar la, sobrevive
     * ao fim da cerimonia. */
    hsv3_rng_timing_wipe();
    {
        int limpo = 1;
        for (size_t i = 0; i < sizeof(s_timing_pool); i++) {
            if (s_timing_pool[i] != 0) { limpo = 0; break; }
        }
        ok("wipe poe o pool a zeros", limpo);
        ok("wipe poe a contagem a zero", hsv3_rng_timing_count() == 0);
        ok("e depois do wipe volta a recusar",
           hsv3_rng_timing_finish(out) == HSV3_ERR_RANGE);
    }
}

/* --------------------------------------------------------------------- */
/* OLED: o framebuffer, antes de sair pelo I2C                           */
/* --------------------------------------------------------------------- */

/* 1 se o pixel (x,y) esta aceso no framebuffer. */
static int pix(int x, int y)
{
    return (s_fb[(y >> 3) * HSV3_OLED_WIDTH + x] >> (y & 7)) & 1;
}

static int linha_vazia(int row)
{
    for (int x = 0; x < HSV3_OLED_WIDTH; x++) {
        if (s_fb[row * HSV3_OLED_WIDTH + x] != 0) return 0;
    }
    return 1;
}

static void test_oled(void)
{
    hsv3_stub_reset();
    s_ready = 0;   /* sem I2C: so o framebuffer interessa */

    hsv3_oled_clear();
    ok("clear apaga tudo", linha_vazia(0) && linha_vazia(7));

    /* Uma linha cheia tem 21 caracteres e nem um a mais: e' a medida a que
     * todo o texto da cerimonia foi escrito. */
    hsv3_oled_clear();
    hsv3_oled_text(0, 2, "123456789012345678901");
    {
        int ultimo_x = 20 * HSV3_FONT_ADVANCE;
        int aceso = 0;
        for (int y = 16; y < 24; y++) if (pix(ultimo_x + 1, y)) aceso = 1;
        ok("21 caracteres cabem na linha", aceso);
    }

    /* O 22o caractere nao pode transbordar para a linha seguinte. */
    hsv3_oled_clear();
    hsv3_oled_text(0, 2, "1234567890123456789012345");
    ok("texto a mais nao passa para a linha seguinte", linha_vazia(3));

    /* put_char ATRIBUI o byte da pagina, nao combina. E' por isso que a regra
     * do layout diz para nao escrever texto na linha 6, onde esta o filete. */
    hsv3_oled_clear();
    hsv3_oled_hline(0, HSV3_OLED_WIDTH - 1, 54, 1);
    ok("filete a y=54 aceso", pix(10, 54));
    hsv3_oled_text(0, 6, "x");
    ok("texto na linha 6 apaga mesmo o filete (por isso a regra existe)",
       !pix(0, 54) && pix(10, 54));

    /* Texto invertido acende o fundo -- e' o que faz a barra do titulo. */
    hsv3_oled_clear();
    hsv3_oled_text_inv(0, 0, " ");
    {
        int todos = 1;
        for (int y = 0; y < 8; y++) if (!pix(0, y)) todos = 0;
        ok("espaco invertido acende a coluna toda", todos);
    }

    /* O digito grande do ecra dos dados: 3x de um glifo 5x7 = 15x21 pixeis. */
    hsv3_oled_clear();
    hsv3_oled_bigchar(56, 26, '4', 3);
    {
        int acesos = 0, fora = 0;
        for (int y = 0; y < HSV3_OLED_HEIGHT; y++) {
            for (int x = 0; x < HSV3_OLED_WIDTH; x++) {
                if (!pix(x, y)) continue;
                acesos++;
                if (x < 56 || x >= 56 + 15 || y < 26 || y >= 26 + 21) fora++;
            }
        }
        ok("bigchar desenha alguma coisa", acesos > 20);
        ok("bigchar nao sai da sua caixa de 15x21", fora == 0);
        ok("bigchar a y=26 nao toca na linha da dica (y>=56)", 26 + 21 <= 56);
    }

    /* A barra de progresso: 0% sem enchimento, 100% cheia, e a moldura sempre. */
    hsv3_oled_clear();
    hsv3_oled_bar(0, 9, 128, 6, 0);
    {
        int dentro = 0;
        for (int x = 2; x < 126; x++) for (int y = 11; y < 13; y++) dentro += pix(x, y);
        ok("barra a 0% nao tem enchimento", dentro == 0);
        ok("barra a 0% tem moldura", pix(0, 9) && pix(127, 9) && pix(0, 14));
    }
    hsv3_oled_clear();
    hsv3_oled_bar(0, 9, 128, 6, 100);
    {
        int dentro = 0;
        for (int x = 2; x < 126; x++) dentro += pix(x, 11);
        ok("barra a 100% enche", dentro > 120);
    }

    /* --- letras grandes: o modo para quem nao ve bem --- */
    ok("a escala 2 poe 10 caracteres por linha", hsv3_oled_bigcols(2) == 10);
    ok("a escala 1 poe os mesmos 21 do texto normal", hsv3_oled_bigcols(1) == 21);
    ok("a escala 3 poe 7", hsv3_oled_bigcols(3) == 7);

    /* Dez caracteres a escala 2 tem de caber; o 11o nao pode transbordar. */
    hsv3_oled_clear();
    hsv3_oled_bigtext(0, 16, "0123456789", 2);
    {
        int fora = 0, dentro = 0;
        for (int y = 0; y < HSV3_OLED_HEIGHT; y++) {
            for (int x = 0; x < HSV3_OLED_WIDTH; x++) {
                if (!pix(x, y)) continue;
                dentro++;
                if (y < 16 || y >= 16 + 14) fora++;
            }
        }
        ok("bigtext desenha os dez", dentro > 60);
        ok("bigtext fica na sua faixa de 14 pixeis", fora == 0);
    }

    /* O espaco nao acende nada, mas ocupa o lugar: e' o que separa os grupos
     * de quatro no hex. */
    hsv3_oled_clear();
    hsv3_oled_bigtext(0, 16, "a a", 2);
    {
        int col_meio = 0;
        for (int y = 16; y < 30; y++) if (pix(12 + 2, y)) col_meio = 1;
        ok("o espaco a escala 2 fica mesmo em branco", !col_meio);
    }

    /* A linha do hex em modo grande -- "a41f 08c2" -- tem de caber. */
    ok("os 9 caracteres de um grupo de hex cabem a escala 2",
       (int)strlen("a41f 08c2") <= hsv3_oled_bigcols(2));
    /* E a palavra mais comprida com o numero: "24.abandon" = 10. */
    ok("a palavra mais comprida numerada cabe a escala 2",
       (int)strlen("24.abandon") <= hsv3_oled_bigcols(2));

    /* Um caractere fora de ASCII imprimivel vira '?' em vez de ler fora do
     * array da fonte. */
    hsv3_oled_clear();
    hsv3_oled_text(0, 2, "\x01");
    {
        uint8_t com_controlo[8];
        memcpy(com_controlo, &s_fb[2 * HSV3_OLED_WIDTH], 8);
        hsv3_oled_clear();
        hsv3_oled_text(0, 2, "?");
        ok("caractere de controlo vira '?'",
           memcmp(com_controlo, &s_fb[2 * HSV3_OLED_WIDTH], 8) == 0);
    }
}

int main(void)
{
    printf("HELTEC-SEED-V3 -- testes do port/ (com substitutos do ESP-IDF)\n\n");

    test_rng_fail_closed();
    test_health();
    test_jitter();
    test_timing();
    test_oled();

    printf("\n");
    if (fails) {
        printf("FALHOU: %d de %d verificacoes\n", fails, checks);
        return 1;
    }
    printf("OK -- todas as %d verificacoes passaram\n", checks);
    printf("     entropia fail-closed (TRNG preso, jitter congelado),\n");
    printf("     acumulador de timing (limiar e wipe), framebuffer do OLED.\n");
    return 0;
}
