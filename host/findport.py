"""Acha a porta USB de uma placa para gravar pela USB: a que já roda o tokenpulse com esse nome ou, senão,
a única que não roda o tokenpulse. Com mais de uma placa plugada, gravar na primeira porta pode
pegar a placa errada. Usado pelo setup.sh e pelo firmware/flash.sh --usb.

    uv run -q --with pyserial python host/findport.py <placa>   imprime a porta, ou explica no stderr
"""
import glob
import json
import sys
import time

import serial

want, fresh = sys.argv[1], []
paths = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*")
paths += glob.glob("/dev/cu.SLAB_USBtoUART*")
for path in sorted(set(paths)):
    native = "usbmodem" in path  # USB nativa do ESP32-S3; o resto é ponte USB-serial
    try:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = path, 460800, 0
        s.dtr = s.rts = False
        s.open()
        s.dtr = native
        # pela ponte, abrir a porta reinicia o ESP32 clássico, que demora uns 4 s para responder
        buf, deadline = b"", time.time() + (3 if native else 8)
        while time.time() < deadline and b"hello " not in buf:
            s.write(b"id\n")
            time.sleep(0.3)
            buf += s.read(4096)
        s.close()
    except (OSError, serial.SerialException):
        continue
    name = None
    for line in buf.decode(errors="replace").splitlines():
        if line.startswith("hello "):
            try:
                name = json.loads(line[6:])["board"]
            except (ValueError, KeyError):
                pass
    if name == want:
        print(path)
        sys.exit()
    if name is None:
        fresh.append(path)
if len(fresh) == 1:
    print(fresh[0])
    sys.exit()
if fresh:
    print("mais de uma placa sem o tokenpulse: " + " ".join(fresh) + ". Deixe só a nova plugada.", file=sys.stderr)
else:
    print(f"não achei a placa: nenhuma porta com uma {want} ou sem o tokenpulse. Ela está plugada com um cabo de dados?",
          file=sys.stderr)
sys.exit(1)
