#!/usr/bin/env python3
"""Falha o build se algum simbolo de radio tiver entrado no firmware.

O sdkconfig diz que o Wi-Fi e o Bluetooth estao desligados. Isto verifica que e'
verdade no binario que vai ser gravado, em vez de confiar na configuracao. Uma
dependencia transitiva que arrastasse a stack de Wi-Fi passaria despercebida --
e num aparelho que gera chaves privadas isso seria fatal.

Corre automaticamente no fim do build (ver CMakeLists.txt), ou a mao:

  python tools/check_no_radio.py build/heltec-seed-v3.elf
"""
import os
import re
import subprocess
import sys

# Prefixos que nao podem existir no ELF final.
FORBIDDEN = [
    r"^esp_wifi_",       # stack Wi-Fi
    r"^wifi_",
    r"^ieee80211",
    r"^esp_bt_",         # Bluetooth / BLE
    r"^esp_ble_",
    r"^bt_controller",
    r"^nimble_",
    r"^esp_now_",        # ESP-NOW
    r"^sx126x_",         # driver do radio LoRa
    r"^lora_",
    r"^radio_",
    r"^esp_coex_",
]

# A nossa propria funcao, que POE o radio em reset -- casa com o padrao mas e'
# precisamente o contrario de ligar radio.
ALLOWED = {"radio_off", "hsv3_radio_off"}

# Tipos do nm que representam codigo ou dados REALMENTE ligados ao binario.
# Deliberadamente NAO inclui 'A' (absoluto): ver a nota abaixo.
LINKED_TYPES = set("TtDdBbRrWwVv")

# Porque e' que 'A' nao conta:
#
# O ESP32-S3 tem a stack de Wi-Fi na ROM de mascara do proprio chip. Os linker
# scripts do ESP-IDF (esp32s3.rom.ld e companhia) declaram esses endereços com
# PROVIDE(), e o nm mostra-os como 'A' (absoluto) em 0x4000xxxx. Existem no
# silicio quer o firmware os use quer nao -- nao ha binario para ESP32-S3 sem
# eles, e nenhum byte de codigo Wi-Fi foi ligado por causa disso.
#
# Codigo mesmo ligado aparece como 'T' em 0x42xxxxxx (flash) ou 0x4037xxxx
# (IRAM). E' isso que este teste procura.
#
# Filtrar por tipo em vez de manter uma lista de nomes permitidos e' importante:
# uma lista de nomes criaria um ponto cego permanente. Se um dia codigo Wi-Fi a
# serio fosse ligado com um nome ja branqueado, passava sem ninguem dar por isso.
ROM_REGION = range(0x40000000, 0x40080000)


def symbols(elf: str):
    """Devolve (endereco, tipo, nome) para cada simbolo definido."""
    candidates = [
        os.path.expanduser("~/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/"
                           "xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe"),
        "xtensa-esp32s3-elf-nm", "nm", "llvm-nm",
    ]
    for tool in candidates:
        try:
            out = subprocess.run([tool, "--defined-only", elf],
                                 capture_output=True, text=True, check=True).stdout
        except (FileNotFoundError, OSError, subprocess.CalledProcessError):
            continue
        rows = []
        for ln in out.splitlines():
            parts = ln.split()
            if len(parts) < 3:
                continue
            addr, kind, name = parts[0], parts[1], parts[-1]
            try:
                rows.append((int(addr, 16), kind, name))
            except ValueError:
                continue
        return rows
    print("ERRO: nao encontrei o nm do xtensa no PATH", file=sys.stderr)
    raise SystemExit(2)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    elf = sys.argv[1]

    patterns = [re.compile(p) for p in FORBIDDEN]
    linked, rom = [], []

    for addr, kind, name in symbols(elf):
        if name in ALLOWED or not any(p.search(name) for p in patterns):
            continue
        if kind in LINKED_TYPES and addr not in ROM_REGION:
            linked.append((addr, kind, name))
        else:
            rom.append(name)

    if linked:
        print(f"BUILD RECUSADO: {len(linked)} simbolos de radio LIGADOS ao "
              f"firmware\n", file=sys.stderr)
        for addr, kind, name in sorted(linked, key=lambda r: r[2])[:40]:
            print(f"  {addr:08x} {kind} {name}", file=sys.stderr)
        if len(linked) > 40:
            print(f"  ... e mais {len(linked) - 40}", file=sys.stderr)
        print("\nUma maquina de gerar seeds nao pode ter um transmissor ligado.",
              file=sys.stderr)
        return 1

    print("check_no_radio: OK, zero codigo de Wi-Fi/BT/LoRa ligado ao firmware")
    if rom:
        print(f"  ({len(rom)} simbolos de radio existem na ROM do ESP32-S3 e "
              f"aparecem sempre;\n   sao enderecos do silicio, nao codigo ligado)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
