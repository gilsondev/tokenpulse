"""Testes do daemon e do instalador da statusline: só o que decide o que a placa mostra ou o que fica gravado.
Rode na raiz do repositório: uv run --with pytest --with pyserial --with bleak pytest host/tests"""
import importlib.util
import json
import os
import stat
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

import pytest

HOST = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, HOST / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


tokend = load("tokend")
setup = load("statusline_setup")


def test_vtuple_only_releases():
    assert tokend.vtuple("v0.22.2") == (0, 22, 2)
    assert tokend.vtuple("v0.22.2-dirty") is None  # firmware de desenvolvimento: o daemon não regrava
    assert tokend.vtuple("") is None
    assert tokend.vtuple("v0.10.0") > tokend.vtuple("v0.9.9")


def test_clock():
    assert tokend.clock(datetime(2026, 1, 1, 15, 0)) == "15h"
    assert tokend.clock(datetime(2026, 1, 1, 9, 5)) == "9h05"


def test_window_fields_future_reset():
    now = datetime(2026, 11, 12, 14, 0)
    out = tokend.window_fields({"used_percentage": 41.6, "resets_at": (now + timedelta(hours=3)).timestamp()},
                               {"used_percentage": 7}, now)
    assert out["p5"] == 42 and out["pw"] == 7 and out["r5"].startswith(("Reset às", "Acaba"))


def test_window_fields_after_reset_waits_for_data():
    now = datetime(2026, 11, 12, 14, 0)
    out = tokend.window_fields({"used_percentage": 90, "resets_at": (now - timedelta(minutes=1)).timestamp()}, {}, now)
    assert out["p5"] == -1 and out["r5"] == "aguardando o 1º dado"  # o % gravado é da janela anterior


def test_encode_frame_drops_sessions_to_fit():
    rows = [["sessão longa " * 20, "work", 1] for _ in range(200)]
    line = tokend.encode_frame({"p5": 10, "sess": {"rows": rows}})
    assert len(line) <= tokend.FRAME_MAX and line.endswith(b"\n")
    assert json.loads(line)["p5"] == 10 and 0 < len(json.loads(line)["sess"]["rows"]) < 200


@pytest.mark.parametrize("networks, expected", [
    (("Casa", "2", {"Casa"}), ("Casa", True)),                            # computador já em 2,4 GHz
    (("Casa", "5", {"Casa"}), ("Casa", True)),                            # mesmo nome nas duas bandas
    (("Casa_5G", "5", {"Casa", "Vizinho"}), ("Casa", True)),              # o roteador separa as bandas
    (("Casa_5G", "5", set()), ("Casa", False)),                           # palpite: a pessoa confirma
    ((None, None, set()), (None, False)),                                 # nome oculto pelo macOS
])
def test_wifi_for_board(monkeypatch, networks, expected):
    monkeypatch.setattr(tokend, "wifi_networks", lambda: networks)
    assert tokend.wifi_for_board() == expected


@pytest.fixture
def status(tmp_path, monkeypatch):
    path = tmp_path / "status.json"
    monkeypatch.setattr(tokend, "STATUS", str(path))
    monkeypatch.setitem(tokend._org, "changed_at", 0.0)
    monkeypatch.setitem(tokend._org, "name", "")

    def write(p5, age=0):
        reset = time.time() + 3 * 3600
        path.write_text(json.dumps({"rate_limits": {"five_hour": {"used_percentage": p5, "resets_at": reset},
                                                    "seven_day": {"used_percentage": 5, "resets_at": reset}}}))
        os.utime(path, (time.time() - age, time.time() - age))
    return write


def test_statusline_wins_over_api_when_fresh(status, monkeypatch):
    status(33)
    monkeypatch.setattr(tokend, "_api", {"five": {"utilization": 80}, "week": {"utilization": 9}, "at": time.time() + 5,
                                         "err": "", "org": ""})
    assert tokend.status_fresh() and tokend.limits()["p5"] == 33


def test_api_is_the_fallback_when_statusline_is_old(status, monkeypatch):
    status(33, age=tokend.STATUS_FRESH + 60)
    monkeypatch.setattr(tokend, "_api", {"five": {"utilization": 80, "resets_at": None}, "week": {"utilization": 9},
                                         "at": time.time(), "err": "", "org": ""})
    assert not tokend.status_fresh() and tokend.limits()["p5"] == 80


def test_board_token_file_is_private(tmp_path, monkeypatch):
    monkeypatch.setattr(tokend, "KEYS", str(tmp_path / "keys.json"))
    tokend.save_token("tokenpulse-caixeta.local", "ab" * 16)
    assert tokend.board_token("tokenpulse-caixeta.local") == "ab" * 16
    assert stat.S_IMODE(os.stat(tmp_path / "keys.json").st_mode) == 0o600
    headers = tokend.auth_headers("tokenpulse-caixeta.local")
    assert headers["X-Token"] == "ab" * 16 and headers["Authorization"].startswith("Basic ")
    assert tokend.auth_headers("outra.local") == {}


@pytest.fixture
def claude_home(tmp_path, monkeypatch):
    monkeypatch.setattr(setup, "SETTINGS", str(tmp_path / "settings.json"))
    monkeypatch.setattr(setup, "PREV", str(tmp_path / "prev"))
    return tmp_path / "settings.json"


def test_statusline_chains_and_restores(claude_home):
    claude_home.write_text(json.dumps({"model": "opus", "statusLine": {"type": "command", "command": "/x/s.sh --a 2",
                                                                       "padding": 1}}))
    setup.install("/opt/tp/host/statusline.sh")
    line = json.loads(claude_home.read_text())["statusLine"]
    assert line["command"] == "/opt/tp/host/statusline.sh '/x/s.sh --a 2'" and line["padding"] == 1
    setup.install("/novo/host/statusline.sh")  # reinstalar não encadeia duas vezes
    assert json.loads(claude_home.read_text())["statusLine"]["command"] == "/novo/host/statusline.sh '/x/s.sh --a 2'"
    setup.remove()
    settings = json.loads(claude_home.read_text())
    assert settings["statusLine"]["command"] == "/x/s.sh --a 2" and settings["model"] == "opus"


def test_statusline_without_previous(claude_home):
    setup.install("/opt/tp/host/statusline.sh")
    assert json.loads(claude_home.read_text())["statusLine"]["command"] == "/opt/tp/host/statusline.sh"
    setup.remove()
    assert "statusLine" not in json.loads(claude_home.read_text())
