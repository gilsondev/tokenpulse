#!/bin/bash
# tokenpulse: liga no Mac o daemon que alimenta a placa. Para quem recebeu uma placa já gravada.
#   curl -fsSL --retry 3 --retry-all-errors https://gilsondev.github.io/tokenpulse/install.sh | bash                    instala ou atualiza
#   curl -fsSL --retry 3 --retry-all-errors https://gilsondev.github.io/tokenpulse/install.sh | bash -s -- --uninstall   remove
# Baixa o código do último release para ~/.local/share/tokenpulse, instala o uv se faltar e registra o
# daemon no login do macOS. Não grava firmware e não pede senha.
set -euo pipefail

main() {  # numa função: com curl | bash, nada roda antes de o script chegar inteiro
  local dir="$HOME/.local/share/tokenpulse" label=com.gilsondev.tokenpulse
  [ "$(uname)" = Darwin ] || { echo "o tokenpulse roda só no macOS" >&2; exit 1; }

  if [ "${1:-}" = "--uninstall" ]; then
    launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
    rm -f ~/Library/LaunchAgents/$label.plist
    rm -rf "$dir"
    echo "tokenpulse removido. A placa volta para \"sem dados do Mac\"."
    return
  fi

  echo "1/4 conferindo o uv"
  if ! command -v uv >/dev/null; then
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
  fi

  echo "2/4 baixando o último release"
  local tag
  tag=$(curl -fsSLI --retry 3 --retry-all-errors -o /dev/null -w '%{url_effective}' https://github.com/gilsondev/tokenpulse/releases/latest)
  tag=${tag##*/}
  local tmp
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  curl -fsSL --retry 3 --retry-all-errors "https://github.com/gilsondev/tokenpulse/archive/refs/tags/$tag.tar.gz" | tar -xz -C "$tmp" --strip-components=1
  rm -rf "$dir"
  mkdir -p "$(dirname "$dir")"
  mv "$tmp" "$dir"
  trap - EXIT
  echo "$tag" > "$dir/VERSION"  # o tokend mostra esta versão na página da placa

  echo "3/4 ligando o daemon ($tag)"
  "$dir/host/install.sh"

  echo "4/4 conferindo o Claude Code e o Codex"
  has_claude=$(PATH="$HOME/.local/bin:$PATH" command -v claude || true)
  if [ -n "$has_claude" ] && ! PATH="$HOME/.local/bin:$PATH" claude auth status >/dev/null 2>&1; then
    echo "  aviso: o Claude Code não parece logado. Rode \"claude\" e faça login; sem isso a placa não mostra o % de uso do Claude."
  fi
  [ -d "$HOME/.codex/sessions" ] && echo "  Codex encontrado: a placa também mostra o uso dele."
  if [ -z "$has_claude" ] && [ ! -d "$HOME/.codex/sessions" ]; then
    echo "  aviso: não achei o Claude Code nem o Codex neste Mac. A placa mostra o uso assim que um dos dois for usado."
  fi

  echo
  echo "pronto: em alguns segundos a placa sai de \"sem dados do Mac\"."
  echo "placa USB: deixe plugada. SmallTV: ele precisa estar no mesmo WiFi que este Mac."
  echo "log: ~/Library/Logs/tokenpulse.log · para remover: curl -fsSL --retry 3 --retry-all-errors https://gilsondev.github.io/tokenpulse/install.sh | bash -s -- --uninstall"
}

main "$@"
