# HELTEC-SEED-V3 -- projeto ESP-IDF para Heltec WiFi LoRa 32 V3 (ESP32-S3).
#
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p COMx flash
#
# O build falha de proposito se algum simbolo de radio entrar no firmware.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/core"
    "${CMAKE_CURRENT_LIST_DIR}/port")

# So estes componentes (e as suas dependencias) entram no build.
#
# Por omissao o ESP-IDF compila tudo -- incluindo esp_http_client, tcp_transport
# e a stack de TLS, que este projeto nunca usa. Numa maquina que gera chaves
# privadas, codigo que nao serve para nada e' so superficie de ataque a mais.
# Isto tambem resolve a incompatibilidade de configuracao do mbedTLS: sem
# componentes de rede, ninguem pede TLS.
set(COMPONENTS main core port)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(heltec-seed-v3)

# --- build de medicao da fonte de jitter ------------------------------------
# Com -D HSV3_JITTER_DUMP=1 o firmware deixa de gerar seeds e passa a despejar
# contagens cruas pela UART, para tools/entropia.py estimar a min-entropia.
#
# E' um firmware diferente, nao um modo escondido: a funcao que faz o despejo
# nunca regressa, por isso esta build nao consegue chegar a uma cerimonia nem
# por engano. E o build normal nao leva uma linha disto -- ha uma invariante em
# test/test_invariantes.py que o confirma.
if(HSV3_JITTER_DUMP)
    message(WARNING
        "HSV3_JITTER_DUMP ligado: esta build MEDE a fonte de jitter e NAO gera "
        "seeds. Nao a uses para uma cerimonia.")
    idf_build_set_property(COMPILE_DEFINITIONS "HSV3_JITTER_DUMP=1" APPEND)
endif()

# --- verificacao anti-radio, no fim do build --------------------------------
# Nao basta o sdkconfig dizer que o Wi-Fi esta desligado: confirma-se no ELF.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_LIST_DIR}/tools/check_no_radio.py"
            "$<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>"
    COMMENT "A confirmar que nao ha simbolos de Wi-Fi/BT/LoRa no firmware")
