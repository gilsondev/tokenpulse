#!/bin/bash
# Grava o firmware de uma placa. O nome da placa é o env do platformio.ini.
#   ./flash.sh [placa]                  compila e manda por OTA pela serial (o daemon envia, a placa mostra o progresso)
#   ./flash.sh [placa] --bin arquivo    manda por OTA um .bin pronto, como o *-ota.bin de um release, sem compilar
#   ./flash.sh [placa] --usb            compila e grava pelo bootloader (primeira vez, ou placa sem este firmware)
# Sem [placa], usa o default_envs do platformio.ini.
set -e
cd "$(dirname "$0")"
board=$(grep -m1 '^default_envs' platformio.ini | cut -d= -f2 | tr -d ' ')
pio=$(command -v pio || echo ~/.local/bin/pio)
usb=false
bin=""
while [ $# -gt 0 ]; do
  case $1 in
    --usb) usb=true ;;
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
  trap 'launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.gilsondev.tokenpulse.plist' EXIT
  "$pio" run -e "$board" -t upload
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
  [[ "$result" == *"ota: ok"* ]] && exit 0
  [[ "$result" == *"sem resposta"* && $attempt == 1 ]] || exit 1
  echo "tentando de novo…"
  start=$(wc -l < "$log")
  echo "$bin" > "$req.tmp" && mv "$req.tmp" "$req"
done
rm -f "$req"
echo "nenhuma placa $board respondeu em 90 s. Ela está plugada? O daemon roda? launchctl list | grep tokenpulse"
exit 1
