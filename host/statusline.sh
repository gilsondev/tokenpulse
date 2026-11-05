#!/bin/bash
# statusLine do Claude Code: grava o JSON (com rate_limits) para o tokend.py e, se você já tinha uma
# statusline, repassa para ela. No ~/.claude/settings.json:
#   "statusLine": {"type": "command", "command": "/caminho/host/statusline.sh /caminho/da/sua-statusline.sh"}
input=$(cat)
printf '%s' "$input" > "$HOME/.claude/esp-tokens-status.json"
if [ -n "$1" ]; then printf '%s' "$input" | "$1"; fi
