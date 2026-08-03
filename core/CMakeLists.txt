# Nucleo portatil: nao inclui um unico header do ESP-IDF.
# E por isso que os testes no PC (test/build.sh) dizem alguma coisa sobre o
# que corre na placa -- e o mesmo codigo, com o mesmo backend mbedTLS.
idf_component_register(
    SRCS "hsv3_crypto_mbedtls.c"
         "hsv3_entropy.c"
         "hsv3_bip39.c"
         "hsv3_bip32.c"
         "hsv3_base58.c"
         "hsv3_bech32.c"
         "hsv3_wordlist.c"
    INCLUDE_DIRS "."
    REQUIRES mbedtls)
