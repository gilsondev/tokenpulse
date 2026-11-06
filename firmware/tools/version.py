"""Versão do firmware nos builds locais: o git describe (v0.4.0-2-gdb2a673-dirty), em vez de "dev <data>".

O CI passa a tag em PLATFORMIO_BUILD_FLAGS (-DFW_VERSION=...) e aí este script não faz nada. Sem git (código
baixado do release), também não: fica o "dev <data> <hora>" do código.
"""
import os
import subprocess

Import("env")  # noqa: F821 (definido pelo PlatformIO)

if "FW_VERSION" not in os.environ.get("PLATFORMIO_BUILD_FLAGS", ""):
    try:
        version = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], capture_output=True, text=True,
                                 cwd=env["PROJECT_DIR"], timeout=5).stdout.strip()  # noqa: F821
    except (OSError, subprocess.TimeoutExpired):
        version = ""
    if version:
        env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
