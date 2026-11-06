#!/bin/bash
# Instala o tokenpulse de uma vez: grava na placa plugada o firmware do último release, pela USB, e liga o
# daemon no login do macOS. Serve para a primeira vez e para reinstalar (a gravação apaga os ajustes).
#   ./setup.sh <placa>     guition-jc3248w535, cyd-2432s028r ou waveshare-s3-lcd-147b
set -euo pipefail
cd "$(dirname "$0")"

board=${1:-}
case $board in
  guition-jc3248w535 | waveshare-s3-lcd-147b) chip=esp32s3 ;;
  cyd-2432s028r) chip=esp32 ;;
  *)
    echo "uso: ./setup.sh <placa>, com a placa sendo guition-jc3248w535, cyd-2432s028r ou waveshare-s3-lcd-147b" >&2
    exit 1
    ;;
esac
command -v uv >/dev/null || { echo "falta o uv: https://docs.astral.sh/uv/" >&2; exit 1; }
claude auth status >/dev/null 2>&1 || echo "aviso: o Claude Code não parece logado (rode claude auth status); sem login a placa não mostra o % de uso" >&2

# o daemon, se já roda, segura a porta e testaria a placa antes de ela ter o firmware: pausa até o fim
launchctl bootout "gui/$(id -u)/com.gilsondev.tokenpulse" 2>/dev/null || true
sleep 1

echo "1/3 procurando a placa"
port=$(uv run -q --with pyserial python host/findport.py "$board")

echo "2/3 gravando o firmware de $board do último release em $port (~30 s)"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
curl -fsSL -o "$dir/factory.bin" \
  "https://github.com/gilsondev/tokenpulse/releases/latest/download/tokenpulse-$board-factory.bin"
uvx -q --from "esptool<5" esptool.py --chip "$chip" --port "$port" -b 460800 write_flash 0x0 "$dir/factory.bin"

echo "3/3 ligando o daemon"
host/install.sh
echo "pronto: em alguns segundos a placa sai de \"sem dados do Mac\" e mostra o seu uso."
if [ "$board" = cyd-2432s028r ]; then
  echo "na primeira vez a ESP32-2432S028R pede 4 toques com a caneta, um em cada canto, para calibrar o touch."
fi
