#!/usr/bin/env python3
"""Fica a vigiar o USB e avisa mal a placa apareca.

Serve para testar cabos sem ter de andar a correr comandos a mao: poe isto a
correr, troca de cabo, e ve o resultado na hora.

  python tools/detetar_placa.py [segundos]

Uma Heltec V3 aparece como CP210x (ponte USB-serie) ou como "USB JTAG/serial
debug unit" (USB nativo do ESP32-S3). Se nao aparecer nada, o problema esta no
cabo ou na alimentacao, nao no driver: a enumeracao USB acontece antes de
qualquer driver carregar, por isso ate um dispositivo sem driver apareceria.
"""
import sys
import time

try:
    from serial.tools import list_ports
except ImportError:
    print("falta o pyserial. Corre:  python -m pip install esptool", file=sys.stderr)
    raise SystemExit(2)

# VID conhecidos: Silicon Labs (CP210x), Espressif (USB nativo), WCH (CH340),
# FTDI.
INTERESTING_VIDS = {0x10C4, 0x303A, 0x1A86, 0x0403}


def snapshot():
    return {p.device: p for p in list_ports.comports()}


def describe(p) -> str:
    vid = f"{p.vid:04X}" if p.vid is not None else "----"
    pid = f"{p.pid:04X}" if p.pid is not None else "----"
    return f"{p.device}  VID:PID={vid}:{pid}  {p.description}"


def main() -> int:
    timeout = int(sys.argv[1]) if len(sys.argv) > 1 else 120

    base = snapshot()
    print("A vigiar o USB. Liga a placa ou troca de cabo agora.")
    print(f"(paro sozinho ao fim de {timeout}s)\n")
    print("portas ja presentes antes de comecar:")
    for p in base.values():
        print(f"  {describe(p)}")
    print()

    deadline = time.time() + timeout
    while time.time() < deadline:
        cur = snapshot()

        for dev, p in cur.items():
            if dev not in base:
                print(f"\n>>> APARECEU: {describe(p)}")
                if p.vid in INTERESTING_VIDS:
                    print(">>> E' a placa. Podes gravar com:")
                    print(f"       idf.py -p {dev} flash")
                    return 0
                print(">>> Nao parece uma placa ESP32 (VID desconhecido).")

        for dev in base:
            if dev not in cur:
                print(f"<<< desapareceu: {dev}")

        base = cur
        time.sleep(1)

    print("\nNada apareceu.")
    print("A enumeracao USB acontece antes de qualquer driver, por isso isto")
    print("nao e' falta de driver. Por ordem de probabilidade:")
    print("  1. cabo USB-C so de carga (testa-o com um telemovel)")
    print("  2. placa ligada a um carregador em vez do PC")
    print("  3. placa sem energia (o LED devia acender)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
