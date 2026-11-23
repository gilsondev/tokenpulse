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
    monkeypatch.setattr(tokend, "_status", {"seen": None, "at": 0.0, "rl": {}, "org": "", "rose": 0.0})
    monkeypatch.setattr(tokend, "_newest", {})
    monkeypatch.setitem(tokend._org, "changed_at", 0.0)
    monkeypatch.setitem(tokend._org, "name", "")
    reset = time.time() + 3 * 3600

    def write(p5, age=0, sid="a", five=True):
        rl = {"seven_day": {"used_percentage": 5, "resets_at": reset}}
        if five:
            rl["five_hour"] = {"used_percentage": p5, "resets_at": reset}
        path.write_text(json.dumps({"session_id": sid, "rate_limits": rl}))
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


def test_idle_session_does_not_overwrite_newer_percent(status, monkeypatch):
    monkeypatch.setattr(tokend, "_api", {"five": None, "week": None, "at": 0.0, "err": "", "org": ""})
    status(37, age=90, sid="ativa")
    assert tokend.limits()["p5"] == 37
    status(2, age=80, sid="parada")  # o refreshInterval faz a sessão parada repetir o % de quando parou
    assert tokend.limits()["p5"] == 37
    status(0, age=70, sid="parada", five=False)  # janela de 5 h dela já expirou: sem o campo
    assert tokend.limits()["p5"] == 37
    status(38, age=60, sid="outra")
    assert tokend.limits()["p5"] == 38
    status(38, sid="mais-uma")  # a sessão que trouxe o 38 fechou: outra com o mesmo número mantém o dado novo
    assert tokend.limits()["age"] < 5


def test_api_wins_when_a_subagent_spends_past_the_statusline(status, monkeypatch):
    status(41)
    reset = json.loads(Path(tokend.STATUS).read_text())["rate_limits"]["five_hour"]["resets_at"]
    api = {"five": {"utilization": 42, "resets_at": datetime.fromtimestamp(reset + 30).astimezone().isoformat()},
           "week": {"utilization": 7}, "at": time.time() - 60, "err": "", "org": ""}
    monkeypatch.setattr(tokend, "_api", api)
    assert tokend.status_fresh() and tokend.limits()["p5"] == 42
    api["five"]["resets_at"] = datetime.fromtimestamp(reset - 5 * 3600).astimezone().isoformat()  # janela anterior
    assert tokend.limits()["p5"] == 41


def test_stalled_only_with_tokens_and_a_frozen_statusline(monkeypatch):
    monkeypatch.setitem(tokend._tokens, "at", time.time() - 10)
    monkeypatch.setitem(tokend._status, "rose", time.time() - tokend.STALL - 1)
    assert tokend.stalled()
    monkeypatch.setitem(tokend._status, "rose", time.time() - 30)
    assert not tokend.stalled()
    monkeypatch.setitem(tokend._status, "rose", 0.0)
    monkeypatch.setitem(tokend._tokens, "at", time.time() - 600)
    assert not tokend.stalled()


def test_statusline_percent_belongs_to_the_org_with_that_weekly_reset(tmp_path, monkeypatch):
    path = tmp_path / "status.json"
    monkeypatch.setattr(tokend, "STATUS", str(path))
    monkeypatch.setattr(tokend, "_status", {"seen": None, "at": 0.0, "rl": {}, "org": "", "rose": 0.0})
    monkeypatch.setattr(tokend, "_newest", {})
    monkeypatch.setattr(tokend, "_per_org", {})
    monkeypatch.setitem(tokend._org, "name", "Jus Team")
    team, jus = time.time() + 6 * 86400, time.time() + 86400
    monkeypatch.setattr(tokend, "_week_reset", {"Jus Team": team, "Jus": jus})

    def write(week_reset, p5, age):
        path.write_text(json.dumps({"rate_limits": {"five_hour": {"used_percentage": p5, "resets_at": time.time() + 3600},
                                                    "seven_day": {"used_percentage": 5, "resets_at": week_reset + 60}}}))
        os.utime(path, (time.time() - age, time.time() - age))
        return tokend.limits()

    out = write(jus, 30, age=20)  # a sessão aberta segue gastando a Jus depois de logar na Jus Team
    assert out["org"] == "Jus" and out["p5"] == 30
    out = write(team, 2, age=10)
    assert out["org"] == "Jus Team" and out["p5"] == 2
    assert tokend._per_org["Jus"]["p5"] == 30


def test_cswap_list_gives_each_org_weekly_reset(tmp_path, monkeypatch):
    fake = tmp_path / "cswap"
    fake.write_text("""#!/bin/sh
cat <<'OUT'
Accounts:
  1: conta1 (x) [Jus Team] (active)
     ├ 5h:      0%   resets 20:00         in 4h 10m
     ├ 7d:      2%   resets Nov 23 06:00  in 6d 14h
  2: conta2 (y) [Jus]
     ├ 7d:     30%   resets Nov 17 00:00  in 8h 10m
OUT
""")
    fake.chmod(0o755)
    monkeypatch.setattr(tokend, "CSWAP", str(fake))
    monkeypatch.setattr(tokend, "_week_reset", {})
    tokend._refresh_accounts()
    assert abs(tokend._week_reset["Jus Team"] - (time.time() + 6 * 86400 + 14 * 3600)) < 5
    assert abs(tokend._week_reset["Jus"] - (time.time() + 8 * 3600 + 10 * 60)) < 5
    assert [r[:3] for r in tokend._accounts["list"]] == [[1, "Jus Team", 1], [2, "Jus", 0]]


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
    assert line["refreshInterval"] == setup.REFRESH
    setup.install("/novo/host/statusline.sh")  # reinstalar não encadeia duas vezes
    assert json.loads(claude_home.read_text())["statusLine"]["command"] == "/novo/host/statusline.sh '/x/s.sh --a 2'"
    setup.remove()
    settings = json.loads(claude_home.read_text())
    assert settings["statusLine"]["command"] == "/x/s.sh --a 2" and settings["model"] == "opus"
    assert "refreshInterval" not in settings["statusLine"]


def test_statusline_keeps_own_refresh_interval(claude_home):
    claude_home.write_text(json.dumps({"statusLine": {"type": "command", "command": "/x/s.sh", "refreshInterval": 5}}))
    setup.install("/opt/tp/host/statusline.sh")
    assert json.loads(claude_home.read_text())["statusLine"]["refreshInterval"] == 5
    setup.remove()
    assert json.loads(claude_home.read_text())["statusLine"] == {"type": "command", "command": "/x/s.sh", "refreshInterval": 5}


def test_statusline_without_previous(claude_home):
    setup.install("/opt/tp/host/statusline.sh")
    assert json.loads(claude_home.read_text())["statusLine"]["command"] == "/opt/tp/host/statusline.sh"
    setup.remove()
    assert "statusLine" not in json.loads(claude_home.read_text())


def test_no_account_switch_without_cswap(monkeypatch, tmp_path):
    monkeypatch.setattr(tokend, "CSWAP", str(tmp_path / "cswap"))
    assert "accts" not in tokend.account_frame()
    (tmp_path / "cswap").touch()
    monkeypatch.setattr(tokend, "accounts_frame", lambda: [])
    assert tokend.account_frame()["accts"] == []
