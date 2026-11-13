#!/bin/bash
# Instala o daemon como LaunchAgent (macOS) ou serviço do systemd do usuário (Linux): sobe no login e volta
# sozinho se cair. Rode de novo para atualizar.
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
uv=$(command -v uv) || { echo "instale o uv antes: https://docs.astral.sh/uv/" >&2; exit 1; }
# o % de uso vem da statusline do Claude Code (documentada); a API da Anthropic fica de reserva
msg=$("$uv" run -q --no-project "$repo/host/statusline_setup.py" install "$repo/host/statusline.sh" 2>/dev/null || true)
[ -n "$msg" ] && { [ -n "${TOKENPULSE_QUIET:-}" ] && echo "  ✓ $msg" || echo "$msg"; }
if [ "$(uname)" = Linux ]; then
  unit=~/.config/systemd/user/tokenpulse.service
  mkdir -p ~/.config/systemd/user ~/.local/state/tokenpulse
  sed -e "s|__UV__|$uv|" -e "s|__REPO__|$repo|" -e "s|__HOME__|$HOME|g" "$repo/host/tokenpulse.service.in" > "$unit"
  systemctl --user daemon-reload
  systemctl --user enable tokenpulse.service >/dev/null 2>&1
  systemctl --user restart tokenpulse.service
  [ -n "${TOKENPULSE_QUIET:-}" ] || echo "daemon instalado; log em ~/.local/state/tokenpulse/tokenpulse.log"
  exit 0
fi
label=com.gilsondev.tokenpulse
plist=~/Library/LaunchAgents/$label.plist
mkdir -p ~/Library/LaunchAgents
sed -e "s|__UV__|$uv|" -e "s|__REPO__|$repo|" -e "s|__HOME__|$HOME|" "$repo/host/tokenpulse.plist.in" > "$plist"
launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
# o bootout volta antes de o serviço sair; subir de novo nessa janela dá "Bootstrap failed: 5"
for _ in $(seq 1 20); do launchctl print "gui/$(id -u)/$label" >/dev/null 2>&1 || break; sleep 0.25; done
launchctl bootstrap "gui/$(id -u)" "$plist"
[ -n "${TOKENPULSE_QUIET:-}" ] || echo "daemon instalado; log em ~/Library/Logs/tokenpulse.log"
