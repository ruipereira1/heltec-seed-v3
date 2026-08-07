# Build reprodutivel do firmware.
#
# Serve para uma coisa concreta: poderes confirmar que o .bin que gravas na
# placa corresponde mesmo a este codigo-fonte, e nao a outra coisa qualquer.
# Duas pessoas que corram isto tem de obter o mesmo SHA256.
#
#   docker build -t heltec-seed-v3 .
#   docker run --rm -v "$PWD/out:/out" heltec-seed-v3
#   sha256sum out/heltec-seed-v3.bin
#
# A versao do ESP-IDF esta fixada por digest, nao por tag: uma tag pode ser
# movida, um digest nao. O digest abaixo corresponde a tag v5.3.2 (o "FROM"
# tambem funciona sem a tag no comentario -- ela e' so' para um humano
# reconhecer a versao a olho).
FROM espressif/idf:v5.3.2@sha256:5b8f58d9d9caa45b689a31a0b6a0cc53b84594e0ba5fdd8d28eeef7d6123242a

# Datas fixas para o compilador nao embeber a hora do build no binario.
ENV SOURCE_DATE_EPOCH=1735689600
ENV IDF_CCACHE_ENABLE=0

WORKDIR /project
COPY CMakeLists.txt sdkconfig.defaults partitions.csv ./
COPY core/ core/
COPY port/ port/
COPY main/ main/
COPY tools/check_no_radio.py tools/

RUN . $IDF_PATH/export.sh \
    && idf.py set-target esp32s3 \
    && idf.py build

# Copia o binario e imprime o hash, que e' o que interessa comparar.
CMD ["/bin/bash", "-c", "\
    mkdir -p /out && \
    cp build/heltec-seed-v3.bin build/heltec-seed-v3.elf /out/ && \
    echo && echo 'SHA256 do firmware:' && \
    sha256sum /out/heltec-seed-v3.bin"]
