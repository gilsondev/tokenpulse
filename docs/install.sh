#!/bin/bash
# tokenpulse: liga no Mac (ou no Linux) o daemon que alimenta a placa. Para quem recebeu uma placa já gravada.
#   curl -fsSL --retry 3 --retry-all-errors https://gilsondev.github.io/tokenpulse/install.sh | bash                    instala ou atualiza
#   curl -fsSL --retry 3 --retry-all-errors https://gilsondev.github.io/tokenpulse/install.sh | bash -s -- --uninstall   remove
# Baixa o código do último release para ~/.local/share/tokenpulse, instala o uv se faltar e registra o
# daemon no login (LaunchAgent no macOS, systemd do usuário no Linux). Não grava firmware e não pede senha.
set -euo pipefail

main() {  # numa função: com curl | bash, nada roda antes de o script chegar inteiro
  local dir="$HOME/.local/share/tokenpulse" label=com.gilsondev.tokenpulse
  local os mac="Mac" log="~/Library/Logs/tokenpulse.log"
  os=$(uname)
  [ "$os" = Darwin ] || [ "$os" = Linux ] || { echo "o tokenpulse roda só no macOS e no Linux" >&2; exit 1; }
  [ "$os" = Linux ] && mac="computador" log="~/.local/state/tokenpulse/tokenpulse.log"

  if [ "${1:-}" = "--uninstall" ]; then
    if [ "$os" = Linux ]; then
      systemctl --user disable --now tokenpulse.service 2>/dev/null || true
      rm -f ~/.config/systemd/user/tokenpulse.service
    else
      launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
      rm -f ~/Library/LaunchAgents/$label.plist
    fi
    rm -rf "$dir"
    echo "tokenpulse removido. A placa volta para \"sem dados do Mac\"."
    return
  fi

  local ok="  ✓" warn="  !"
  echo "tokenpulse: instalando no $mac"
  echo
  if ! command -v uv >/dev/null; then
    curl -LsSf https://astral.sh/uv/install.sh | sh >/dev/null
    export PATH="$HOME/.local/bin:$PATH"
  fi
  echo "$ok uv pronto"

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
  echo "$ok versão $tag baixada"

  TOKENPULSE_QUIET=1 "$dir/host/install.sh"
  echo "$ok programa ligado: roda sozinho e volta a cada login"
  if [ "$os" = Linux ]; then  # a porta serial é do grupo dialout (Debian, Ubuntu, Fedora) ou uucp (Arch)
    local grp=dialout
    getent group dialout >/dev/null || grp="uucp"
    if id -nG | grep -qw "$grp"; then echo "$ok acesso à porta USB (grupo $grp)"
    else echo "$warn sem acesso à porta USB: rode \"sudo usermod -aG $grp \$USER\" e saia e entre de novo na sessão"; fi
  fi

  has_claude=$(PATH="$HOME/.local/bin:$PATH" command -v claude || true)
  if [ -n "$has_claude" ]; then
    if PATH="$HOME/.local/bin:$PATH" claude auth status >/dev/null 2>&1; then echo "$ok Claude Code logado"
    else echo "$warn Claude Code sem login: rode \"claude\" e entre na sua conta (sem isso a placa não mostra o % de uso)"; fi
  fi
  [ -d "$HOME/.codex/sessions" ] && echo "$ok Codex encontrado: a placa também mostra o uso dele"
  if [ -z "$has_claude" ] && [ ! -d "$HOME/.codex/sessions" ]; then
    echo "$warn não achei o Claude Code nem o Codex: a placa mostra o uso assim que um dos dois for usado"
  fi

  # espera a placa aparecer para dizer o próximo passo, em vez de "deve funcionar"
  echo
  printf "Procurando a placa"
  local boards="$HOME/.claude/esp-tokens-boards.json" found=""
  for _ in $(seq 1 30); do
    found=$("$(command -v /usr/bin/python3 python3 | head -1)" -c 'import json,sys; b=json.load(open(sys.argv[1])); print("\n".join(f"{k} {v.get(\"fw\",\"\")}" for k,v in b.items()))' "$boards" 2>/dev/null || true)
    [ -n "$found" ] && break
    printf "."
    sleep 2
  done
  echo
  if [ -n "$found" ]; then
    while read -r board fw; do
      case $board in
        guition-jc3248w535) name="Guition JC3248W535" ;;
        cyd-2432s028r) name="ESP32-2432S028R" ;;
        waveshare-amoled-216) name="Waveshare AMOLED 2.16" ;;
        waveshare-s3-lcd-147b) name="Waveshare 1.47B" ;;
        smalltv-ultra) name="SmallTV-Ultra" ;;
        *) name=$board ;;
      esac
      echo "$ok $name conectada (firmware $fw)"
    done <<< "$found"
    echo "    Se o firmware for mais antigo, ela se atualiza sozinha agora (não desligue). Depois ela pergunta o que mostrar."
  else
    echo "$warn nenhuma placa ainda. Plugue a placa no $mac com um cabo USB que passe dados."
    echo "    SmallTV: ligue na tomada; ele precisa estar no mesmo WiFi que este $mac."
    if [ "$os" = Linux ] && ! command -v avahi-browse >/dev/null; then
      echo "    No Linux, o SmallTV é achado pelo avahi-browse: instale o pacote avahi-utils (avahi no Arch)."
    fi
  fi

  echo
  echo "Pronto. Ajuda: tokenpulse.s.gy/comecar · log: $log"
  echo "Para remover: curl -fsSL https://gilsondev.github.io/tokenpulse/install.sh | bash -s -- --uninstall"
}

main "$@"
