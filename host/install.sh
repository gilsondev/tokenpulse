#!/bin/bash
# Instala o daemon como LaunchAgent: sobe no login e volta sozinho se cair. Rode de novo para atualizar.
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
uv=$(command -v uv) || { echo "instale o uv antes: https://docs.astral.sh/uv/" >&2; exit 1; }
label=com.gilsondev.tokenpulse
plist=~/Library/LaunchAgents/$label.plist
mkdir -p ~/Library/LaunchAgents
sed -e "s|__UV__|$uv|" -e "s|__REPO__|$repo|" -e "s|__HOME__|$HOME|" "$repo/host/tokenpulse.plist.in" > "$plist"
launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$plist"
echo "daemon instalado; log em ~/Library/Logs/tokenpulse.log"
