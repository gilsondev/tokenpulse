#!/bin/bash
# statusLine do Claude Code: grava o JSON (com rate_limits) para o tokend.py e, se você já tinha uma
# statusline, repassa para ela. O install.sh liga sozinho (host/statusline_setup.py), guardando a anterior.
input=$(cat)
out="$HOME/.claude/esp-tokens-status.json"
printf '%s' "$input" > "$out.$$" && mv -f "$out.$$" "$out"  # atômico: várias sessões gravam a cada 30 s
if [ -n "$1" ]; then printf '%s' "$input" | sh -c "$1"; fi  # o comando como estava no settings.json, com argumentos
