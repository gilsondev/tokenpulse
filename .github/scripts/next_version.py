#!/usr/bin/env python3
"""Próxima versão do release pelo Conventional Commits, a partir dos commits desde a última tag v*.

Imprime `tag=vX.Y.Z` para o $GITHUB_OUTPUT, ou `tag=` quando não sai release:
  feat                              -> minor (v0.4.0 -> v0.5.0)
  fix, perf                         -> patch (v0.4.0 -> v0.4.1)
  tipo com ! ou BREAKING CHANGE:    -> major (v0.4.0 -> v1.0.0)
  docs, ci, chore, refactor, test…  -> nenhum release
Num push de tag v* (release feito à mão), devolve a própria tag. Fora de um push no main, não há release.
`python3 next_version.py --test` roda a conferência do cálculo.
"""
import os
import re
import subprocess
import sys

TITLE = re.compile(r"^(\w+)(\([^)]*\))?(!)?:\s")


def level(messages):
    """0 nenhum release, 1 patch, 2 minor, 3 major."""
    best = 0
    for msg in messages:
        m = TITLE.match(msg)
        if not m:
            continue
        if m.group(3) or re.search(r"^BREAKING[ -]CHANGE:", msg, re.M):
            return 3
        best = max(best, {"feat": 2, "fix": 1, "perf": 1}.get(m.group(1).lower(), 0))
    return best


def bump(tag, lvl):
    major, minor, patch = map(int, tag.lstrip("v").split("."))
    if lvl == 3:
        return f"v{major + 1}.0.0"
    if lvl == 2:
        return f"v{major}.{minor + 1}.0"
    return f"v{major}.{minor}.{patch + 1}"


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def main():
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/v"):
        return print(f"tag={ref.removeprefix('refs/tags/')}")
    if os.environ.get("GITHUB_EVENT_NAME") != "push" or ref != "refs/heads/main":
        return print("tag=")
    try:
        last = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*").strip()
        span = f"{last}..HEAD"
    except subprocess.CalledProcessError:  # repositório sem nenhuma tag ainda
        last, span = "v0.0.0", "HEAD"
    messages = [m.strip() for m in git("log", "--format=%B%x00", span).split("\0") if m.strip()]
    lvl = level(messages)
    print(f"tag={bump(last, lvl) if lvl else ''}")


def test():
    assert level(["docs: README", "ci: cache", "SmallTV: título sem prefixo"]) == 0
    assert level(["fix(smalltv): cor do contador", "docs: x"]) == 1
    assert level(["perf: faixas menores"]) == 1
    assert level(["fix: x", "feat(daemon): sessões pelo WiFi"]) == 2
    assert level(["feat!: frame novo"]) == 3
    assert level(["feat: x\n\nBREAKING CHANGE: o frame mudou"]) == 3
    assert level(["featuring: não é tipo"]) == 0
    assert bump("v0.4.0", 1) == "v0.4.1" and bump("v0.4.1", 2) == "v0.5.0" and bump("v0.5.3", 3) == "v1.0.0"
    print("ok")


if __name__ == "__main__":
    test() if "--test" in sys.argv else main()
