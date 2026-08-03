#!/bin/sh
# Compila e corre os testes do nucleo no PC.
#
# Usa "zig cc" (clang) porque nao exige instalacao de Visual Studio nem
# privilegios de administrador:   python -m pip install ziglang
#
#   sh test/build.sh              compila e corre tudo
#   sh test/build.sh smoke        so o teste das primitivas
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="python -m ziglang cc"
MB="$ROOT/third_party/mbedtls"
OUT="$ROOT/test/build"
mkdir -p "$OUT"

if [ ! -d "$MB" ]; then
  echo "falta o mbedTLS. Corre:"
  echo "  git clone --depth 1 --branch v3.6.2 https://github.com/Mbed-TLS/mbedtls third_party/mbedtls"
  exit 1
fi

# So os modulos do mbedTLS que a configuracao minima liga.
MBEDTLS_SRC="
  $MB/library/platform.c
  $MB/library/platform_util.c
  $MB/library/constant_time.c
  $MB/library/sha256.c
  $MB/library/sha512.c
  $MB/library/ripemd160.c
  $MB/library/md.c
  $MB/library/pkcs5.c
  $MB/library/bignum.c
  $MB/library/bignum_core.c
  $MB/library/ecp.c
  $MB/library/ecp_curves.c
  $MB/library/error.c
"

CORE_SRC="
  $ROOT/core/hsv3_crypto_mbedtls.c
  $ROOT/core/hsv3_entropy.c
  $ROOT/core/hsv3_bip39.c
  $ROOT/core/hsv3_bip32.c
  $ROOT/core/hsv3_base58.c
  $ROOT/core/hsv3_bech32.c
  $ROOT/core/hsv3_wordlist.c
"

# O test_port.c inclui os .c do OLED e do RNG em vez de os ligar, para chegar
# ao que e' estatico -- o framebuffer so se inspeciona assim. Por isso aqui so
# entram os que faltam.
PORT_SRC="
  $ROOT/core/hsv3_crypto_mbedtls.c
  $ROOT/port/hsv3_font.c
"

CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror
  -Wno-unused-command-line-argument
  -I$ROOT/core -I$MB/include -I$ROOT/test
  -DMBEDTLS_CONFIG_FILE=<hsv3_mbedtls_config.h>"

# Os substitutos do ESP-IDF so entram na compilacao do port/. O core/ nao pode
# ve-los: e' precisamente por nao incluir um unico header do ESP-IDF que os
# testes dele no PC dizem alguma coisa sobre o que corre na placa.
PORT_CFLAGS="$CFLAGS -I$ROOT/port -I$ROOT/test/stubs"

build() {
  name=$1; shift
  echo "==> $name"
  # shellcheck disable=SC2086
  $CC $CFLAGS "$@" -o "$OUT/$name.exe"
  "$OUT/$name.exe"
}

build_port() {
  name=$1; shift
  echo "==> $name"
  # shellcheck disable=SC2086
  $CC $PORT_CFLAGS "$@" -o "$OUT/$name.exe"
  "$OUT/$name.exe"
}

case "${1:-all}" in
  smoke)
    # shellcheck disable=SC2086
    build smoke $ROOT/test/smoke.c $ROOT/core/hsv3_crypto_mbedtls.c $MBEDTLS_SRC
    ;;
  port)
    # shellcheck disable=SC2086
    build_port test_port $ROOT/test/test_port.c $PORT_SRC $MBEDTLS_SRC
    ;;
  all)
    # shellcheck disable=SC2086
    build smoke $ROOT/test/smoke.c $ROOT/core/hsv3_crypto_mbedtls.c $MBEDTLS_SRC
    # shellcheck disable=SC2086
    build test_core $ROOT/test/test_core.c $CORE_SRC $MBEDTLS_SRC
    # shellcheck disable=SC2086
    build_port test_port $ROOT/test/test_port.c $PORT_SRC $MBEDTLS_SRC
    echo "==> hsv3_dump (ferramenta de confronto)"
    # shellcheck disable=SC2086
    $CC $CFLAGS $ROOT/test/hsv3_dump.c $CORE_SRC $MBEDTLS_SRC -o "$OUT/hsv3_dump.exe"
    ;;
  *)
    echo "uso: $0 [smoke|core|port|all]"; exit 2
    ;;
esac

echo
echo "OK"
