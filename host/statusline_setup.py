"""Liga (install) ou desliga (remove) a statusline do tokenpulse no Claude Code. Ela grava os limites de uso
(rate_limits, documentados) para o tokend.py e repassa a entrada à statusline que a pessoa já tinha, então o que
aparece no terminal não muda. O comando anterior fica guardado para o --uninstall devolver.

    statusline_setup.py install /caminho/host/statusline.sh
    statusline_setup.py remove
"""
import json
import os
import shlex
import sys

HOME = os.path.expanduser("~")
SETTINGS = os.path.join(os.environ.get("CLAUDE_CONFIG_DIR") or os.path.join(HOME, ".claude"), "settings.json")
PREV = os.path.join(HOME, ".claude", "esp-tokens-statusline-prev")


def load():
    try:
        with open(SETTINGS) as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


def save(settings):
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    with open(SETTINGS + ".tmp", "w") as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(SETTINGS + ".tmp", SETTINGS)


def ours(command):
    """O comando já é a statusline do tokenpulse (de qualquer instalação): o primeiro termo é host/statusline.sh."""
    try:
        first = shlex.split(command)[0]
    except (ValueError, IndexError):
        return False
    return first.endswith("/host/statusline.sh")


def install(script):
    settings = load()
    line = settings.get("statusLine") or {}
    current = line.get("command", "")
    if ours(current):  # já ligada: só aponta para a instalação de agora, mantendo a statusline encadeada
        chained = shlex.split(current)[1:]
        command = " ".join([shlex.quote(script)] + [shlex.quote(c) for c in chained])
        message = "statusline do tokenpulse já ligada"
    elif current:
        with open(PREV, "w") as f:
            f.write(current)
        command = f"{shlex.quote(script)} {shlex.quote(current)}"
        message = "statusline ligada (a sua continua aparecendo)"
    else:
        command = shlex.quote(script)
        message = "statusline ligada"
    settings["statusLine"] = {**line, "type": "command", "command": command}
    save(settings)
    print(message)


def remove():
    settings = load()
    line = settings.get("statusLine") or {}
    if not ours(line.get("command", "")):
        return
    try:
        with open(PREV) as f:
            previous = f.read().strip()
    except FileNotFoundError:
        previous = ""
    if previous:
        settings["statusLine"] = {**line, "command": previous}
    else:
        settings.pop("statusLine", None)
    save(settings)
    if os.path.exists(PREV):
        os.remove(PREV)


if __name__ == "__main__":
    try:
        install(sys.argv[2]) if sys.argv[1:2] == ["install"] else remove()
    except ValueError:  # settings.json com erro de sintaxe: não mexe e segue a instalação pela API
        print("statusline não ligada: o ~/.claude/settings.json tem um erro de sintaxe")
