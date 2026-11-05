#!/bin/bash
# Tira um screenshot da placa pelo daemon. Os comandos rodam antes, na ordem, com 1,5 s entre eles.
#   ./shot.sh saida.png [comando ...]    comandos: "page N", "tap X Y", "swipe L", "swipe R"
#   ./shot.sh /tmp/sessoes.png "page 1"
# Placa: o default_envs do platformio.ini; mude com BOARD=nome.
# Por padrão o daemon só captura em modo demonstração (tokend.py --demo); REAL=1 captura os seus dados reais.
set -e
out=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
board=${BOARD:-$(grep -m1 '^default_envs' "$(dirname "$0")/../firmware/platformio.ini" | cut -d= -f2 | tr -d ' ')}
mkdir -p ~/.claude/esp-tokens-shot
req=~/.claude/esp-tokens-shot/"$board"
trap 'rm -f "$req" "$req.tmp"' EXIT  # pedido esquecido rodaria os toques no próximo plug
rm -f "$out"
python3 -c 'import json,os,sys; print(json.dumps({"cmds": sys.argv[2:], "out": sys.argv[1], "real": os.environ.get("REAL") == "1"}))' "$out" "$@" > "$req.tmp"
mv "$req.tmp" "$req"
for _ in $(seq 1 60); do
  [ -s "$out" ] && { echo "$out"; exit; }
  sleep 1
done
echo "sem screenshot em 60 s; veja o log do daemon" >&2
exit 1
