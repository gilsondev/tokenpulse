#!/bin/bash
# Grava o firmware de uma placa. O nome da placa é o env do platformio.ini.
#   ./flash.sh [placa]                  compila e manda por OTA pela serial (o daemon envia, a placa mostra o progresso)
#   ./flash.sh [placa] --bin arquivo    manda por OTA um .bin pronto, como o *-ota.bin de um release, sem compilar
#   ./flash.sh [placa] --usb            compila e grava pelo bootloader (primeira vez, ou placa sem este firmware)
#   ./flash.sh [placa] --usb --port P   idem, na porta P (quando há mais de uma placa nova plugada)
# Sem [placa], usa o default_envs do platformio.ini.
set -e
cd "$(dirname "$0")"
board=$(grep -m1 '^default_envs' platformio.ini | cut -d= -f2 | tr -d ' ')
pio=$(command -v pio || echo ~/.local/bin/pio)
usb=false
bin=""
port=""
while [ $# -gt 0 ]; do
  case $1 in
    --usb) usb=true ;;
    --port) port=$2; shift ;;
    --bin) bin=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift ;;
    *) board=$1 ;;
  esac
  shift
done
if [ -z "$bin" ]; then
  "$pio" run -e "$board"
  bin="$PWD/.pio/build/$board/firmware.bin"
fi

if $usb; then
  # o daemon segura a porta USB: pausa durante a gravação e religa no fim
  launchctl bootout "gui/$(id -u)/com.gilsondev.tokenpulse" 2>/dev/null || true
  trap 'launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.gilsondev.tokenpulse.plist 2>/dev/null' EXIT
  sleep 1
  if [ -z "$port" ]; then
    # com mais de uma placa plugada o upload do PlatformIO pega a primeira porta e pode gravar na placa
    # errada. Vale a porta que já roda esta placa; senão, a única que não roda o tokenpulse.
    py=$(head -1 "$pio" | sed 's/^#!//')
    port=$("$py" - "$board" <<'PY'
import glob, json, sys, time
import serial
want, fresh = sys.argv[1], []
for path in sorted(set(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.SLAB_USBtoUART*"))):
    try:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = path, 460800, 0
        s.dtr = s.rts = False
        s.open()
        s.dtr = "usbmodem" in path
        buf, deadline = b"", time.time() + 3
        while time.time() < deadline and b"hello " not in buf:
            s.write(b"id\n"); time.sleep(0.3); buf += s.read(4096)
        s.close()
    except (OSError, serial.SerialException):
        continue
    name = None
    for line in buf.decode(errors="replace").splitlines():
        if line.startswith("hello "):
            try: name = json.loads(line[6:])["board"]
            except (ValueError, KeyError): pass
    if name == want:
        print(path); sys.exit()
    if name is None:
        fresh.append(path)
if len(fresh) == 1:
    print(fresh[0])
elif fresh:
    print("mais de uma placa sem o tokenpulse: " + " ".join(fresh), file=sys.stderr)
else:
    print("nenhuma porta com uma placa " + want + " ou sem o tokenpulse", file=sys.stderr)
PY
)
    [ -n "$port" ] || { echo "escolha a porta com --port" >&2; exit 1; }
    echo "gravando $board em $port"
  fi
  "$pio" run -e "$board" -t upload --upload-port "$port"
  exit
fi

[ -f "$bin" ] || { echo "não achei $bin" >&2; exit 1; }
log=~/Library/Logs/tokenpulse.log
start=$(wc -l < "$log")
mkdir -p ~/.claude/esp-tokens-ota
req=~/.claude/esp-tokens-ota/"$board"
# pedido que sobrasse no disco gravaria um .bin qualquer dias depois, no próximo plug: some ao sair
trap 'rm -f "$req" "$req.tmp"' INT TERM
echo "$bin" > "$req.tmp" && mv "$req.tmp" "$req"  # o daemon nunca lê pela metade
# uma falha por falta de resposta tenta mais uma vez: a placa só troca o firmware se o md5 bater
for attempt in 1 2; do
  for _ in $(seq 1 90); do
    result=$(tail -n +"$((start + 1))" "$log" | grep -m1 "^\[$board\] ota:" || true)
    [ -n "$result" ] && break
    sleep 1
  done
  [ -z "$result" ] && break
  echo "$result"
  if [[ "$result" == *"ota: ok"* ]]; then
    # gravou não é o mesmo que bootou: espera a placa voltar e se apresentar ao daemon
    start=$(wc -l < "$log")
    for _ in $(seq 1 75); do
      tail -n +"$((start + 1))" "$log" | grep -q "^\[$board\] conectada" && { echo "a placa voltou com o firmware novo"; exit 0; }
      sleep 1
    done
    echo "a placa não voltou em 75 s: o firmware novo pode estar travando no boot. Veja \"Placa não volta depois do OTA\" no README." >&2
    exit 1
  fi
  [[ "$result" == *"sem resposta"* && $attempt == 1 ]] || exit 1
  echo "tentando de novo…"
  start=$(wc -l < "$log")
  echo "$bin" > "$req.tmp" && mv "$req.tmp" "$req"
done
rm -f "$req"
echo "nenhuma placa $board respondeu em 90 s. Ela está plugada? O daemon roda? launchctl list | grep tokenpulse"
exit 1
