#!/bin/bash
# Compila e grava o firmware de uma placa. O nome da placa é o env do platformio.ini.
#   ./flash.sh [placa]         OTA pela serial: o daemon do login envia o .bin e a placa mostra o progresso
#   ./flash.sh [placa] --usb   pelo bootloader com esptool, para quando o firmware da placa não tiver OTA
# Sem [placa], usa o default_envs do platformio.ini.
set -e
cd "$(dirname "$0")"
board=$(grep -m1 '^default_envs' platformio.ini | cut -d= -f2 | tr -d ' ')
usb=false
for arg in "$@"; do
  case $arg in
    --usb) usb=true ;;
    *) board=$arg ;;
  esac
done
~/.local/bin/pio run -e "$board"

if $usb; then
  # o daemon segura a porta USB: pausa durante a gravação e religa no fim
  launchctl bootout "gui/$(id -u)/com.gilsondev.tokenpulse" 2>/dev/null || true
  trap 'launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.gilsondev.tokenpulse.plist' EXIT
  ~/.local/bin/pio run -e "$board" -t upload
  exit
fi

log=~/Library/Logs/tokenpulse.log
start=$(wc -l < "$log")
mkdir -p ~/.claude/esp-tokens-ota
req=~/.claude/esp-tokens-ota/"$board"
# pedido que sobrasse no disco gravaria um .bin qualquer dias depois, no próximo plug: some ao sair
trap 'rm -f "$req" "$req.tmp"' INT TERM
echo "$PWD/.pio/build/$board/firmware.bin" > "$req.tmp" && mv "$req.tmp" "$req"  # o daemon nunca lê pela metade
for _ in $(seq 1 90); do
  result=$(tail -n +"$((start + 1))" "$log" | grep -m1 "^\[$board\] ota:" || true)
  if [ -n "$result" ]; then
    echo "$result"
    [[ "$result" == *"ota: ok"* ]]
    exit
  fi
  sleep 1
done
rm -f "$req"
echo "nenhuma placa $board respondeu em 90 s. Ela está plugada? O daemon roda? launchctl list | grep tokenpulse"
exit 1
