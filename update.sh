#!/bin/bash
# Atualiza pelo OTA, pelo mesmo cabo USB, todas as placas plugadas que estão atrás do último release.
# Diferente do setup.sh, mantém os ajustes e a calibração do toque.
#   ./update.sh
set -euo pipefail
cd "$(dirname "$0")"

boards=~/.claude/esp-tokens-boards.json  # o daemon grava aqui quem está conectado e com qual firmware
launchctl list | grep -q com.gilsondev.tokenpulse ||
  { echo "o daemon não está rodando: rode host/install.sh (ou ./setup.sh <placa> numa placa nova)" >&2; exit 1; }

tag=$(curl -fsSLI -o /dev/null -w '%{url_effective}' https://github.com/gilsondev/tokenpulse/releases/latest)
tag=${tag##*/}
echo "último release: $tag"

list=$(uv run -q python -c 'import json, sys
for name, b in json.load(open(sys.argv[1])).items():
    print(name, b["fw"].replace(" ", "_"))' "$boards" 2>/dev/null || true)
[ -n "$list" ] || { echo "nenhuma placa conectada ao daemon: confira o cabo e o log em ~/Library/Logs/tokenpulse.log" >&2; exit 1; }

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
failed=0
while read -r board fw; do
  if [ "$fw" = "$tag" ]; then
    echo "$board: já está na $tag"
    continue
  fi
  echo "$board: ${fw//_/ } -> $tag"
  curl -fsSL -o "$dir/$board.bin" "https://github.com/gilsondev/tokenpulse/releases/download/$tag/tokenpulse-$board-ota.bin"
  firmware/flash.sh "$board" --bin "$dir/$board.bin" </dev/null || failed=1
done <<<"$list"
exit $failed
