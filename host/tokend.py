# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5", "bleak>=0.22"]
# ///
"""tokenpulse: lê o uso de tokens do Claude Code (CLI e Desktop) e envia 1 linha JSON por segundo
para as telas ESP32 ligadas na USB. `--once` imprime um frame; `--demo` manda só dados fictícios."""
import asyncio
import base64
import glob
import hashlib
import hmac
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from collections import namedtuple
from datetime import datetime, timedelta

import serial
from serial.tools import list_ports


def agent_version():
    """Versão deste tokend: o VERSION que o instalador de uma linha grava ao lado do código, ou o git describe
    de quem roda do clone (v0.4.0-3-gdb2a673-dirty)."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        return open(os.path.join(root, "VERSION")).read().strip()
    except OSError:
        pass
    try:
        return subprocess.run(["git", "-C", root, "describe", "--tags", "--always", "--dirty"],
                              capture_output=True, text=True, timeout=5).stdout.strip() or "dev"
    except (OSError, subprocess.TimeoutExpired):
        return "dev"


def log(msg):
    print(f"{datetime.now():%d/%m %H:%M:%S} {msg}", file=sys.stderr, flush=True)

HOME = os.path.expanduser("~")
MAC = sys.platform == "darwin"  # no Linux: xdg-open, gsettings, avahi e o token num arquivo, sem trazer app à frente
OPEN = "open" if MAC else "xdg-open"
LOG_DIR = os.path.join(HOME, "Library/Logs" if MAC else ".local/state/tokenpulse")
PROJECTS = os.path.join(HOME, ".claude/projects/**/*.jsonl")  # ** inclui <sessão>/subagents/
STATUS = os.path.join(HOME, ".claude/esp-tokens-status.json")
ACCOUNT = os.path.join(HOME, ".claude.json")
SHOT_DIR = os.path.join(HOME, ".claude/esp-tokens-shot")  # o shot.sh grava <placa> com {"cmds": [...], "out": "x.png"}
OTA_DIR = os.path.join(HOME, ".claude/esp-tokens-ota")  # o flash.sh grava <placa> com o caminho do .bin
NET_ACTIONS = {"prov", "mc", "mcws", "lskip", "update"}  # o que a página do SmallTV pede; trocar de conta é só no cabo
KEYS = os.path.join(HOME, ".claude/esp-tokens-keys.json")  # o token de cada placa de WiFi, entregue no /hello; só este usuário lê
BOARDS = os.path.join(HOME, ".claude/esp-tokens-boards.json")  # placas conectadas e o firmware de cada uma (update.sh)
# Placas com Bluetooth que já estiveram na USB deste Mac: o daemon só conecta sem fio a elas, nunca à de um colega
BLE_KNOWN = os.path.join(HOME, ".claude/esp-tokens-ble.json")
# placas com USB nativo (usbmodem) e com ponte USB-serial CP210x/CH34x
# placas: USB nativo do ESP32 (Espressif) e pontes USB-serial CP210x/CH34x; outros seriais nem são abertos
ESPRESSIF_VID = 0x303A
BOARD_VIDS = {ESPRESSIF_VID, 0x10C4, 0x1A86}
WEEKDAYS = ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"]
Event = namedtuple("Event", "ts total inp out cread cwrite entry session model project cost")
HISTORY_DAYS = 14

# US$ por milhão de tokens: entrada, saída, leitura de cache. Escrita de cache: 1,25x a entrada (5 min)
# ou 2x (1 h). Preços da API, não da assinatura: é o custo equivalente se o mesmo uso fosse cobrado
# por token. Fonte: tabela de modelos da Claude API em 2026-10. Ordem importa: o prefixo mais longo primeiro.
PRICES = [
    ("claude-fable-5-1", 10, 50, 0.25), ("claude-fable-5", 10, 50, 1.0), ("claude-mythos", 10, 50, 0.25),
    ("claude-opus-5-5", 4, 20, 0.20), ("claude-opus", 5, 25, 0.50),
    ("claude-sonnet-5", 2, 10, 0.20), ("claude-sonnet", 3, 15, 0.30),
    ("claude-haiku-5", 0.10, 0.50, 0.01), ("claude-haiku", 1, 5, 0.10),
]


def cost_usd(model_id, u):
    for prefix, inp, out, cread in PRICES:
        if (model_id or "").startswith(prefix):
            break
    else:
        return 0.0  # <synthetic> e modelos desconhecidos
    cc = u.get("cache_creation") or {}
    w5, w1h = cc.get("ephemeral_5m_input_tokens"), cc.get("ephemeral_1h_input_tokens")
    if w5 is None and w1h is None:
        w5, w1h = u.get("cache_creation_input_tokens") or 0, 0
    return ((u.get("input_tokens") or 0) * inp + (u.get("output_tokens") or 0) * out
            + (u.get("cache_read_input_tokens") or 0) * cread + (w5 or 0) * inp * 1.25 + (w1h or 0) * inp * 2) / 1e6


def fmt_usd(v):
    return f"US$ {v:,.0f}".replace(",", ".") if v >= 10 else f"US$ {v:.2f}".replace(".", ",")


_EMAIL = re.compile(r"\S+@\S+")


def scrub(text):
    """Tira e-mails do que vai para a placa e para o log (org pessoal se chama "<email>'s Organization")."""
    return _EMAIL.sub("…", text or "")


def cut(text, nbytes):
    """Corta em bytes de UTF-8, não em caracteres: emoji ocupa 4 B no buffer serial da placa."""
    return text.encode()[:nbytes].decode(errors="ignore")


def fmt_tokens(n):
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M".replace(".", ",")
    if n >= 1_000:
        return f"{n / 1_000:.0f}k"
    return str(int(n))


def model_label(model_id, effort):
    # claude-opus-5-5 -> Opus 5.5
    parts = (model_id or "").removeprefix("claude-").split("-")
    name = parts[0].capitalize() + (" " + ".".join(p for p in parts[1:3] if p.isdigit()) if len(parts) > 1 else "")
    return f"{name} · {effort}" if effort else name


def parse_reset(v):
    if isinstance(v, (int, float)):
        return datetime.fromtimestamp(v)
    if isinstance(v, str):
        return datetime.fromisoformat(v.replace("Z", "+00:00")).astimezone().replace(tzinfo=None)
    return None


def clock(dt):
    """3h, 14h30: o horário local do jeito que se fala."""
    return f"{dt.hour}h{dt.minute:02d}".replace("h00", "h")


class Usage:
    def __init__(self):
        self.offsets = {}
        self.seen = {}  # (message.id, requestId) -> ts, para não contar a mesma mensagem duas vezes
        self.events = []
        self.last_model, self.last_ts = "", 0.0
        self.sessions = {}  # sessionId -> última linha: ts, tipo, stop_reason, projeto, origem
        self.titles = {}    # sessionId -> título da sessão (linha custom-title do Claude Code)
        self.paths, self.globbed_at = [], 0.0
        self.recent = False  # algum log do Claude Code mexido nos últimos 7 dias

    def poll(self):
        cutoff = time.time() - 86400
        week_ago, recent = time.time() - 7 * 86400, False
        # o glob recursivo em ~1.240 arquivos custa ~300 ms; arquivo novo de sessão aparece no máximo a cada 15 s
        if time.time() - self.globbed_at > 15:
            self.paths, self.globbed_at = glob.glob(PROJECTS, recursive=True), time.time()
        for path in self.paths:
            try:
                st = os.stat(path)
            except OSError:
                continue
            recent = recent or st.st_mtime >= week_ago
            if st.st_mtime < cutoff:
                continue
            if st.st_size < self.offsets.get(path, 0):
                self.offsets[path] = 0  # arquivo reescrito menor: relê do início; o seen evita contar de novo
            with open(path, "rb") as f:
                f.seek(self.offsets.get(path, 0))
                for raw in f:
                    if not raw.endswith(b"\n"):
                        break  # linha ainda sendo escrita
                    self.offsets[path] = f.tell()
                    self._ingest(raw)
        self.recent = recent
        self.events = [e for e in self.events if e.ts >= cutoff]
        self.seen = {k: ts for k, ts in self.seen.items() if ts >= cutoff}
        self.sessions = {k: v for k, v in self.sessions.items() if v["ts"] >= cutoff}
        self.titles = {k: v for k, v in self.titles.items() if k in self.sessions}

    def _ingest(self, raw):
        # uma linha fora do formato não pode derrubar o daemon: ele relê o mesmo arquivo ao voltar
        try:
            d = json.loads(raw)
            self._track_session(d)
            self._ingest_line(d)
        except (ValueError, TypeError, KeyError, AttributeError) as e:
            log(f"linha ignorada: {e!r}")

    def _track_session(self, d):
        sid, kind = d.get("sessionId"), d.get("type")
        if kind == "custom-title" and sid and d.get("customTitle"):
            self.titles[sid] = d["customTitle"]
            return
        if not sid or kind not in ("user", "assistant") or not d.get("timestamp"):
            return
        ts = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).timestamp()
        cur = self.sessions.get(sid)
        if cur and cur["ts"] > ts:
            return
        msg = d.get("message") if isinstance(d.get("message"), dict) else {}
        q = (cur or {}).get("q", "")
        if kind == "assistant":
            q = ask_summary(msg.get("content")) or q
        self.sessions[sid] = {"ts": ts, "kind": kind, "stop": msg.get("stop_reason"),
                              "project": os.path.basename((d.get("cwd") or "").rstrip("/")) or "?", "cwd": d.get("cwd") or "",
                              "entry": d.get("entrypoint", "cli"), "q": q}

    def _ingest_line(self, d):
        msg = d.get("message")
        if d.get("type") != "assistant" or not isinstance(msg, dict) or not msg.get("usage"):
            return
        key = (msg.get("id"), d.get("requestId"))
        if key in self.seen:  # o Claude Code grava uma linha por bloco com o mesmo usage
            return
        u = msg["usage"]
        inp, out = u.get("input_tokens") or 0, u.get("output_tokens") or 0
        cread, cwrite = u.get("cache_read_input_tokens") or 0, u.get("cache_creation_input_tokens") or 0
        ts = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).timestamp()
        self.seen[key] = ts
        project = os.path.basename((d.get("cwd") or "").rstrip("/")) or "?"
        e = Event(ts, inp + out + cread + cwrite, inp, out, cread, cwrite, d.get("entrypoint", "cli"),
                  d.get("sessionId"), model_label(msg.get("model"), None), project, cost_usd(msg.get("model"), u))
        if ts >= self.last_ts:
            self.last_ts, self.last_model = ts, model_label(msg.get("model"), d.get("effort"))
        self.events.append(e)

    def snapshot(self):
        now = time.time()
        midnight = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
        today = [e for e in self.events if e.ts >= midnight]
        spark = minute_spark(self.events, now)
        active = {(e.entry, e.session) for e in self.events if now - e.ts < 600}
        return {
            "m": self.last_model or "Claude",
            "tot": fmt_tokens(sum(e.total for e in today)),
            "sub": f"out {fmt_tokens(sum(e.out for e in today))} · cache {fmt_tokens(sum(e.cread + e.cwrite for e in today))}",
            "rate": fmt_tokens(sum(spark[-5:]) / 5),
            "sp": spark,
            "cli": sum(1 for a in active if a[0] != "claude-desktop"),
            "desk": sum(1 for a in active if a[0] == "claude-desktop"),
            "mods": models(today),
            "projs": projects([e for e in self.events if now - e.ts < 3600], now),
            "sess": sessions(self.sessions, self.titles, herdr_agents(), self.events, now),
            "hist": history_rows(today),
        }


def minute_spark(events, now):
    """Tokens por minuto na última hora, do mais antigo ao atual."""
    spark = [0] * 60
    for e in events:
        age = int((now - e.ts) // 60)
        if 0 <= age < 60:
            spark[59 - age] += e.total
    return spark


CODEX_SESSIONS = os.path.join(HOME, ".codex/sessions/**/rollout-*.jsonl")
CODEX_INDEX = os.path.join(HOME, ".codex/session_index.jsonl")  # id -> thread_name, o nome que o app mostra


class CodexUsage:
    """Os logs do Codex (app e CLI). Os tokens saem da diferença do acumulado de cada sessão, porque o mesmo
    token_count às vezes se repete. O % das janelas é o do último token_count: só muda quando o Codex é usado."""

    def __init__(self):
        self.offsets, self.files = {}, {}  # caminho -> modelo, esforço, pasta, sessão e o último acumulado
        self.events, self.limits, self.limits_ts = [], None, 0.0
        self.last_model, self.last_ts = "", 0.0
        self.paths, self.globbed_at, self.recent = [], 0.0, False
        self.sessions = {}  # id -> última atividade, se há tarefa rodando, se terminou alguma, pasta
        self.names, self.names_at = {}, 0.0
        self.last_use = 0.0  # o log do Codex mexido por último, em qualquer data

    def poll(self):
        now = time.time()
        if now - self.globbed_at > 15:
            self.paths, self.globbed_at = glob.glob(CODEX_SESSIONS, recursive=True), now
        # 7 dias: o % da semana pode estar num arquivo de dias atrás; e sem uso nesse prazo o Codex some da placa
        week_ago, self.recent, self.last_use = now - 7 * 86400, False, 0.0
        for path in self.paths:
            try:
                st = os.stat(path)
            except OSError:
                continue
            self.last_use = max(self.last_use, st.st_mtime)
            if st.st_mtime < week_ago:
                continue
            self.recent = True
            if st.st_size < self.offsets.get(path, 0):
                self.offsets[path], self.files[path] = 0, {}
            with open(path, "rb") as f:
                f.seek(self.offsets.get(path, 0))
                for raw in f:
                    if not raw.endswith(b"\n"):
                        break
                    self.offsets[path] = f.tell()
                    try:
                        self._ingest(self.files.setdefault(path, {}), json.loads(raw))
                    except (ValueError, TypeError, KeyError, AttributeError) as e:
                        log(f"codex: linha ignorada: {e!r}")
        self.events = [e for e in self.events if e.ts >= now - 86400]
        self.sessions = {k: v for k, v in self.sessions.items() if v["ts"] >= now - 86400}

    def session_items(self, now):
        """Sessões com atividade na última hora: (estado, -última atividade, linha de sess.rows, id curto).
        Sem pedido de aprovação: os logs do Codex não registram esse pedido."""
        try:
            if os.path.getmtime(CODEX_INDEX) != self.names_at:
                self.names_at = os.path.getmtime(CODEX_INDEX)
                with open(CODEX_INDEX) as f:
                    self.names = {d["id"]: d.get("thread_name") or "" for d in map(json.loads, f) if d.get("id")}
        except (OSError, ValueError, KeyError, TypeError):
            pass
        tokens = {}
        for e in self.events:
            tokens[e.session] = tokens.get(e.session, 0) + e.total
        items = []
        for sid, s in self.sessions.items():
            age = now - s["ts"]
            if age > 3600:
                continue
            # tarefa rodando há mais de 30 min sem nenhuma linha nova: o app fechou no meio
            st = "work" if s["busy"] and age < 1800 else "turn" if s["done"] else "idle"
            name = self.names.get(sid) or (s["project"] if s["project"] != "?" else "Codex")
            items.append((st, -s["ts"], [cut(name, 48), "Codex", st, fmt_tokens(tokens[sid]) if sid in tokens else "--",
                                         int(age // 60)], sid[:8]))
        return items

    def _ingest(self, f, d):
        p, kind = d.get("payload") or {}, d.get("type")
        if kind == "session_meta":
            f.update(sid=p.get("id") or "", cwd=p.get("cwd") or "")
            return
        if not d.get("timestamp") or not f.get("sid"):
            return
        ts = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).timestamp()
        s = self.sessions.setdefault(f["sid"], {"ts": 0.0, "busy": False, "done": False})
        s["ts"], s["project"] = max(s["ts"], ts), os.path.basename((f.get("cwd") or "").rstrip("/")) or "?"
        if kind == "event_msg" and p.get("type") in ("task_started", "task_complete"):
            s["busy"] = p["type"] == "task_started"
            s["done"] = s["done"] or not s["busy"]
        elif kind == "turn_context":
            f.update(model=p.get("model") or f.get("model", ""), effort=p.get("effort") or "", cwd=p.get("cwd") or f.get("cwd", ""))
        elif kind == "event_msg" and p.get("type") == "token_count":
            ts = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).timestamp()
            if p.get("rate_limits") and ts >= self.limits_ts:
                self.limits, self.limits_ts = p["rate_limits"], ts
            tot = (p.get("info") or {}).get("total_token_usage")
            if not tot:
                return
            prev = f.get("prev") or {}
            if (tot.get("total_tokens") or 0) < (prev.get("total_tokens") or 0):
                prev = {}  # o acumulado recomeçou
            delta = {k: (tot.get(k) or 0) - (prev.get(k) or 0)
                     for k in ("total_tokens", "input_tokens", "cached_input_tokens", "output_tokens")}
            f["prev"] = tot
            if delta["total_tokens"] <= 0:
                return
            model = f.get("model") or "Codex"
            self.events.append(Event(ts, delta["total_tokens"], delta["input_tokens"] - delta["cached_input_tokens"],
                                     delta["output_tokens"], delta["cached_input_tokens"], 0, "codex", f.get("sid", ""),
                                     model, os.path.basename((f.get("cwd") or "").rstrip("/")) or "?", 0.0))
            if ts >= self.last_ts:
                self.last_ts = ts
                self.last_model = f"{model} · {f['effort']}" if f.get("effort") else model

    def snapshot(self, chosen=False):
        """O bloco "cx" do frame, ou None sem uso do Codex nos últimos 7 dias: aí a placa fica como sempre foi.
        chosen: o Codex foi escolhido em Provedores; aí o bloco vai mesmo parado, com o aviso no lugar do reset."""
        if not self.recent and not chosen:
            return None
        now = time.time()
        midnight = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
        spark = minute_spark(self.events, now)
        rl = self.limits or {}
        return {"m": self.last_model or "Codex", "plan": (rl.get("plan_type") or "").capitalize(),
                "tot": fmt_tokens(sum(e.total for e in self.events if e.ts >= midnight)),
                "rate": fmt_tokens(sum(spark[-5:]) / 5), "sp": spark,
                "n": len({e.session for e in self.events if now - e.ts < 600})} | codex_windows(rl, datetime.now()) | (
            {} if self.recent else {"r5": f"Sem uso há {int((now - self.last_use) // 86400)} dias" if self.last_use else "Sem uso"})


PROVIDERS = os.path.join(HOME, ".claude/esp-tokens-providers")  # o ajuste Provedores, escolhido na placa
PROVIDER_MODES = ("auto", "claude", "codex", "both")


def provider_mode():
    try:
        with open(PROVIDERS) as f:
            mode = f.read().strip()
    except OSError:
        mode = ""
    return mode if mode in PROVIDER_MODES else "auto"


def with_codex(frame, cx, claude_events, codex, claude_recent):
    """O frame conforme o ajuste Provedores, quando o Codex foi usado nos últimos 7 dias. "Só Claude" é o frame de
    sempre. "Os dois" acrescenta o bloco cx e soma o Codex em sessões, modelos e histórico. "Só Codex" põe os dados
    do Codex nos campos de sempre: as telas de hoje mostram o Codex sem mudar. O automático é "os dois", ou
    "só Codex" num Mac sem uso do Claude Code."""
    chosen = provider_mode()
    frame |= {"prov": chosen, "cxok": 1}  # cxok: a placa mostra o ajuste só quando o Mac tem o Codex
    if not os.path.exists(PROVIDERS):  # ninguém escolheu ainda: a placa pergunta, com o texto do que o Mac tem
        frame["pask"] = "both" if claude_recent else "codex"
    mode = chosen if chosen != "auto" else "both" if claude_recent else "codex"
    if mode == "claude" or not cx:  # sem cx: automático sem Codex recente, a placa de sempre
        return frame
    now = time.time()
    midnight = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    claude_today = [e for e in claude_events if e.ts >= midnight]
    codex_today = [e for e in codex.events if e.ts >= midnight]
    if _openai["st"]:
        cx["st"] = _openai["st"]
    items = codex.session_items(now)
    if mode == "codex":
        sess = {"n": 0, "ws": [], "rows": []}
        add_codex_sessions(sess, items)
        keep = {k: frame[k] for k in ("clk", "t", "dark", "agv", "act", "prov", "cxok", "pask") if k in frame}
        return keep | {k: v for k, v in cx.items() if k not in ("n", "plan")} | {
            "cxo": 1, "cxn": cx["n"], "org": f"Codex · {cx['plan']}" if cx["plan"] else "Codex", "cli": 0, "desk": 0,
            "sess": sess, "mods": models(codex_today), "projs": projects([e for e in codex.events if now - e.ts < 3600], now),
            "hist": [r[:2] + [r[4], 0] for r in history_rows([], codex_today)], "accts": []}  # sem preço: custo 0
    frame["cx"] = cx
    add_codex_sessions(frame["sess"], items)
    frame["mods"] = models(claude_today + codex_today)
    frame["hist"] = history_rows(claude_today, codex_today)
    return frame


def add_codex_sessions(sess, items):
    """As sessões do Codex num workspace fixo "Codex", no fim (último campo 2; o do Claude Desktop é 1): os logs
    não dizem em que terminal ou painel elas rodam, e o app do Codex fica fora do herdr."""
    if not items:
        return
    items.sort(key=lambda r: ({"work": 0, "turn": 1}.get(r[0], 2), r[1]))
    count = lambda st: sum(1 for r in items if r[0] == st)
    i = len(sess["ws"])
    sess["ws"].append(["Codex", len(items), count("work"), 0, count("turn"), 0, 2])
    sess["rows"] = (sess["rows"] + [r[2] + [i, r[3], 0] for r in items[:MAX_PER_WS]])[:MAX_SESSIONS]
    sess["n"] += len(items)


def codex_windows(rl, now):
    """As janelas do Codex no formato das do Claude. Passado o reset sem uso novo, a janela está zerada de fato.
    Plano sem janela de 5 h (Pro) fica com p5 -1."""
    def win(w):
        w = w or {}
        if w.get("resets_at") and w["resets_at"] <= now.timestamp():
            return {"used_percentage": 0}
        return {"used_percentage": w.get("used_percent"), "resets_at": w.get("resets_at")}
    return window_fields(win(rl.get("primary")), win(rl.get("secondary")), now)


def models(events):
    """Tokens de hoje por modelo: [nome, total, % do dia, in, out, cache lido, cache escrito, custo,
    [[projeto, tokens], ...] dos 3 que mais usaram]."""
    by = {}
    for e in events:
        by.setdefault(e.model, []).append(e)
    day = sum(e.total for e in events) or 1
    rows = sorted(by.items(), key=lambda kv: -sum(e.total for e in kv[1]))[:3]
    out = []
    for name, es in rows:
        per_proj = {}
        for e in es:
            proj = e.project if e.project != "?" else first_prompt(e.session)[:40] or "?"
            per_proj[proj] = per_proj.get(proj, 0) + e.total
        top = sorted(per_proj.items(), key=lambda kv: -kv[1])[:3]
        codex = all(e.entry == "codex" for e in es)  # sem cache escrito e sem preço na tabela: os dois vão vazios
        out.append([name, fmt_tokens(t := sum(e.total for e in es)), round(100 * t / day),
                    *(fmt_tokens(sum(getattr(e, f) for e in es)) for f in ("inp", "out", "cread")),
                    "" if codex else fmt_tokens(sum(e.cwrite for e in es)),
                    "" if codex else fmt_usd(sum(e.cost for e in es)), [[p, fmt_tokens(v)] for p, v in top]])
    return out


def projects(events, now):
    """Sessões com uso na última hora: [projeto, CLI/Desk, tokens, minutos desde a última resposta]."""
    by = {}
    for e in events:
        by.setdefault(e.session, []).append(e)
    rows = sorted(by.values(), key=lambda es: -max(e.ts for e in es))[:8]
    return [[es[0].project if es[0].project != "?" else first_prompt(es[0].session)[:40] or "?",
             "Desk" if es[-1].entry == "claude-desktop" else "Codex" if es[-1].entry == "codex" else "CLI",
             fmt_tokens(sum(e.total for e in es)), int((now - max(e.ts for e in es)) // 60)]
            for es in rows]


_herdr = {"at": 0.0, "agents": {}, "focused_ws": None}
HERDR_STATE = {"working": "work", "blocked": "block", "done": "turn"}
_STATE_TITLE = re.compile(r"^[●✳]?\s*[^:]*:\s*(done|ready|working|blocked|idle)$")


HERDR = shutil.which("herdr") or "/opt/homebrew/bin/herdr"  # o LaunchAgent roda sem o PATH do Homebrew


HERDR_SOCKET = os.path.join(HOME, ".config/herdr/herdr.sock")
HERDR_EVENTS = ["pane.focused", "pane.created", "pane.closed", "pane.agent_detected", "tab.focused", "tab.renamed",
                "workspace.focused", "workspace.renamed"]
_RESUBSCRIBE = (b"pane.created", b"pane.closed", b"pane.agent_detected")  # mudou a lista de painéis
_wake = threading.Event()  # algo mudou: manda o frame agora, sem esperar o próximo segundo


def herdr_events():
    """Escuta os eventos do herdr pelo socket: quando uma sessão muda de estado ou de foco, o cache cai e a
    placa recebe o frame na hora (antes: até 3 s do cache + 1 s do loop). O estado do agente só pode ser
    assinado painel por painel, então a assinatura é refeita quando um painel abre ou fecha.
    Sem herdr, tenta de novo a cada 30 s."""
    import socket
    while True:
        try:
            _herdr["at"] = 0
            panes = {a["pane"] for a in herdr_agents().values() if a.get("pane")}
            subs = [{"type": t} for t in HERDR_EVENTS]
            subs += [{"type": "pane.agent_status_changed", "pane_id": p} for p in sorted(panes)]
            with socket.socket(socket.AF_UNIX) as sock:
                sock.connect(HERDR_SOCKET)
                sock.sendall((json.dumps({"id": "tokenpulse", "method": "events.subscribe",
                                          "params": {"subscriptions": subs}}) + "\n").encode())
                for line in sock.makefile("rb"):
                    if b'"error"' in line:
                        log(f"herdr: assinatura recusada: {line[:160]!r}")
                        break
                    _herdr["at"] = 0
                    _wake.set()
                    if any(k in line for k in _RESUBSCRIBE):
                        break
        except OSError:
            pass
        time.sleep(30 if not os.path.exists(HERDR_SOCKET) else 1)


def herdr_agents():
    """Painéis do herdr com Claude, por sessionId: estado (working/blocked/done/idle), nome da aba, título,
    pasta e workspace. Relido a cada 3 s (~70 ms); sem herdr instalado ou rodando, fica vazio e vale só o .jsonl."""
    if time.time() - _herdr["at"] < 3:
        return _herdr["agents"]
    agents, labels = {}, {}
    try:
        r = subprocess.run([HERDR, "workspace", "list"], capture_output=True, text=True, timeout=3)
        for w in json.loads(r.stdout)["result"]["workspaces"]:
            labels[w["workspace_id"]] = (w.get("number") or 99, w.get("label") or w["workspace_id"])
            if w.get("focused"):
                _herdr["focused_ws"] = labels[w["workspace_id"]]
        r = subprocess.run([HERDR, "tab", "list"], capture_output=True, text=True, timeout=3)
        tabs = {t["tab_id"]: t.get("label") or "" for t in json.loads(r.stdout)["result"]["tabs"]}
        r = subprocess.run([HERDR, "agent", "list"], capture_output=True, text=True, timeout=3)
        for a in json.loads(r.stdout)["result"]["agents"]:
            sid = (a.get("agent_session") or {}).get("value")
            if a.get("agent") == "claude" and sid:
                title = (a.get("terminal_title_stripped") or "").strip()
                ws = labels.get(a.get("workspace_id"), (99, a.get("workspace_id") or "?"))
                agents[sid] = {"status": a.get("agent_status"), "cwd": a.get("cwd") or "", "ws": ws,
                               "title": "" if _STATE_TITLE.match(title) else title, "pane": a.get("pane_id"),
                               "tab": tabs.get(a.get("tab_id"), ""),  # o nome que você deu à aba no herdr
                               "focused": bool(a.get("focused")) and _herdr.get("focused_ws") == ws}
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired):
        pass
    _herdr.update(at=time.time(), agents=agents)
    return agents


_title_cache = {}  # sessionId -> título lido do .jsonl (sessões fora da janela de 24 h)


def session_title(sid):
    """Último custom-title do .jsonl da sessão; para painéis do herdr abertos há mais de um dia."""
    if sid not in _title_cache:
        title = ""
        for path in glob.glob(os.path.join(HOME, f".claude/projects/*/{sid}.jsonl")):
            try:
                with open(path, "rb") as f:
                    for raw in f:
                        if b'"custom-title"' in raw:
                            title = json.loads(raw).get("customTitle") or title
            except (OSError, ValueError):
                pass
        _title_cache[sid] = title
    return _title_cache[sid]


_prompt_cache = {}
_log_path = {}  # sessão -> o .jsonl dela ("" se não achou); o caminho não muda, a data sim


def log_age(sid, now):
    """Segundos desde a última escrita no .jsonl da sessão. Serve de idade para painel do herdr sem
    atividade recente lida pelo daemon: sem ela, uma sessão esperando você ficaria sem "há N min"."""
    if sid not in _log_path:
        _log_path[sid] = next(iter(glob.glob(os.path.join(HOME, f".claude/projects/*/{sid}.jsonl"))), "")
    try:
        return now - os.path.getmtime(_log_path[sid]) if _log_path[sid] else None
    except OSError:
        return None


def first_prompt(sid):
    """Começo do primeiro prompt: nome de reserva para sessão sem título nem pasta, como as do Agent SDK,
    que rodam a partir de / (as coletas automáticas, por exemplo)."""
    if sid not in _prompt_cache:
        text = ""
        for path in glob.glob(os.path.join(HOME, f".claude/projects/*/{sid}.jsonl")):
            try:
                with open(path) as f:
                    for raw in f:
                        d = json.loads(raw)
                        if d.get("type") != "user":
                            continue
                        c = (d.get("message") or {}).get("content")
                        text = c if isinstance(c, str) else next(
                            (b.get("text", "") for b in c or [] if isinstance(b, dict) and b.get("type") == "text"), "")
                        if text.strip():
                            break
            except (OSError, ValueError):
                pass
        _prompt_cache[sid] = " ".join(text.split())
    return _prompt_cache[sid]


MAX_SESSIONS = 40   # o frame fica em ~5 KB, abaixo do buffer serial de 8 KB da placa
MAX_PER_WS = 12
OUTSIDE = (1000, "Fora do herdr")  # terminais sem herdr, depois dos workspaces
DESKTOP = (1001, "Claude Desktop")  # sempre o último: o Desktop não deixa escolher a conversa, só abrir o app
MULTICA_WS = (1002, "Multica")  # as sessões que o Multica abre, numa pasta por run em vez do nome do projeto


ASK_MAX = 120  # o resumo do que a sessão pede: cabe em 3 linhas do destaque da placa
_PLAIN = str.maketrans({"\u201c": '"', "\u201d": '"', "\u2018": "'", "\u2019": "'", "\u2014": "-", "\u2013": "-",
                        "\u2192": "->", "\u00a0": " "})


def plain(text):
    """Texto que as fontes da placa desenham: ASCII, acentos do português e "·…"; o resto (emoji, marcação) sai."""
    text = re.sub(r"(?m)^\s*(#+|>)\s*", "", text.translate(_PLAIN))  # títulos e citações do markdown
    text = re.sub(r"\*\*|__|`", "", text)
    text = "".join(c for c in text if c.isascii() or c in "áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ·…°")
    return re.sub(r"\s+([.,!?;:])", r"\1", " ".join(text.split()))


def ask_summary(content):
    """O que a última mensagem do Claude pede: a pergunta do AskUserQuestion, a ferramenta e o comando ou arquivo
    de um pedido de permissão, ou o texto. Vazio se a mensagem não tem nada disso."""
    if isinstance(content, str):
        content = [{"type": "text", "text": content}]
    for c in content if isinstance(content, list) else []:
        if not isinstance(c, dict):
            continue
        if c.get("type") == "tool_use":
            inp = c.get("input") if isinstance(c.get("input"), dict) else {}
            qs = inp.get("questions")
            if c.get("name") == "AskUserQuestion" and isinstance(qs, list) and qs and isinstance(qs[0], dict):
                s = qs[0].get("question", "")
            else:
                detail = inp.get("command") or inp.get("file_path") or inp.get("url") or inp.get("description") or ""
                s = f"{c.get('name', '')}: {detail}" if detail else c.get("name", "")
        elif c.get("type") == "text":
            s = c.get("text", "")
        else:
            continue
        if s := plain(str(s)):
            return s[:ASK_MAX - 1] + "…" if len(s) > ASK_MAX else s
    return ""


def sessions(state, titles, herdr, events, now):
    """Sessões agrupadas pelos workspaces do herdr.
    ws: [nome, sessões, trabalhando, precisa de você, sua vez, em foco, 1 Claude Desktop / 3 Multica], na ordem do herdr.
    rows: [nome, CLI/Desk, estado, tokens, minutos desde a última atividade (-1: sem dado), índice do workspace,
           id curto da sessão (para o toque trazê-la à frente), em foco no herdr, o que ela pede (só esperando você)].
    Entram todos os painéis de Claude do herdr e as outras sessões com atividade na última hora.
    Estado: block (esperando você aprovar), work, turn (o Claude terminou, sua vez) ou idle.
    O estado do herdr vale mais que a heurística do .jsonl; o nome é o título da sessão quando existe."""
    tokens = {}
    for e in events:
        tokens[e.session] = tokens.get(e.session, 0) + e.total
    seen = herdr and terminal_in_front()  # o foco do herdr só conta se a janela do terminal está na frente
    show_multica = mc_settings()["on"]  # Multica desligado: as runs dele (automação, não sessão sua) nem aparecem
    groups = {}
    for sid in set(state) | set(herdr):
        s, h = state.get(sid), herdr.get(sid)
        age = now - s["ts"] if s else log_age(sid, now) if h else None
        if not h and age > 3600:
            continue
        if h and h["status"] in HERDR_STATE:
            st = HERDR_STATE[h["status"]]
        elif s and s["kind"] == "assistant" and s["stop"] == "end_turn" and age < 3600:
            st = "turn"
        elif s and ((s["kind"] == "assistant" and age < 600) or (s["kind"] == "user" and age < 300)):
            st = "work"  # ferramenta rodando, ou o modelo pensando depois do prompt
        else:
            st = "idle"
        folder = s["project"] if s else os.path.basename((h["cwd"] if h else "").rstrip("/")) or "?"
        # o nome da aba no herdr é o que você escolheu; depois o título da sessão; por último a pasta
        name = ((h and h["tab"]) or titles.get(sid) or (h and h["title"]) or (h and session_title(sid))
                or (folder if folder != "?" else first_prompt(sid) or folder))
        entry = s["entry"] if s else ""
        origin = "Desk" if entry == "claude-desktop" else "SDK" if entry == "sdk-cli" else "CLI"
        if issue := multica_issue(s["cwd"] if s else h["cwd"] if h else ""):
            if not show_multica:
                continue
            origin, name = "Multica", f"{issue} {_mc['titles'].get(issue, '')}".strip()
        row = [cut(name, 48), origin, st, fmt_tokens(tokens[sid]) if sid in tokens else "--",
               int(age // 60) if age is not None else -1]
        ask = s.get("q", "") if s and st in ("block", "turn") and age is not None and age <= 3600 else ""
        _known_sessions[sid[:8]] = {"pane": h["pane"] if h else None, "desk": origin == "Desk"}
        # sem herdr na máquina, o agrupamento natural é o projeto (pasta) da sessão
        group = (MULTICA_WS if origin == "Multica" else h["ws"] if h else DESKTOP if origin == "Desk"
                 else OUTSIDE if herdr else (0, folder))
        groups.setdefault(group, []).append((st, -(s["ts"] if s else 0), row, sid[:8],
                                              bool(seen and h and h["focused"]), ask))
    order = {"block": 0, "work": 1, "turn": 2, "idle": 3}
    ws, rows = [], []
    for i, (key, items) in enumerate(sorted(groups.items())):
        items.sort(key=lambda r: (order[r[0]], r[1]))
        count = lambda st: sum(1 for r in items if r[0] == st)
        ws.append([cut(key[1], 32), len(items), count("work"), count("block"), count("turn"),
                   1 if seen and key == _herdr.get("focused_ws") else 0, 1 if key == DESKTOP else 3 if key == MULTICA_WS else 0])
        rows += [r[2] + [i, r[3], 1 if r[4] else 0] + ([r[5]] if r[5] else []) for r in items[:MAX_PER_WS]]
    return {"n": sum(w[1] for w in ws), "ws": ws, "rows": rows[:MAX_SESSIONS]}


_history = {}  # "AAAA-MM-DD" -> [tokens, custo], dias anteriores a hoje
_cx_history = {}  # "AAAA-MM-DD" -> tokens do Codex, dias anteriores a hoje


def codex_days(cutoff, today):
    """Tokens do Codex por dia, pela diferença do acumulado de cada sessão (o token_count às vezes se repete)."""
    days = {}
    for path in glob.glob(CODEX_SESSIONS, recursive=True):
        if os.path.getmtime(path) < cutoff:
            continue
        prev = 0
        with open(path, "rb") as f:
            for raw in f:
                if b'"token_count"' not in raw:
                    continue
                try:
                    d = json.loads(raw)
                    tot = d["payload"]["info"]["total_token_usage"]["total_tokens"]
                    day = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).astimezone().strftime("%Y-%m-%d")
                except (ValueError, TypeError, KeyError, AttributeError):
                    continue
                delta, prev = (tot - prev if tot >= prev else tot), tot
                if day < today and delta > 0:
                    days[day] = days.get(day, 0) + delta
    return days


def history_loop():
    """Tokens e custo por dia nos últimos HISTORY_DAYS dias. Relê os .jsonl (~1,4 GB em 14 dias, ~5 s)
    a cada 30 min numa thread: o dia de hoje vem dos eventos ao vivo, os anteriores não mudam."""
    while True:
        try:
            cutoff = time.time() - HISTORY_DAYS * 86400
            today = datetime.now().strftime("%Y-%m-%d")
            days, seen = {}, set()
            for path in glob.glob(PROJECTS, recursive=True):
                if os.path.getmtime(path) < cutoff:
                    continue
                with open(path, "rb") as f:
                    for raw in f:
                        if b'"usage"' not in raw or b'"assistant"' not in raw:
                            continue
                        try:
                            d = json.loads(raw)
                            msg, u = d["message"], d["message"]["usage"]
                            key = (msg.get("id"), d.get("requestId"))
                            if key in seen:
                                continue
                            seen.add(key)
                            day = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00")).astimezone().strftime("%Y-%m-%d")
                        except (ValueError, TypeError, KeyError, AttributeError):
                            continue
                        if day >= today:
                            continue
                        tot = sum(u.get(k) or 0 for k in ("input_tokens", "output_tokens", "cache_read_input_tokens",
                                                          "cache_creation_input_tokens"))
                        acc = days.setdefault(day, [0, 0.0])
                        acc[0] += tot
                        acc[1] += cost_usd(msg.get("model"), u)
            global _history, _cx_history
            _history = days  # troca a referência: nunca há um frame com o histórico vazio no meio
            _cx_history = codex_days(cutoff, today)
        except Exception as e:  # o histórico é extra: sem ele a tela só fica sem as barras antigas
            log(f"histórico falhou: {e!r}")
        now = datetime.now()
        to_midnight = (now.replace(hour=0, minute=0, second=0) + timedelta(days=1) - now).total_seconds()
        time.sleep(min(1800, to_midnight + 60))  # depois da meia-noite, ontem entra no histórico na hora


def history_rows(today_events, codex_today=None):
    """[dia da semana, dia do mês, tokens em milhões, custo em US$] dos últimos HISTORY_DAYS dias, hoje por último.
    Com o Codex (codex_today: os eventos dele hoje), os tokens somam os dois e um 5º campo traz os do Codex."""
    now = datetime.now()
    rows = []
    for back in range(HISTORY_DAYS - 1, -1, -1):
        d = datetime.fromtimestamp(now.timestamp() - back * 86400)
        if back == 0:
            tot, cost = sum(e.total for e in today_events), sum(e.cost for e in today_events)
        else:
            tot, cost = _history.get(d.strftime("%Y-%m-%d"), [0, 0.0])
        row = [WEEKDAYS[d.weekday()], d.day, round(tot / 1e6, 1), round(cost)]
        if codex_today is not None:
            cx = sum(e.total for e in codex_today) if back == 0 else _cx_history.get(d.strftime("%Y-%m-%d"), 0)
            row[2] = round((tot + cx) / 1e6, 1)
            row.append(round(cx / 1e6, 1))
        rows.append(row)
    return rows


# Contas do Claude Code no cswap (claude-swap). A placa pede a troca; o daemon só executa estas ações.
CSWAP = shutil.which("cswap") or os.path.join(HOME, ".local/bin/cswap")
CLAUDE = shutil.which("claude") or os.path.join(HOME, ".local/bin/claude")
_ACCOUNT_LINE = re.compile(r"^\s*(\d+):\s*\S+.*?\[(?P<org>[^\]]+)\](?P<active>\s*\(active\))?")
_accounts = {"at": 0.0, "list": []}
_action = {"state": "", "target": "", "msg": "", "at": 0.0}  # o que a placa pediu e em que pé está


def accounts():
    """[[número, org, ativa, precisa de login]] do cswap list, relido a cada 60 s numa thread (o cswap leva
    alguns segundos e não pode segurar o frame das placas). Sem e-mail: a placa não precisa."""
    if time.time() - _accounts["at"] >= 60 and not _accounts.get("busy"):
        _accounts["busy"] = True
        threading.Thread(target=_refresh_accounts, daemon=True).start()
    return _accounts["list"]


def _refresh_accounts():
    rows = []
    try:
        out = subprocess.run([CSWAP, "list"], capture_output=True, text=True, timeout=10).stdout.splitlines()
        for i, line in enumerate(out):
            if m := _ACCOUNT_LINE.match(line):
                relogin = i + 1 < len(out) and "re-login needed" in out[i + 1]
                rows.append([int(line.split(":")[0]), cut(scrub(m["org"]), 32), 1 if m["active"] else 0,
                             1 if relogin else 0])
    except (OSError, subprocess.TimeoutExpired):
        pass
    _accounts.update(at=time.time(), list=rows, busy=False)


def accounts_frame():
    """Contas com o último % conhecido: [número, org, ativa, precisa de login, % 5h, % semana, minutos desde]."""
    out, current = [], _org["name"]
    rows = accounts()
    known = any(name == current for _, name, _, _ in rows)
    for num, name, active, relogin in rows:
        if known:  # "Em uso" pela org logada agora, sem esperar o cswap list ser relido
            active = 1 if name == current else 0
        seen = _per_org.get(name) or {}
        p5, pw = seen.get("p5"), seen.get("pw")
        out.append([num, name, active, relogin, round(p5) if p5 is not None else -1, round(pw) if pw is not None else -1,
                    int((time.time() - seen["at"]) // 60) if seen else -1])
    return out


_known_sessions = {}  # id curto -> painel do herdr ou Desktop: o "focus" só aceita sessões listadas aqui


def terminal_app():
    """App de terminal onde o cliente do herdr roda (Warp, iTerm, Ghostty…), achado subindo a árvore de
    processos até um .app; None se não achar."""
    try:
        pid = subprocess.run(["pgrep", "-x", "herdr"], capture_output=True, text=True).stdout.split()
        for p in pid:
            for _ in range(8):
                out = subprocess.run(["ps", "-o", "ppid=,comm=", "-p", p], capture_output=True, text=True).stdout.strip()
                if not out:
                    break
                ppid, comm = out.split(None, 1)
                if ".app/" in comm:
                    return comm[:comm.index(".app/") + 4]
                p = ppid
    except (OSError, ValueError):
        pass
    return None


_front = {"at": 0.0, "path": "", "term_at": 0.0, "term": None}


def frontmost_app():
    """Caminho do .app em primeiro plano no Mac (lsappinfo, ~15 ms, sem permissão de acessibilidade)."""
    if time.time() - _front["at"] > 1:
        try:
            asn = subprocess.run(["lsappinfo", "front"], capture_output=True, text=True, timeout=2).stdout.strip()
            out = subprocess.run(["lsappinfo", "info", "-only", "bundlepath", asn], capture_output=True, text=True,
                                 timeout=2).stdout
            _front["path"] = out.split("=", 1)[1].strip().strip('"') if "=" in out else ""
        except (OSError, subprocess.TimeoutExpired):
            _front["path"] = ""
        _front["at"] = time.time()
    return _front["path"]


def terminal_in_front():
    """O app de terminal do herdr está em primeiro plano: só então a sessão em foco no herdr é a que você vê."""
    if time.time() - _front["term_at"] > 60:
        _front["term"], _front["term_at"] = terminal_app(), time.time()
    return bool(_front["term"]) and frontmost_app() == _front["term"]


def focus_session(sid8):
    """Traz a sessão para a frente no Mac: o painel no herdr e o app de terminal, ou o Claude Desktop."""
    target = _known_sessions.get(sid8)
    if not target:
        return
    if target["pane"]:
        subprocess.run([HERDR, "agent", "focus", target["pane"]], capture_output=True, timeout=5)
        app = MAC and terminal_app()
        if app:
            subprocess.run(["open", "-a", app], capture_output=True, timeout=5)
    elif target["desk"] and MAC:
        subprocess.run(["open", "-a", "Claude"], capture_output=True, timeout=5)


def run_action(cmd):
    """Ação pedida pela placa: "switch N" troca para a conta N do cswap; "login" entra numa org nova.
    Conta com login válido troca na hora; sem ele, abre o login do Claude Code no navegador e, quando
    ele terminar, registra a conta no cswap."""
    parts = cmd.split()
    if len(parts) == 2 and parts[0] == "focus" and re.fullmatch(r"[0-9a-f-]{8}", parts[1]):
        return focus_session(parts[1])  # trazer à frente não troca nada: sem confirmação nem estado
    if len(parts) == 2 and parts[0] == "prov" and parts[1] in PROVIDER_MODES:  # o ajuste Provedores, da placa
        with open(PROVIDERS, "w") as f:
            f.write(parts[1])
        log(f"provedores: {parts[1]}")
        return _wake.set()
    if len(parts) == 2 and parts[0] == "mcopen" and re.fullmatch(r"[A-Z]+\d*-\d+", parts[1]) and _mc["url"]:
        return subprocess.Popen([OPEN, multica_link(_mc["url"] + parts[1])])
    if parts == ["lskip"]:  # a pessoa pulou o login do Claude: só tokens, sem %; não pergunta mais
        with open(ONBOARD, "w") as f:
            json.dump({"login": True}, f)
        return _wake.set()
    if parts == ["update"]:
        return run_update()
    if parts == ["fwlater"]:  # a pessoa adiou a gravação na placa: uma hora sem perguntar
        for state in _fw.values():
            state.update(later=time.time() + 3600)
            state.pop("start", None)
        return
    if len(parts) == 2 and parts[0] == "mcproj" and parts[1].isdigit():
        _mc["open"] = (int(parts[1]), time.time())
        return _wake.set()
    if len(parts) == 2 and parts[0] == "mc" and parts[1] in ("0", "1"):
        return mc_save(on=parts[1] == "1")
    if len(parts) == 2 and parts[0] == "mcws" and parts[1].isdigit():
        wss = [w for w in mc_list(mc_cli("workspace", "list"), "workspaces")]
        return mc_save(i=int(parts[1]), ws=wss[int(parts[1])]["id"]) if int(parts[1]) < len(wss) else None
    if _action["state"] in ("switching", "login") or not parts or parts[0] not in ("switch", "login"):
        return
    target = next((a for a in accounts() if parts[0] == "switch" and len(parts) > 1 and str(a[0]) == parts[1]), None)
    if parts[0] == "switch" and not target:
        return
    org_name = target[1] if target else ""
    _action.update(state="login", target=org_name, msg="", at=time.time())  # já ocupado: 2ª ACTION é ignorada

    def work():
        try:
            if target and not target[3]:
                _action.update(state="switching", target=org_name, msg="", at=time.time())
                r = subprocess.run([CSWAP, "switch", str(target[0])], capture_output=True, text=True, timeout=60)
                ok = r.returncode == 0
            else:
                _action.update(state="login", target=org_name, msg="", at=time.time())
                r = subprocess.run([CLAUDE, "auth", "login", "--claudeai"], stdin=subprocess.DEVNULL,
                                   capture_output=True, text=True, timeout=600)
                ok = r.returncode == 0 and subprocess.run([CSWAP, "add"], capture_output=True, text=True,
                                                          timeout=60).returncode == 0
            _action.update(state="done" if ok else "error",
                           msg="" if ok else scrub((r.stderr or r.stdout).strip())[-60:], at=time.time())
        except (OSError, subprocess.TimeoutExpired) as e:
            _action.update(state="error", msg=type(e).__name__, at=time.time())
        _accounts["at"] = 0  # relê a lista na próxima volta
        log(f"conta: {_action['state']} {org_name} {_action['msg']}")

    threading.Thread(target=work, daemon=True).start()


def account_frame():
    act = _action if time.time() - _action["at"] < 600 else {"state": "", "target": "", "msg": ""}
    state = act["state"]
    # a org do Claude Code mudou depois do pedido: a troca já valeu, mesmo com o cswap add ainda rodando
    if state in ("login", "switching") and _org["changed_at"] > _action["at"]:
        state = "done"
    return {"accts": accounts_frame(), "act": [state, act["target"], act["msg"]]}


_theme = {"at": 0.0, "dark": True}


def dark_mode():
    """Modo escuro do sistema, relido a cada 10 s. No macOS a chave não existe no modo claro; no Linux vale o
    color-scheme do GNOME, e sem gsettings fica o último valor (escuro de início)."""
    if time.time() - _theme["at"] > 10:
        _theme["at"] = time.time()
        try:
            if MAC:
                r = subprocess.run(["defaults", "read", "-g", "AppleInterfaceStyle"], capture_output=True, text=True)
                _theme["dark"] = r.stdout.strip() == "Dark"
            else:
                r = subprocess.run(["gsettings", "get", "org.gnome.desktop.interface", "color-scheme"],
                                   capture_output=True, text=True, timeout=2)
                if r.returncode == 0:
                    _theme["dark"] = "dark" in r.stdout
        except (OSError, subprocess.TimeoutExpired):
            pass
    return _theme["dark"]


_org = {"mtime": 0.0, "name": "", "changed_at": 0.0}
_refetch = threading.Event()


def read_org():
    return scrub((json.load(open(ACCOUNT)).get("oauthAccount") or {}).get("organizationName", ""))


def org():
    """Org logada no Claude Code (oauthAccount do ~/.claude.json), relida só quando o arquivo muda."""
    try:
        mtime = os.path.getmtime(ACCOUNT)
        if mtime != _org["mtime"]:
            name = read_org()
            if _org["name"] and name != _org["name"]:
                _org["changed_at"] = time.time()
                _refetch.set()  # org nova: o % da anterior não vale mais, busca o desta já
                _accounts["at"] = 0  # e a lista do cswap relê, para a org nova aparecer
            _org.update(mtime=mtime, name=name)
    except (OSError, ValueError):
        pass
    return _org["name"]


# % de 5h e da semana direto da Anthropic, com o token OAuth que o Claude Code guarda no Keychain (no Linux, no
# ~/.claude/.credentials.json).
# É o mesmo endpoint que o Claude Code usa. Não é documentado: se falhar, vale a statusline.
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
_api = {"five": None, "week": None, "at": 0.0, "err": "", "org": ""}
_per_org = {}  # org -> último % visto quando ela estava logada: {"p5", "pw", "at"}


def fetch_usage():
    # o token é relido a cada busca e nunca renovado aqui: renovar invalidaria o do Claude Code
    if MAC:
        raw = subprocess.run(["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
                             capture_output=True, text=True, timeout=30, check=True).stdout
    else:
        raw = open(os.path.join(os.environ.get("CLAUDE_CONFIG_DIR", os.path.join(HOME, ".claude")), ".credentials.json")).read()
    token = json.loads(raw)["claudeAiOauth"]["accessToken"]
    req = urllib.request.Request(USAGE_URL, headers={
        "Authorization": f"Bearer {token}", "anthropic-beta": "oauth-2025-04-20", "User-Agent": "esp-ia-tokens/1.0"})
    with urllib.request.urlopen(req, timeout=10) as r:
        d = json.load(r)
    return d["five_hour"], d["seven_day"]


USAGE_EVERY = 120  # o % muda devagar; 60 s já levou a 429
USAGE_MAX_WAIT = 1800


USAGE_CACHE = os.path.join(HOME, ".claude/esp-tokens-usage.json")


def usage_loop():
    """Busca o % na API. O último resultado e a hora da próxima chamada ficam em disco: reiniciar o
    daemon não chama a API de novo antes da hora (cada início chamando na hora levava a 429)."""
    wait, next_at = USAGE_EVERY, 0.0
    try:
        cached = json.load(open(USAGE_CACHE))
        _api.update(five=cached["five"], week=cached["week"], at=cached["at"], org=cached.get("org", ""))
        _per_org.update(cached.get("orgs", {}))
        wait, next_at = cached.get("wait", USAGE_EVERY), cached.get("next_at", 0.0)
    except (OSError, ValueError, KeyError):
        pass
    while True:
        # a troca de org acorda antes da hora, mas não fura a espera de um 429
        while time.time() < next_at:
            if _refetch.wait(next_at - time.time()) and wait <= USAGE_EVERY:
                break
        _refetch.clear()
        try:
            name = read_org()
            five, week = fetch_usage()
            if read_org() != name:  # trocou de org no meio da busca: o % pode ser de qualquer uma
                raise ValueError("org trocou durante a busca")
            if not isinstance(five, dict) or not isinstance(week, dict):
                raise ValueError("resposta sem five_hour/seven_day")
            _api.update(five=five, week=week, at=time.time(), err="", org=name)
            _per_org[name] = {"p5": five.get("utilization"), "pw": week.get("utilization"), "at": time.time()}
            wait = USAGE_EVERY
        except Exception as e:  # rede, Keychain, 401 ou formato novo: registra uma vez e segue na statusline
            if getattr(e, "code", None) == 429:  # respeita o Retry-After; sem ele, dobra a espera
                retry = (e.headers.get("Retry-After") or "").strip()
                wait = min(USAGE_MAX_WAIT, max(int(retry) if retry.isdigit() else 0, wait * 2))
            err = f"{type(e).__name__}: {getattr(e, 'code', '') or e}"[:120]
            if err != _api["err"]:
                log(f"uso via API falhou: {err}; próxima tentativa em {wait} s")
            _api["err"] = err
        # trocou de org no meio da busca: tenta a nova em 5 s, sem esperar a volta inteira
        next_at = time.time() + (5 if _api["err"].startswith("ValueError: org trocou") else wait)
        try:
            with open(USAGE_CACHE + ".tmp", "w") as f:
                json.dump({"five": _api["five"], "week": _api["week"], "at": _api["at"], "org": _api["org"],
                           "wait": wait, "next_at": next_at, "orgs": _per_org}, f)
            os.replace(USAGE_CACHE + ".tmp", USAGE_CACHE)  # atômico: um bootout no meio não trunca o cache
        except OSError:
            pass


def exhaustion(start, p5, reset, now):
    """Quando a janela de 5 h esgota no ritmo médio desde o início dela, se for antes do reset."""
    # ponytail: ritmo médio da janela inteira; uma rajada recente só pesa aos poucos
    if not start or not reset or p5 < 10 or now - start < timedelta(minutes=20):
        return None
    end = start + (now - start) * 100 / p5
    return end if end < reset - timedelta(minutes=5) else None


STATUS_URL = "https://status.claude.com/api/v2/summary.json"
STATUS_COMPONENTS = ("Claude Code", "Claude API (api.anthropic.com)")
_claude = {"st": 0}  # o pior estado desses componentes na página de status: 0 ok, 1 instável, 2 fora do ar
OPENAI_STATUS_URL = "https://status.openai.com/api/v2/summary.json"
_openai = {"st": 0}  # o mesmo para os componentes do Codex ("Codex in ChatGPT Desktop")


def worst_status(url, wanted):
    rank = {"degraded_performance": 1, "under_maintenance": 1, "partial_outage": 2, "major_outage": 2}
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            comps = json.load(r)["components"]
        return max((rank.get(c["status"], 0) for c in comps if wanted(c["name"])), default=0)
    except Exception:  # sem resposta da página não quer dizer falha do serviço: o aviso some
        return 0


def status_loop():
    """De 5 em 5 min, as páginas de status da Anthropic e da OpenAI."""
    while True:
        _claude["st"] = worst_status(STATUS_URL, lambda name: name in STATUS_COMPONENTS)
        _openai["st"] = worst_status(OPENAI_STATUS_URL, lambda name: "Codex" in name)
        time.sleep(300)


MULTICA = shutil.which("multica") or "/opt/homebrew/bin/multica"  # o LaunchAgent roda sem o PATH do Homebrew
MULTICA_CONFIG = os.path.join(HOME, ".multica/config.json")
MULTICA_DESKTOP_CFG = os.path.join(HOME, ".multica/desktop.json")  # o servidor em que o app desktop está ligado
MULTICA_APP = "/Applications/Multica.app"
MULTICA_CFG = os.path.join(HOME, ".claude/esp-tokens-multica.json")  # o ajuste Multica, escolhido na placa
# Cada chamada da CLI vai ao servidor do Multica. Uma volta lê issues, agentes e projetos (3 chamadas) e as runs só
# dos agentes trabalhando agora ou na volta anterior (a anterior pega a run que acabou ou falhou). De minuto em
# minuto enquanto há agente trabalhando; parado, de 5 em 5 min. Runtimes e squads, de hora em hora; PRs, a cada 10 min.
MULTICA_EVERY = 60
MULTICA_IDLE_EVERY = 300
MC_PROJECT = {"planned": "Planejado", "in_progress": "Em andamento", "completed": "Concluído", "paused": "Pausado",
              "cancelled": "Cancelado"}
MC_PRIORITY = {"low": "baixa", "medium": "média", "high": "alta", "urgent": "urgente"}
MC_ALERT_SECS = 60
MC_LIST = 6  # cartões de cada número no frame (rodando, na fila, em revisão, falhou): 24 no máximo, ~3 KB
MC_PROJECTS = 12  # projetos no frame
MC_PROJECT_ISSUES = 15  # issues do projeto aberto na placa; só ele vai no frame
# a ordem das issues de um projeto, que também é o código do status no frame (a placa escolhe nome e cor por ele)
MC_ISSUE_ORDER = {"in_review": 0, "blocked": 1, "in_progress": 2, "todo": 3, "backlog": 4, "done": 5}
_MC_DIR = re.compile(r"/multica_workspaces[^/]*/[^/]+/([a-z]+\d*-\d+)-[0-9a-f]{12}(?:/|$)")
_mc = {"mc": None, "mcs": None, "alert": None, "titles": {}, "url": "", "proj": [], "open": (-1, 0.0)}
_mc_wake = threading.Event()


def multica_ok():
    return os.path.exists(MULTICA) and os.path.exists(MULTICA_CONFIG)


def mc_settings():
    try:
        with open(MULTICA_CFG) as f:
            s = json.load(f)
    except (OSError, ValueError):
        s = {}
    return {"on": s.get("on", False), "ws": s.get("ws", "")}  # desligado até a pessoa ligar nos ajustes


def mc_save(i=None, **kw):
    """Grava o ajuste e já muda o que as placas recebem; a leitura nova do Multica vem logo depois."""
    s = mc_settings() | kw
    with open(MULTICA_CFG, "w") as f:
        json.dump(s, f)
    if _mc["mcs"]:
        _mc["mcs"] = _mc["mcs"] | {"on": int(s["on"])} | ({"i": i} if i is not None else {})
    if not s["on"]:
        _mc["mc"] = None
    _mc_wake.set()
    _wake.set()


def mc_cli(*args, ws=""):
    """A CLI do Multica já autenticada; o daemon não lê nem guarda o token."""
    try:
        r = subprocess.run([MULTICA, *args, "--output", "json"] + (["--workspace-id", ws] if ws else []),
                           capture_output=True, text=True, timeout=30)
        return json.loads(r.stdout) if r.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired, ValueError):
        return None


def mc_list(d, key):
    """A CLI devolve a lista pura ou dentro de um objeto, conforme o comando."""
    d = d.get(key) if isinstance(d, dict) else d
    return [x for x in d if isinstance(x, dict)] if isinstance(d, list) else []


def mc_ts(s):
    """Hora ISO do Multica (fração com até 9 dígitos e Z) em epoch; 0 sem hora."""
    m = re.match(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(\.\d+)?(Z|[+-]\d\d:\d\d)?$", s or "")
    if not m:
        return 0.0
    frac = (m.group(2) or "")[:7]
    return datetime.fromisoformat(m.group(1) + frac + (m.group(3) or "Z").replace("Z", "+00:00")).timestamp()


def mc_issues(ws):
    issues = []
    while len(issues) < 500:
        d = mc_cli("issue", "list", "--limit", "100", "--offset", str(len(issues)), "--fields",
                   "id,identifier,title,status,status_category,priority,assignee_type,assignee_id,creator_id,"
                   "project_id,updated_at", ws=ws)
        if d is None:
            return None
        page = mc_list(d, "issues")
        issues += page
        if not page or not (isinstance(d, dict) and d.get("has_more")):
            return issues
    return issues


def multica_link(url):
    """O link da issue: no app desktop do Multica quando ele está instalado e ligado ao mesmo servidor, senão na web."""
    try:
        with open(MULTICA_DESKTOP_CFG) as f:
            app = json.load(f).get("appUrl", "").rstrip("/")
    except (OSError, ValueError):
        app = ""
    if app and url.startswith(app + "/") and os.path.isdir(MULTICA_APP):
        return "multica://open/issue?url=" + urllib.parse.quote(url, safe="")
    return url


def mc_prs(state, wid, issue_id, now):
    """Quantos PRs a issue tem, relido a cada 10 min."""
    cache = state.setdefault("prs", {})
    if issue_id not in cache or now - cache[issue_id][0] > 600:
        cache[issue_id] = (now, len(mc_list(mc_cli("issue", "pull-requests", issue_id, ws=wid), "pull_requests")))
    return cache[issue_id][1]


def multica_issue(cwd):
    """A issue de uma sessão que o Multica abriu: a pasta da run é <workspace>/<issue>-<hash>/workdir."""
    m = _MC_DIR.search(cwd or "")
    return m.group(1).upper() if m else ""


def mc_alert(kind, issue, who, detail):
    ident = issue.get("identifier", "")
    _mc["alert"] = {"id": int(time.time()), "k": kind, "i": ident, "t": cut(plain(issue.get("title", "")), 60),
                    "m": " · ".join(x for x in (ident, who) if x), "x": detail, "at": time.time()}
    log(f"multica: aviso {kind} {ident}")


def multica_once(state):
    """Uma volta: issues, projetos, agentes e as runs de cada agente, no workspace escolhido."""
    s = mc_settings()
    if not state.get("wss") or time.time() - state.get("wss_at", 0) > 3600:
        state["wss"] = mc_list(mc_cli("workspace", "list"), "workspaces") or state.get("wss") or []
        state["me"] = (mc_cli("user", "profile", "get") or {}).get("id", "")
        state["wss_at"] = time.time()
        try:
            with open(MULTICA_CONFIG) as f:
                cfg = json.load(f)
            state["app"], state["default"] = cfg.get("app_url", ""), cfg.get("workspace_id", "")
        except (OSError, ValueError):
            pass
    wss = state["wss"]
    ids = [w.get("id") for w in wss]
    wid = s["ws"] if s["ws"] in ids else state.get("default") if state.get("default") in ids else ids[0] if ids else ""
    _mc["mcs"] = {"on": int(s["on"]), "w": [cut(w.get("name", ""), 32) for w in wss[:5]],
                  "i": ids.index(wid) if wid in ids else 0}
    if not s["on"] or not wid:
        _mc["mc"] = None
        return
    if wid != state.get("scope"):  # outro workspace: começa sem avisos do que já existia
        state.update(scope=wid, prev=None, failed={}, working=set(), prs={})
    issues = mc_issues(wid)
    if issues is None:
        return  # servidor fora: fica a última leitura
    agents = mc_list(mc_cli("agent", "list", ws=wid), "agents")
    projects = mc_list(mc_cli("project", "list", ws=wid), "projects")
    me, now = state.get("me"), time.time()
    my_agents = {a.get("id") for a in agents if a.get("owner_id") == me}
    # "suas" são as issues criadas por você ou atribuídas a você, a um agente seu ou a um squad de que você (ou um
    # agente seu) participa. Os squads mudam pouco: a lista de membros é relida de hora em hora
    if state.get("squads_of") != wid or now - state.get("squads_at", 0) > 3600:
        mine_ids = {me} | my_agents
        state["provider"] = {r.get("id"): r.get("provider", "") for r in mc_list(mc_cli("runtime", "list", ws=wid), "runtimes")}
        squads = mc_list(mc_cli("squad", "list", ws=wid), "squads")
        state["squad_names"] = {sq.get("id"): sq.get("name", "") for sq in squads}
        state["squads"] = {sq.get("id") for sq in squads
                           if any(m.get("member_id") in mine_ids
                                  for m in mc_list(mc_cli("squad", "member", "list", sq.get("id", ""), ws=wid), "members"))}
        state.update(squads_of=wid, squads_at=now)
    ours, provider = {me} | my_agents | state["squads"], state["provider"]
    mine = lambda i: i.get("creator_id") in ours or i.get("assignee_id") in ours
    # a placa mostra só o seu trabalho: suas issues, as runs dos seus agentes ou nas suas issues, e os projetos
    # que você lidera ou em que tem issue. As de todos ficam para achar a issue de uma run e nomear as sessões
    by_id = {i.get("id"): i for i in issues}
    _mc["titles"] = {i.get("identifier", ""): i.get("title", "") for i in issues}
    issues = [i for i in issues if mine(i)]
    names = {a.get("id"): a.get("name", "") for a in agents}
    pname = {p.get("id"): p.get("title", "") for p in projects}
    slug = next((w.get("slug", "") for w in wss if w.get("id") == wid), "")
    _mc["url"] = f"{state.get('app', '').rstrip('/')}/{slug}/issues/" if state.get("app") and slug else ""

    who_of = lambda i: "Você" if i.get("assignee_id") == me else names.get(i.get("assignee_id")) or \
        state.get("squad_names", {}).get(i.get("assignee_id"), "")
    mins = lambda ts: int((now - ts) // 60) if ts else -1

    def row(kind, agent, prov, issue, ago, tok="--", attempt=1, prs=0, model="", err=""):
        return [cut(agent, 24), prov, issue.get("identifier", ""), cut(plain(issue.get("title", "")), 60), kind, ago, tok,
                cut(pname.get(issue.get("project_id"), ""), 32), MC_PRIORITY.get(issue.get("priority"), ""), attempt, prs,
                model, err]

    # as falhas das últimas 24 h ficam guardadas: o agente que falhou para de trabalhar e sai das próximas leituras
    runs, last = [], state.get("at", now)
    failed = {k: f for k, f in state.get("failed", {}).items() if f[0] >= now - 86400}
    working = {a.get("id") for a in agents if a.get("status") == "working"}
    for a in agents:
        if a.get("id") not in working | state.get("working", set()):
            continue
        for t in mc_list(mc_cli("agent", "tasks", a.get("id", ""), "--limit", "10", ws=wid), "tasks"):
            st, issue = t.get("status"), by_id.get(t.get("issue_id")) or {}
            ours_run = issue and (mine(issue) or a.get("id") in my_agents)
            done_at = mc_ts(t.get("completed_at"))
            tries = f"tentativa {t.get('attempt')} de {t.get('max_attempts')}" if t.get("max_attempts") else ""
            err = cut(plain(str(t.get("error") or "")), 40)
            if st == "failed" and ours_run and state.get("prev") is not None and done_at >= last - 5:
                mc_alert("fail", issue, " · ".join(x for x in (a.get("name", ""), tries) if x), err)
            if st == "failed" and ours_run and done_at >= now - 86400 and done_at > failed.get(issue.get("id"), (0,))[0]:
                failed[issue.get("id")] = (done_at, row(3, a.get("name", ""), (provider.get(a.get("runtime_id")) or "").capitalize(),
                                                         issue, mins(done_at), attempt=t.get("attempt") or 1, err=err))
            if st in ("completed", "failed", "cancelled") or not ours_run:
                continue
            usage = t.get("usage") if isinstance(t.get("usage"), list) else []
            tok = sum((u.get("input_tokens") or 0) + (u.get("output_tokens") or 0) + (u.get("cache_read_tokens") or 0)
                      + (u.get("cache_write_tokens") or 0) for u in usage if isinstance(u, dict))
            model = next((u.get("model") for u in usage if isinstance(u, dict) and u.get("model")), "")
            start = mc_ts(t.get("started_at"))
            runs.append((0 if start else 1, -(start or mc_ts(t.get("created_at"))), a, t, issue, tok, model))
    runs.sort(key=lambda r: r[:2])
    active = {r[4].get("id") for r in runs}
    state.update(failed=failed, working=working)
    failed = [f[1] for k, f in sorted(failed.items(), key=lambda kv: -kv[1][0]) if k not in active]  # já tentou de novo: sai
    review = sorted((i for i in issues if i.get("status") == "in_review" and mine(i)), key=lambda i: -mc_ts(i.get("updated_at")))
    rows = []
    for queued, neg_ts, a, t, issue, tok, model in runs[:MC_LIST]:
        prs = 0 if queued else mc_prs(state, wid, issue.get("id", ""), now)
        rows.append(row(1 if queued else 0, a.get("name", ""), (provider.get(a.get("runtime_id")) or "").capitalize(), issue,
                        int((now + neg_ts) // 60), fmt_tokens(tok) if tok else "--", t.get("attempt") or 1, prs,
                        model_label(model, None) if model else ""))
    rows += [row(2, who_of(i), "", i, mins(mc_ts(i.get("updated_at")))) for i in review[:MC_LIST]]
    rows += failed[:MC_LIST]
    running = sum(1 for r in runs if not r[0])

    prev = state.get("prev")
    for i in issues:  # issue sua que chegou em revisão desde a última volta
        if (prev is not None and i.get("status") == "in_review" and prev.get(i.get("id"), "in_review") != "in_review"
                and mine(i)):
            mc_alert("rev", i, who_of(i), "")
    state["prev"] = {i.get("id"): i.get("status") for i in issues}
    state["at"] = now

    # as issues de cada projeto, para quando a placa abrir um: as que pedem atenção primeiro, sem as canceladas
    in_review = {}
    for i in issues:
        if i.get("status") == "in_review":
            in_review[i.get("project_id")] = in_review.get(i.get("project_id"), 0) + 1
    order = {"in_progress": 0, "planned": 1, "paused": 2, "completed": 3}
    my_projects = {i.get("project_id") for i in issues}
    pjs = sorted((p for p in projects if p.get("issue_count") and p.get("status") != "cancelled"
                  and (p.get("lead_id") in ours or p.get("id") in my_projects)),
                 key=lambda p: (order.get(p.get("status"), 4), -(p.get("issue_count") or 0)))[:MC_PROJECTS]
    by_project = {}
    for i in sorted(issues, key=lambda i: (MC_ISSUE_ORDER.get(i.get("status"), 9), -mc_ts(i.get("updated_at")))):
        if i.get("status") in MC_ISSUE_ORDER and len(by_project.setdefault(i.get("project_id"), [])) < MC_PROJECT_ISSUES:
            by_project[i.get("project_id")].append(
                [i.get("identifier", ""), cut(plain(i.get("title", "")), 48), MC_ISSUE_ORDER[i.get("status")], cut(who_of(i), 24),
                 mins(mc_ts(i.get("updated_at")))])
    _mc["proj"] = [by_project.get(p.get("id"), []) for p in pjs]
    _mc["mc"] = {"w": cut(next((w.get("name", "") for w in wss if w.get("id") == wid), ""), 32), "r": running,
                 "c": [running, len(runs) - running, len(review), len(failed)], "a": rows,
                 "p": [[cut(plain(p.get("title", "")), 32), MC_PROJECT.get(p.get("status"), p.get("status", "")),
                        p.get("done_count") or 0, p.get("issue_count") or 0, in_review.get(p.get("id"), 0)] for p in pjs]}


def multica_loop():
    """De minuto em minuto, o Multica pela CLI; ação da placa acorda antes."""
    state = {}
    while True:
        if multica_ok():
            try:
                multica_once(state)
            except Exception as e:  # resposta fora do formato não derruba o daemon
                log(f"multica: {e!r}")
        else:
            _mc.update(mc=None, mcs=None)
        _mc_wake.wait(MULTICA_EVERY if state.get("working") else MULTICA_IDLE_EVERY)
        _mc_wake.clear()


def multica_frame():
    if not _mc["mcs"]:
        return {}
    f = {"mcs": _mc["mcs"]}
    if not os.path.exists(MULTICA_CFG):  # primeiro uso com o Multica no Mac: a placa pergunta se mostra
        f["mask"] = 1
    if _mc["mc"]:
        f["mc"] = _mc["mc"]
        k, at = _mc["open"]  # o projeto aberto na placa nos últimos 10 min: as issues dele vão junto
        if 0 <= k < len(_mc["proj"]) and time.time() - at < 600:
            f["mc"] = f["mc"] | {"pi": [k, _mc["proj"][k]]}
    a = _mc["alert"]
    if a and time.time() - a["at"] < MC_ALERT_SECS:
        f["mca"] = {k: v for k, v in a.items() if k != "at"}
    return f


def limits():
    try:
        status_at = os.path.getmtime(STATUS)
    except OSError:
        status_at = 0.0
    # usa a fonte mais recente: a API (pausada em caso de 429) ou a statusline das sessões de CLI
    # o % da API é de uma org: depois de uma troca, só vale quando chegar o da org nova
    api_ok = _api["at"] and _api["org"] in ("", _org["name"])
    try:  # a statusline só concorre se trouxer a janela de 5h (logo depois do reset ela vem sem)
        status_has_5h = bool((json.load(open(STATUS)).get("rate_limits") or {}).get("five_hour"))
    except (OSError, ValueError, AttributeError):
        status_has_5h = False
    if api_ok and (_api["at"] >= status_at or not status_has_5h):
        age = time.time() - _api["at"]
        five = {"used_percentage": _api["five"].get("utilization"), "resets_at": _api["five"].get("resets_at")}
        week = {"used_percentage": _api["week"].get("utilization"), "resets_at": _api["week"].get("resets_at")}
    else:
        try:
            age = time.time() - os.path.getmtime(STATUS)
            rl = json.load(open(STATUS)).get("rate_limits") or {}
        except (OSError, ValueError):
            return {"p5": -1, "pw": -1}
        # a statusline não diz de qual org é o %: se a org mudou depois dela, o % é da org anterior
        if time.time() - age < _org["changed_at"]:
            return {"p5": -1, "pw": -1, "r5": "trocou de org", "age": int(age)}
        five, week = rl.get("five_hour") or {}, rl.get("seven_day") or {}
    return {"age": int(age)} | window_fields(five, week, datetime.now())


def window_fields(five, week, now):
    """Campos do anel de 5 h e da barra da semana a partir de {used_percentage, resets_at} de cada janela."""
    out = {}
    # depois do reset o % gravado é da janela anterior: vira "--" até chegar um dado novo
    out["p5"] = round(five.get("used_percentage") if five.get("used_percentage") is not None else -1)
    r5 = parse_reset(five.get("resets_at"))
    out["r5"] = f"Reset às {clock(r5)}" if r5 else ""  # horário, como o uso no app do Claude
    if r5 and r5 <= now:  # a janela virou e o % gravado é da anterior: até chegar o novo, é "janela nova"
        start = r5
        out["p5"], out["r5"] = -1, "aguardando o 1º dado"
    else:
        start = r5 - timedelta(hours=5) if r5 else None
    if start and out["p5"] <= 0:  # 0% ou "--" parece erro: o rótulo diz que a janela acabou de começar
        out["s5"] = f"janela nova · {clock(start)}"
    end = exhaustion(start, out["p5"], r5, now)
    if end and int(time.time()) // 3 % 2:  # o anel só tem uma linha: alterna com o reset a cada 3 s
        out["r5"] = f"Acaba ~{clock(end)}"
    out["pw"] = round(week.get("used_percentage") if week.get("used_percentage") is not None else -1)
    rw = parse_reset(week.get("resets_at"))
    out["rw"] = f"{WEEKDAYS[rw.weekday()]} {rw.hour}h" if rw else ""
    if rw:  # quanto da janela de 7 dias já passou: o ritmo linear que a barra da semana compara
        out["wf"] = max(0, min(100, round(100 - (rw - now).total_seconds() / 6048)))
    if rw and rw <= now:
        out["pw"], out["rw"] = -1, ""
    return out


class Board:
    """Uma placa conectada: a porta serial e o nome que ela informou no handshake."""

    def __init__(self, path, ser, name, fw="", ble="", reset=""):
        self.path, self.ser, self.name, self.fw, self.ble = path, ser, name, fw, ble
        self.reset = reset  # por que a placa reiniciou da última vez (firmware novo manda no hello)
        self.buf = b""  # linha da placa ainda sem o \n
        self.busy = False  # OTA ou screenshot rodando numa thread: o laço principal não mexe na porta
        self.gone = False  # a thread acabou com a porta (OTA reiniciou a placa, ou deu erro): reconectar
        self.caps = []  # o que o firmware entende além dos frames ("wifi"), vindo do hello
        self.out = None  # linha para mandar antes do próximo frame (o WiFi do computador): nunca vai para o log

    def log(self, msg):
        log(f"[{NICKS.get(self.name, self.name)}] {msg}")


BAUD = 460800  # placas com ponte USB-serial (CH340, CP210x) usam esta; a USB nativa do ESP ignora


def known_ble():
    try:
        return set(json.load(open(BLE_KNOWN)))
    except (OSError, ValueError):
        return set()


def remember_ble(name):
    known = known_ble()
    # o final é o id da placa: um nome antigo da mesma placa (antes "tokenpulse-XXXXXX") sai da lista
    now = {k for k in known if k[-6:] != name[-6:]} | {name}
    if now != known:
        json.dump(sorted(now), open(BLE_KNOWN, "w"))
    if name not in known:
        log(f"bluetooth: {name} pareada (veio pela USB)")


_usb_ble = set()  # nomes Bluetooth das placas que estão na USB agora: com cabo, o BLE fica de lado
BLE_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"  # o "UART" da Nordic, o mesmo do firmware
BLE_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # o daemon escreve aqui
BLE_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # a placa avisa por aqui (ACTION ...)


_ble_frame = {"line": b""}  # no processo do Bluetooth: o último frame que o daemon mandou


async def ble_main():
    """Uma placa conhecida fora da USB recebe os frames por Bluetooth, um por segundo, e manda as ações."""
    from bleak import BleakClient, BleakScanner
    while True:
        usb = set() if os.environ.get("TOKENPULSE_BLE_TEST") else _usb_ble  # teste: BLE mesmo com o cabo
        targets = known_ble() - usb
        if not targets:
            await asyncio.sleep(5)
            continue
        try:
            found = {}  # o nome anunciado agora: d.name pode ser o nome antigo que o macOS guardou

            def match(d, ad):
                found["name"] = ad.local_name or d.name
                return found["name"] in targets
            dev = await BleakScanner.find_device_by_filter(match, timeout=8, service_uuids=[BLE_SERVICE])
        except Exception as e:  # Bluetooth desligado
            print(f"log bluetooth: {e}", flush=True)
            await asyncio.sleep(60)
            continue
        if not dev:
            await asyncio.sleep(10)
            continue
        name, buf, client_mtu = found.get("name", dev.name), b"", [0]

        def on_tx(_, data):
            nonlocal buf
            *lines, buf = (buf + bytes(data)).split(b"\n")
            for raw in lines:
                line = raw.decode(errors="replace").strip()
                if line.startswith("ACTION "):
                    print(f"action {line[7:].strip()}", flush=True)
                    print(f"log [{name}] {line}", flush=True)
            if buf:
                print(f"log [{name}] bluetooth: pedaço sem fim de linha ({len(buf)} B, mtu {client_mtu[0]})", flush=True)
        try:
            async with BleakClient(dev) as client:
                print(f"log [{name}] conectada por bluetooth", flush=True)
                # uma escrita com resposta antes de tudo: a placa exige criptografia, e o macOS pareia (ou usa o
                # pareamento guardado) ao receber a recusa; as escritas sem resposta seguintes já vão cifradas
                await client.write_gatt_char(BLE_RX, b"\n", response=True)
                await client.start_notify(BLE_TX, on_tx)
                chunk = max(20, client.mtu_size - 3)
                client_mtu[0] = client.mtu_size
                while client.is_connected and name not in usb:
                    line = _ble_frame["line"]
                    for i in range(0, len(line), chunk):
                        await client.write_gatt_char(BLE_RX, line[i:i + chunk], response=False)
                    await asyncio.sleep(3)  # pelo ar a cada 3 s: a placa no Bluetooth quase sempre está na bateria
                    usb = set() if os.environ.get("TOKENPULSE_BLE_TEST") else _usb_ble
                print(f"log [{name}] " + ("voltou para a USB" if name in usb else "bluetooth desconectado"), flush=True)
        except Exception as e:
            print(f"log [{name}] bluetooth: {e}", flush=True)
            await asyncio.sleep(5)


def ble_worker():
    """Processo à parte para o Bluetooth: no macOS, um processo sem permissão é morto pelo sistema no primeiro
    acesso, e assim isso derruba só este processo, nunca o daemon. Lê do daemon "frame <json>" e "usb <nomes>"."""
    def read_stdin():
        global _usb_ble
        for raw in sys.stdin:
            kind, _, rest = raw.rstrip("\n").partition(" ")
            if kind == "frame":
                _ble_frame["line"] = (rest + "\n").encode()
            elif kind == "usb":
                _usb_ble = set(rest.split("\t")) if rest else set()  # tab: o nome tem espaços
        os._exit(0)  # o daemon fechou o pipe: acabou
    threading.Thread(target=read_stdin, daemon=True).start()
    asyncio.run(ble_main())


def ble_loop():
    """No daemon: mantém o processo do Bluetooth e passa os frames para ele. Se o processo morrer logo (macOS sem
    permissão de Bluetooth, bleak ausente), registra e para de tentar."""
    deaths = 0
    time.sleep(15)  # antes, o laço principal acha as placas na USB: sem isso, a placa na USB conecta e cai do BLE
    while deaths < 3:
        if not known_ble():
            time.sleep(10)
            continue
        started = time.time()
        proc = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--ble-worker"], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True, bufsize=1)

        def pump_out():
            for line in proc.stdout:
                kind, _, rest = line.rstrip("\n").partition(" ")
                if kind == "log":
                    log(rest)
                elif kind == "action":  # a regra do demo fica no board_action, como na USB
                    threading.Thread(target=board_action, args=("ACTION " + rest,), daemon=True).start()
        threading.Thread(target=pump_out, daemon=True).start()
        try:
            while proc.poll() is None:
                if frame := _latest.get("frame"):
                    proc.stdin.write("usb " + "\t".join(sorted(_usb_ble)) + "\n")
                    proc.stdin.write("frame " + encode_frame(frame).decode().rstrip("\n") + "\n")
                    proc.stdin.flush()
                time.sleep(1)
        except (BrokenPipeError, OSError):
            pass
        proc.wait()
        if time.time() - started < 60:
            deaths += 1
        log(f"bluetooth: o processo saiu (código {proc.returncode})")
        time.sleep(10)
    log("bluetooth: indisponível neste Mac (sem permissão ou sem o bleak); só USB e WiFi")


def serial_group(path):
    """O grupo dono da porta serial no Linux: dialout no Debian/Ubuntu/Fedora, uucp no Arch."""
    import grp
    try:
        return grp.getgrgid(os.stat(path).st_gid).gr_name
    except (OSError, KeyError):
        return "dialout"


def connect(path, vid):
    """Abre a porta e pergunta quem é. Devolve None se nada responder como placa do monitor."""
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = path, BAUD, 0
    ser.write_timeout = 2  # se a placa parar de ler, o write falha e o daemon reconecta em vez de travar
    # USB nativa: o macOS liga DTR e RTS ao abrir e o pyserial baixa o DTR antes do RTS; DTR 0 com RTS 1 reinicia o
    # S3. Com DTR já em 1 isso não acontece, e sem DTR o USB CDC do Arduino descarta o que o ESP imprime.
    # Na ponte USB-serial, DTR mexe no reset: os dois ficam em 0.
    ser.dtr, ser.rts = vid == ESPRESSIF_VID, False
    ser.open()
    time.sleep(0.5)
    # a placa pode estar reiniciando: pela ponte USB-serial, abrir a porta reinicia o ESP32 clássico, que só
    # responde depois do boot e da abertura (~4 s). Como a ponte só é testada uma vez, a janela é maior.
    deadline, buf = time.time() + (6 if vid == ESPRESSIF_VID else 12), b""
    while time.time() < deadline:
        ser.write(b"id\n")
        time.sleep(0.5)
        buf += ser.read(4096)
        for line in buf.decode(errors="replace").splitlines():
            if line.startswith("hello "):
                try:
                    hello = json.loads(line[6:])
                    board = Board(path, ser, hello["board"], hello.get("fw", ""), hello.get("ble", ""),
                                  hello.get("reset", ""))
                    board.caps = hello.get("caps", [])
                    if board.ble:
                        remember_ble(board.ble)
                    return board
                except (ValueError, KeyError, TypeError):
                    continue  # hello partido: tenta de novo até o prazo
    ser.close()
    return None


def wait_for(port, word, timeout):
    """Lê linhas do ESP até achar `word`. Devolve a linha de erro se o ESP responder ERRO."""
    deadline, buf = time.time() + timeout, b""
    while time.time() < deadline:
        buf += port.read(max(1, port.in_waiting))  # read(256) esperaria o timeout inteiro a cada "OK"
        *lines, buf = buf.split(b"\n")
        for line in lines:
            text = line.decode(errors="replace").strip()
            if text == word:
                return None
            if text.startswith("ERRO"):
                return text
    return f"sem resposta ({word})"


def settle(port):
    # pela ponte CH340, começar a captura ou o OTA com o último frame ainda saindo corta bytes no meio
    # dele, e a placa mostra o modelo com texto trocado
    port.flush()
    time.sleep(0.3)
    # o que a placa respondeu aos comandos de antes (rtc, play...) vai para o log em vez de sumir
    for line in port.read(port.in_waiting).decode(errors="replace").splitlines():
        board_action(line.strip())  # um toque logo antes da captura não se perde
        if line.strip() and not line.startswith("{"):
            log(f"placa: {line.strip()}")


def wifi_networks():
    """O WiFi do computador e as redes de 2,4 GHz que ele enxerga: (atual, banda da atual, {redes 2,4 GHz}).
    No macOS 15.6+ os nomes podem vir ocultos sem permissão de localização: aí volta (None, None, set())."""
    try:
        if MAC:
            out = subprocess.run(["system_profiler", "SPAirPortDataType"], capture_output=True, text=True, timeout=30).stdout
            # nome da rede numa linha "Nome:", o canal algumas linhas abaixo: "Channel: 44 (5GHz, 80MHz)"
            cur, band, low, section, name = None, None, set(), "", None
            for ln in out.splitlines():
                t = ln.strip()
                if t in ("Current Network Information:", "Other Local Wi-Fi Networks:"):
                    section, name = t, None
                elif section and t.endswith(":") and not t.startswith(("PHY", "Channel", "Network", "Security", "Signal")):
                    name = t[:-1]
                elif section and name and t.startswith("Channel:"):
                    g = "2" if "(2GHz" in t else "5"
                    if section.startswith("Current"):
                        cur, band = name, g
                    elif g == "2":
                        low.add(name)
                    name = None
                elif section and t and not ln.startswith(" " * 10):
                    section = ""  # saiu da lista de redes (outra interface ou outra seção)
            if cur and "redacted" in cur.lower():
                cur = None
            return cur, band, low
        out = subprocess.run(["nmcli", "-t", "-f", "active,ssid,chan", "dev", "wifi", "list"], capture_output=True, text=True, timeout=15).stdout
        cur, band, low = None, None, set()
        for ln in out.splitlines():
            parts = re.split(r"(?<!\\):", ln)
            if len(parts) < 3 or not parts[2].isdigit():
                continue
            name, g = parts[1].replace("\\:", ":"), "2" if int(parts[2]) <= 14 else "5"
            if g == "2":
                low.add(name)
            if parts[0] == "yes":
                cur, band = name, g
        return cur, band, low
    except (OSError, subprocess.TimeoutExpired):
        return None, None, set()


def wifi_for_board():
    """O nome de rede que a placa (só 2,4 GHz) consegue usar, a partir do WiFi do computador. Devolve (nome, certeza):
    com certeza False, o nome é um palpite para a pessoa confirmar na janela."""
    cur, band, low = wifi_networks()
    if cur and (band == "2" or cur in low):  # já em 2,4 GHz, ou o roteador usa o mesmo nome nas duas bandas
        return cur, True
    base = re.sub(r"[\s_-]*5\s*g(hz)?$", "", cur or "", flags=re.I)
    if base and base in low:  # o roteador separa: "Casa_5G" no computador, "Casa" em 2,4 GHz
        return base, True
    return base or None, False


def ask(prompt, hidden=False, default=""):
    """Pergunta numa janela do computador. None se a pessoa cancelar ou se não houver como perguntar (sem GUI)."""
    try:
        if MAC:
            q = lambda t: '"' + t.replace("\\", "\\\\").replace('"', '\\"') + '"'  # string do AppleScript: sem \\uXXXX
            dialog = f'display dialog {q(prompt)} default answer {q(default)} with title "tokenpulse"'
            cmd = ["osascript", "-e", f"text returned of ({dialog}{' with hidden answer' if hidden else ''})"]
        elif shutil.which("zenity"):
            cmd = ["zenity", "--entry", "--title=tokenpulse", f"--text={prompt}"] + (["--hide-text"] if hidden else [f"--entry-text={default}"])
        elif shutil.which("kdialog"):
            cmd = ["kdialog", "--title", "tokenpulse"] + (["--password", prompt] if hidden else ["--inputbox", prompt, default])
        else:
            return None
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if r.returncode and "-128" not in r.stderr:  # -128: a pessoa cancelou; o resto é erro de verdade
            log(f"janela: {cmd[0]} falhou: {r.stderr.strip()[:200]}")
        return r.stdout.rstrip("\n") if r.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


_wifi_lock = threading.Lock()


def wifi_setup(board):
    """O WiFi deste computador para uma placa USB que pediu: o nome vem do sistema, a senha a pessoa digita numa janela
    (não lemos o Keychain nem o NetworkManager). A senha vai só pelo cabo, na próxima linha para a placa."""
    if not _wifi_lock.acquire(blocking=False):
        return  # uma janela por vez: dois toques não abrem duas
    try:
        nick = NICKS.get(board.name, board.name)
        ssid, sure = wifi_for_board()
        if not sure:  # a placa só enxerga 2,4 GHz: sem achar essa rede, a pessoa confirma ou digita o nome
            ssid = ask(f"Nome do WiFi de 2,4 GHz para a placa {nick} (ela não enxerga redes de 5 GHz):", default=ssid or "")
        if not ssid:
            return board.log("wifi: sem nome de rede (não detectado ou cancelado)")
        pw = ask(f"Senha do WiFi \u201c{ssid}\u201d para a placa {nick}.\nDeixe vazio se a rede não tem senha.", hidden=True)
        if pw is None:
            return board.log("wifi: cancelado")
        board.out = json.dumps({"wifi": {"ssid": ssid, "pass": pw}}, ensure_ascii=False)
        board.log(f"wifi: mandando {ssid}")  # a senha fica fora do log
    finally:
        _wifi_lock.release()


def board_action(line):
    """Ação pedida pela placa. Na demonstração nada troca a sua conta; o ajuste Provedores pode, para as capturas."""
    parts = line.split()
    if DEMO and len(parts) == 3 and parts[1] == "mcproj" and parts[2].isdigit():  # o Multica do demo
        _demo_mc["proj"] = int(parts[2])
    elif DEMO and len(parts) == 3 and parts[1] in ("mc", "mcws") and parts[2].isdigit():
        _demo_mc[{"mc": "on", "mcws": "i"}[parts[1]]] = min(int(parts[2]), 1)
    elif line.startswith("ACTION ") and (not DEMO or line.startswith("ACTION prov ")):
        run_action(line[7:].strip())


def ota(port, path):
    """Manda o firmware pela serial; a placa mostra o progresso e reinicia no fim."""
    data = open(path, "rb").read()
    port.timeout = 0.05
    settle(port)
    port.write((json.dumps({"ota": len(data), "md5": hashlib.md5(data).hexdigest()}) + "\n").encode())
    if err := wait_for(port, "OK", 10):
        return err
    for i in range(0, len(data), 1024):
        port.write(data[i:i + 1024])
        if err := wait_for(port, "OK", 10):  # apagar um setor da flash às vezes passa de 5 s
            return f"{err} no byte {i}"
    return wait_for(port, "DONE", 15)


# Versões: o daemon instalado pelo install.sh procura release novo no GitHub e, quando a placa roda um
# firmware mais velho que o dele, grava o firmware do mesmo release sozinho (5 s de aviso na tela; o toque adia).
# Rodando do clone (versão "v0.20.3-1-g…-dirty") nada disso acontece: firmware de desenvolvimento fica como está.
RELEASE_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
REPO = "gilsondev/tokenpulse"
FW_CACHE = os.path.join(HOME, ".cache/tokenpulse")
RELEASE_EVERY = 6 * 3600
FW_WARN = 5  # s de aviso antes de gravar
_rel = {"latest": ""}
_fw = {}  # placa -> {"start": quando grava, "later": adiado até}


def vtuple(v):
    m = RELEASE_RE.match(v or "")
    return tuple(int(x) for x in m.groups()) if m else None


def installed():
    """Instalado pelo install.sh: há um VERSION ao lado do código (o clone de desenvolvimento não tem)."""
    return os.path.exists(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "VERSION"))


def release_loop():
    """De 6 em 6 h, o último release do GitHub (o redirect de /releases/latest diz a tag, sem API)."""
    while True:
        try:
            req = urllib.request.Request(f"https://github.com/{REPO}/releases/latest", method="HEAD")
            with urllib.request.urlopen(req, timeout=20) as r:
                tag = r.geturl().rstrip("/").rsplit("/", 1)[-1]
            if vtuple(tag):
                _rel["latest"] = tag
        except Exception as e:  # sem rede: tenta na próxima volta
            log(f"release: {type(e).__name__}")
        time.sleep(RELEASE_EVERY)


ONBOARD = os.path.join(HOME, ".claude/esp-tokens-onboard.json")  # perguntas do primeiro uso que a pessoa pulou
_login = {"ok": True}


def login_loop():
    """De 5 em 5 min: o Claude Code está logado? Sem login, a placa não tem o % de uso e pede o login."""
    while True:
        if os.path.exists(CLAUDE):
            try:
                _login["ok"] = subprocess.run([CLAUDE, "auth", "status"], capture_output=True, timeout=30).returncode == 0
            except (OSError, subprocess.TimeoutExpired):
                pass
        time.sleep(300)


def login_frame():
    try:
        skipped = json.load(open(ONBOARD)).get("login")
    except (OSError, ValueError):
        skipped = False
    return {} if _login["ok"] or skipped else {"lask": 1}


def update_frame():
    """"upd": a versão nova, quando o release do GitHub passa a deste daemon. A placa pergunta se atualiza."""
    latest = os.environ.get("TOKENPULSE_LATEST") or _rel["latest"]
    mine = vtuple(RELEASE)
    if _rel.get("updating"):  # o install.sh já está rodando: todas as placas mostram o progresso, não só a tocada
        return {"updg": _rel["updating"]}
    return {"upd": latest} if mine and vtuple(latest) and vtuple(latest) > mine else {}


def run_update():
    """Atualiza o daemon e, com ele, o firmware das placas: o mesmo install.sh do primeiro uso. Roda numa sessão
    própria, porque o install.sh reinicia o daemon que o chamou."""
    if not installed():
        return log("atualizar: daemon rodando do clone, use git pull")
    log("atualizar: rodando o install.sh")
    _rel["updating"] = os.environ.get("TOKENPULSE_LATEST") or _rel["latest"]
    _wake.set()  # o aviso sai para as placas antes de o install.sh reiniciar este daemon
    os.makedirs(LOG_DIR, exist_ok=True)
    with open(os.path.join(LOG_DIR, "tokenpulse-update.log"), "a") as out:
        subprocess.Popen(["/bin/bash", "-c", "curl -fsSL --retry 3 --retry-all-errors "
                          "https://gilsondev.github.io/tokenpulse/install.sh | bash"],
                         stdout=out, stderr=out, start_new_session=True)


def firmware_bin(board_name, tag):
    """O .bin de OTA do release, baixado uma vez para o cache; None enquanto não chegou."""
    path = os.path.join(FW_CACHE, tag, f"tokenpulse-{board_name}-ota.bin")
    if os.path.exists(path):
        return path
    state = _fw.setdefault(board_name, {})
    if state.get("downloading"):
        return None
    state["downloading"] = True

    def get():
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            url = f"https://github.com/{REPO}/releases/download/{tag}/tokenpulse-{board_name}-ota.bin"
            with urllib.request.urlopen(url, timeout=120) as r, open(path + ".tmp", "wb") as f:
                shutil.copyfileobj(r, f)
            os.replace(path + ".tmp", path)
        except Exception as e:
            log(f"[{board_name}] firmware {tag}: download falhou ({type(e).__name__})")
            state["later"] = time.time() + 600
        state["downloading"] = False
    threading.Thread(target=get, daemon=True).start()
    return None


def auto_ota(board, warn=True):
    """O firmware do release deste daemon, quando a placa roda um release mais velho e o aviso já passou."""
    mine, theirs = vtuple(RELEASE), vtuple(board.fw)
    state = _fw.setdefault(board.name, {})
    if not mine or not theirs or theirs >= mine or time.time() < state.get("later", 0):
        state.pop("start", None)
        return None
    path = firmware_bin(board.name, RELEASE)
    if not path:
        return None
    if warn and "start" not in state:
        state["start"] = time.time() + FW_WARN
        board.log(f"firmware {board.fw} → {RELEASE} em {FW_WARN} s")
    if warn and time.time() < state["start"]:
        return None
    state.pop("start", None)
    state["later"] = time.time() + 600  # se a gravação falhar, tenta de novo em 10 min, não em loop
    return path


def fw_frame(board):
    """O aviso na tela da placa antes de gravar: a versão nova e os segundos que faltam."""
    start = _fw.get(board.name, {}).get("start")
    return {"fwup": [RELEASE, max(0, round(start - time.time()))]} if start else {}


def pending_ota(board_name):
    request = os.path.join(OTA_DIR, board_name)
    try:
        path = open(request).read().strip()
        os.remove(request)
        return path
    except OSError:
        return None


def pending_shot(board_name):
    request = os.path.join(SHOT_DIR, board_name)
    try:
        req = json.load(open(request))
        os.remove(request)
        return req
    except (OSError, ValueError):
        return None


def png_from_rgb565(data, w, h):
    """PNG RGB de 8 bits a partir do quadro RGB565 com o byte alto primeiro, só com a stdlib."""
    import struct
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = data[y * w * 2:(y + 1) * w * 2]
        for i in range(0, len(row), 2):
            v = row[i] << 8 | row[i + 1]
            r, g, b = v >> 11 & 31, v >> 5 & 63, v & 31
            raw += bytes((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def screenshot(port, req, frame):
    """Manda os comandos (page N, tap X Y, swipe L/R/U/D), espera a tela assentar e salva o quadro mostrado.
    O frame segue indo durante a espera: sem ele por 5 s a placa se mostra offline na captura."""
    line = (json.dumps(frame, ensure_ascii=False) + "\n").encode()
    for cmd in req.get("cmds", []):
        port.write((cmd + "\n").encode())
        deadline = time.time() + 1.5
        while time.time() < deadline:
            port.write(line)
            time.sleep(0.5)
    # pela ponte CH340 o macOS às vezes perde bytes da placa para o Mac, e às vezes o pedido seguinte
    # some: tenta de novo, a tela é a mesma
    for _ in range(5):
        port.write(line)  # sem frame por uns segundos a placa se mostra offline na captura seguinte
        if not (err := capture(port, req)):
            return None
    return err


def capture(port, req):
    settle(port)
    port.write(b"shot\n")
    port.timeout = 0.05  # sem isso o read(max(1, in_waiting)) gira a 100% de CPU enquanto espera
    deadline, buf = time.time() + 8, b""
    while time.time() < deadline and b"SHOT " not in buf:
        buf += port.read(max(1, port.in_waiting))
    head, _, rest = buf.partition(b"SHOT ")
    while time.time() < deadline and b"\n" not in rest:
        rest += port.read(max(1, port.in_waiting))
    line, _, data = rest.partition(b"\n")
    try:
        w, h, n = map(int, line.split())
    except ValueError:
        port.timeout = 0
        return f"cabeçalho do quadro inválido: {line[:40]!r}"
    last = time.time()
    while time.time() < deadline and len(data) < n and time.time() - last < 0.8:  # parou de chegar: perdeu bytes
        got = port.read(max(1, port.in_waiting))
        data, last = data + got, time.time() if got else last
    port.timeout = 0
    if len(data) < n:
        return f"quadro incompleto: {len(data)} de {n} bytes"
    open(req["out"], "wb").write(png_from_rgb565(data[:n], w, h))
    return None


def demo_frame():
    """Dados fictícios para screenshots e demonstrações (`tokend.py --demo`): nada vem dos seus logs,
    da sua conta ou do herdr, e pedidos de troca de conta são ignorados."""
    import math
    now, t = datetime.now(), time.time()
    sp = [int(380_000 + 260_000 * math.sin(i / 5 + t / 60) + 140_000 * math.sin(i * 1.7)) for i in range(60)]
    daily = [118, 64, 12, 96, 171, 143, 58, 77, 41, 22, 9, 104, 132, 84]
    hist = []
    for back, tok in zip(range(13, -1, -1), daily):
        d = now - timedelta(days=back)
        hist.append([WEEKDAYS[d.weekday()], d.day, float(tok), round(tok * 0.42)])
    sessions = [
        ["Refatorar o checkout", "CLI", "work", "12,4M", 0, 0, "0000000a", 1],
        ["Revisar PR de pagamentos", "CLI", "block", "3,1M", 1, 0, "0000000b", 0, "Bash: gh pr merge 482 --squash"],
        ["Testes do carrinho", "CLI", "turn", "6,8M", 4, 0, "0000000c", 0, "Os 42 testes passaram. Abro o PR agora ou reviso a cobertura do checkout antes?"],
        ["Pipeline de deploy", "CLI", "turn", "2,2M", 9, 1, "0000000e", 0],
        ["Alertas do cluster", "CLI", "idle", "--", -1, 1, "0000000f", 0],
        ["Terraform da VPC", "CLI", "idle", "540k", 130, 1, "00000010", 0],
        ["Blog pessoal", "CLI", "idle", "--", -1, 2, "00000011", 0],
        ["Migração do banco", "Desk", "turn", "1,2M", 6, 3, "0000000d", 0, "Rodo a migração em staging primeiro?"],
    ]
    f = {
        "m": "Opus 5.5 · high", "tot": "84,2M", "rate": fmt_tokens(sum(sp[-5:]) / 5), "sp": sp, "cli": 3, "desk": 1,
        "sub": "out 402k · cache 83,5M",
        "mods": [["Opus 5.5", "71,6M", 85, "18k", "402k", "68,9M", "2,3M", "US$ 41",
                  [["checkout-api", "32,1M"], ["design-system", "21,4M"], ["docs", "9,8M"]]],
                 ["Sonnet 5.5", "10,9M", 13, "6k", "88k", "10,2M", "620k", "US$ 4",
                  [["infra", "6,2M"], ["blog", "3,1M"]]],
                 ["Haiku 5.5", "1,7M", 2, "2k", "31k", "1,6M", "90k", "US$ 0,06", [["scripts", "1,7M"]]]],
        "projs": [[r[0], r[1], r[3], max(r[4], 0)] for r in sessions[:5]],
        "sess": {"n": len(sessions), "ws": [["Produto", 3, 1, 1, 1, 1, 0], ["Infra", 3, 0, 0, 1, 0, 0], ["Pessoal", 1, 0, 0, 0, 0, 0],
                                            ["Claude Desktop", 1, 0, 0, 1, 0, 1]],
                 "rows": sessions},
        "hist": hist,
        "p5": DEMO_P5, "st": DEMO_ST, "r5": "Reset às 17h", "pw": 38, "rw": "seg 6h", "wf": 49, "age": 30,
        "clk": now.strftime("%H:%M"), "t": local_time(), "org": "Acme Labs", "dark": dark_mode(),
        "accts": [[1, "Acme Labs", 1, 0, DEMO_P5, 38, 0], [2, "Side Project", 0, 0, 7, 12, 180]], "act": ["", "", ""],
    }
    if DEMO_MULTICA:
        f |= demo_multica(f)
    if not DEMO_CODEX:
        return f
    # o ajuste Provedores vale também no demo; TOKENPULSE_DEMO_PROV escolhe um modo sem mexer no arquivo de verdade
    mode = os.environ.get("TOKENPULSE_DEMO_PROV") or provider_mode()
    f |= {"prov": mode, "cxok": 1}
    if not os.path.exists(PROVIDERS):  # a pergunta do primeiro uso; TOKENPULSE_DEMO_ASK=codex simula um Mac sem Claude
        f["pask"] = os.environ.get("TOKENPULSE_DEMO_ASK", "both")
    if mode == "claude":
        return f
    cx = {"m": "gpt-6.1-sol · medium", "plan": "Plus", "tot": "21,7M", "rate": fmt_tokens(sum(sp[-5:]) / 20),
          "sp": [v // 4 for v in sp], "n": 2, "p5": 9, "r5": "Reset às 18h", "pw": 9, "rw": "qua 9h", "wf": 30}
    cx_items = [("work", -t, ["Gerar testes da API", "Codex", "work", "2,0M", 0], "00000012"),
                ("turn", -t + 60, ["Migrar o cache", "Codex", "turn", "4,4M", 2], "00000013")]
    cx_model = ["gpt-6.1-sol", "21,7M", 20, "702k", "98k", "20,9M", "", "", [["checkout-api", "14,2M"], ["infra", "7,5M"]]]
    cx_daily = [30, 12, 0, 22, 41, 35, 9, 18, 6, 0, 0, 25, 33, 21.7]
    if mode == "codex":
        sess = {"n": 0, "ws": [], "rows": []}
        add_codex_sessions(sess, cx_items)
        return {k: f[k] for k in ("clk", "t", "dark", "act", "prov", "cxok", "pask") if k in f} | {k: v for k, v in cx.items() if k not in ("n", "plan")} | {
            "cxo": 1, "cxn": 2, "org": "Codex · Plus", "cli": 0, "desk": 0, "sess": sess, "mods": [cx_model[:2] + [100] + cx_model[3:]],
            "projs": [["checkout-api", "Codex", "2,0M", 0], ["infra", "Codex", "4,4M", 2]],
            "hist": [r[:2] + [cx_daily[i], 0] for i, r in enumerate(f["hist"])]}
    f["cx"] = cx
    add_codex_sessions(f["sess"], cx_items)
    f["mods"].insert(1, cx_model)
    f["mods"] = f["mods"][:3]  # o daemon manda os 3 que mais usaram
    f["mods"][0][2], f["mods"][2][2] = 68, 12
    for row, c in zip(f["hist"], cx_daily):
        row[2] = round(row[2] + c, 1)
        row.append(c)
    return f


_demo_mc = {"on": 1, "i": 0}


def demo_multica(f):
    """O Multica fictício das capturas. TOKENPULSE_DEMO_MC_ALERT=rev|fail mostra um aviso."""
    f["sess"]["ws"].append(["Multica", 2, 2, 0, 0, 0, 3])
    i = len(f["sess"]["ws"]) - 1
    f["sess"]["rows"] += [["TP-42 Refatorar o checkout", "Multica", "work", "3,4M", 0, i, "00000014", 0],
                          ["TP-47 Testes da API", "Multica", "work", "0,8M", 0, i, "00000015", 0]]
    f["sess"]["n"] += 2
    out = {"mcs": {"on": _demo_mc["on"], "w": ["Acme Growth", "Pessoal"], "i": _demo_mc["i"]}}
    if not _demo_mc["on"]:
        return out
    out["mc"] = {"w": ["Acme Growth", "Pessoal"][_demo_mc["i"]], "c": [2, 1, 2, 1], "r": 2, "a": [
        ["Opus Dev", "Claude", "TP-42", "Refatorar o checkout", 0, 12, "3,4M", "Checkout novo", "alta", 3, 1, "Opus 5.5", ""],
        ["Codex Rev", "Codex", "TP-47", "Testes da API", 0, 3, "0,8M", "Checkout novo", "média", 1, 0, "gpt-6.1-sol", ""],
        ["Sonnet Docs", "Claude", "TP-53", "Gerar o changelog", 1, 1, "--", "Design system", "", 1, 0, "", ""],
        ["Opus Dev", "", "TP-38", "Ajustar o webhook de pagamento", 2, 2, "--", "Checkout novo", "alta", 1, 0, "", ""],
        ["Squad Front", "", "TP-44", "Cupom no carrinho", 2, 95, "--", "Checkout novo", "", 1, 0, "", ""],
        ["Sonnet Docs", "Claude", "TP-51", "Atualizar o README", 3, 20, "--", "Design system", "", 2, 0, "", "exit 1 · npm test"]],
        "p": [["Checkout novo", "Em andamento", 12, 20, 1], ["Design system", "Em andamento", 5, 18, 0],
              ["Busca nova", "Em andamento", 3, 11, 1], ["Notificações", "Em andamento", 8, 10, 0],
              ["App mobile", "Planejado", 0, 9, 0], ["Pagamentos v2", "Planejado", 0, 6, 0],
              ["Infra 2027", "Concluído", 7, 7, 0]]}
    if _demo_mc.get("proj", -1) >= 0:
        out["mc"]["pi"] = [_demo_mc["proj"], [
            ["TP-38", "Ajustar o webhook de pagamento", 0, "Opus Dev", 2], ["TP-49", "Taxa de entrega por CEP", 1, "Você", 300],
            ["TP-42", "Refatorar o checkout", 2, "Opus Dev", 12], ["TP-47", "Testes da API", 2, "Codex Rev", 3],
            ["TP-57", "Parcelamento no cartão", 3, "Squad Front", 60], ["TP-55", "Dark mode no painel", 4, "", 1440],
            ["TP-58", "Frete grátis acima de R$ 200", 3, "", 2000], ["TP-59", "Resumo do pedido no e-mail", 4, "", 3000],
            ["TP-60", "Cupom por categoria", 4, "Squad Front", 4000], ["TP-61", "Limite de itens no carrinho", 4, "", 5000],
            ["TP-31", "Login com passkey", 5, "Você", 2880]]]
    kind = os.environ.get("TOKENPULSE_DEMO_MC_ALERT", "")
    if kind == "rev":
        out["mca"] = {"id": 1, "k": "rev", "i": "TP-38", "t": "Ajustar o webhook de pagamento", "m": "TP-38 · Opus Dev", "x": ""}
    elif kind == "fail":
        out["mca"] = {"id": 2, "k": "fail", "i": "TP-51", "t": "Atualizar o README",
                      "m": "TP-51 · Sonnet Docs · tentativa 2 de 3", "x": "exit 1 · npm test"}
    return out


FRAME_MAX = 7500  # o buffer serial da placa tem 8 KB


def local_time():
    """Segundos desde 1970 no fuso do Mac: o relógio das placas mostra a hora com segundos sem saber de fuso."""
    now = datetime.now().astimezone()
    return int(now.timestamp() + now.utcoffset().total_seconds())


def encode_frame(frame):
    """Frame em uma linha. Se passar do buffer da placa, corta as sessões do fim até caber."""
    line = (json.dumps(frame, ensure_ascii=False) + "\n").encode()
    rows = (frame.get("sess") or {}).get("rows") or []
    while len(line) > FRAME_MAX and rows:
        rows = rows[:-1]
        frame = frame | {"sess": frame["sess"] | {"rows": rows}}
        line = (json.dumps(frame, ensure_ascii=False) + "\n").encode()
    return line


_serial_boards = {}
_save_lock = threading.Lock()  # o laço serial e o do WiFi gravam o mesmo arquivo


def save_boards(boards=None):
    """As placas conectadas agora, com o firmware de cada uma: o update.sh decide por elas quem atualizar.
    O laço serial passa as dele; o do WiFi chama sem argumento e mantém as da serial."""
    global _serial_boards
    with _save_lock:
        if boards is not None:
            _serial_boards = dict(boards)
        data = {b.name: {"fw": b.fw, "port": b.path} for b in [*_serial_boards.values(), *net_boards.values()]}
        with open(BOARDS + ".tmp", "w") as f:
            json.dump(data, f)
        os.replace(BOARDS + ".tmp", BOARDS)  # quem lê nunca pega o arquivo pela metade


def in_background(board, job):
    """OTA e screenshot levam de segundos a quase um minuto: rodam numa thread, e o laço principal pula
    esta placa enquanto isso, para as outras seguirem recebendo frames."""
    def run():
        try:
            job()
        except (OSError, ValueError, serial.SerialException) as e:
            board.log(f"serial: {e}")
            board.gone = True
        finally:
            board.busy = False

    board.busy = True
    threading.Thread(target=run, daemon=True).start()


def waiting(board):
    try:
        return not board.busy and board.ser.in_waiting > 0
    except (OSError, serial.SerialException):
        return True  # a placa saiu: o serve() descobre e tira da lista


def serve(board, frame):
    """Um ciclo de uma placa: OTA se houver pedido, senão o frame. Devolve False se a placa saiu."""
    if board.gone:
        board.ser.close()
        return False
    if board.busy:
        return True
    try:
        if path := pending_ota(board.name) or auto_ota(board):
            def ota_job():
                t0 = time.time()
                err = ota(board.ser, path)
                board.log(f"ota: {err or 'ok'} ({path}, {time.time() - t0:.0f} s)")
                board.gone = True  # a placa reinicia; volta pelo handshake
            in_background(board, ota_job)
            return True
        if req := pending_shot(board.name):
            # captura com dados reais só se o pedido disser (shot.sh com REAL=1): uma captura para o
            # README feita sem --demo exporia org, títulos de sessão e custos
            if DEMO or req.get("real"):
                in_background(board, lambda: board.log(f"shot: {screenshot(board.ser, req, frame) or 'ok'} ({req.get('out')})"))
                return True
            board.log(f"shot: recusado: daemon sem --demo; rode com REAL=1 para capturar dados reais ({req.get('out')})")
        if board.out:
            board.ser.write(board.out.encode() + b"\n")
            board.out = None
        board.ser.write(encode_frame(frame | fw_frame(board)))
        board.buf += board.ser.read(4096)  # lê sempre para não encher o buffer
        *lines, board.buf = board.buf.split(b"\n")  # só linhas completas: "ACTION switch 12" não vira "1"
        for raw in lines:
            line = raw.decode(errors="replace").strip()
            if line == "ACTION wifi" and "wifi" in board.caps:  # a placa sem rede pediu o WiFi deste computador
                threading.Thread(target=wifi_setup, args=(board,), daemon=True).start()
            board_action(line)
            if line:
                board.log(line)
        return True
    except (OSError, ValueError, serial.SerialException) as e:
        board.log(f"serial: {e}")
        board.ser.close()
        return False


# Placas pelo WiFi (o SmallTV-Ultra, que só tem USB de energia). Elas se anunciam por mDNS como
# _tokenpulse._tcp e recebem um frame enxuto por HTTP, com só o que a tela principal delas mostra.
NET_KEYS = ("p5", "r5", "pw", "rw", "tot", "rate", "sp", "m", "org", "clk", "dark", "age", "mods", "projs", "hist", "accts", "cx",
            "prov", "cxok", "st", "pask", "cxo", "mc", "mcs", "mca", "upd", "mask", "lask")
# o codinome de cada placa (uma árvore que lembra ela), o mesmo do firmware; nos logs no lugar do modelo
NICKS = {"smalltv-ultra": "Caixeta", "waveshare-amoled-216": "Jabuticaba", "guition-jc3248w535": "Sumaúma",
         "cyd-2432s028r": "Ipê", "waveshare-s3-lcd-147b": "Açaí"}
NET_EVERY = 2  # s entre frames: pelo WiFi, 1 por segundo só gastaria a placa
net_boards = {}  # host -> NetBoard
_latest = {}     # o último frame montado pelo laço principal


class NetBoard:
    def __init__(self, host, ip, name, fw):
        self.host, self.ip, self.name, self.fw = host, ip, name, fw
        self.path = f"http://{host}"
        self.fails = 0

    def log(self, msg):
        log(f"[{NICKS.get(self.name, self.name)}] {msg}")


def http(url, data=None, headers=None, timeout=4):
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers or {}), timeout=timeout) as r:
        return r.read()


def browse_mdns(seconds=3):
    """Nomes das placas anunciadas na rede, pelo dns-sd do macOS (ele não termina sozinho) ou pelo avahi-browse."""
    if not MAC:
        try:
            out = subprocess.run(["avahi-browse", "-ptk", "_tokenpulse._tcp"], capture_output=True, text=True,
                                 timeout=seconds + 5).stdout
        except (OSError, subprocess.TimeoutExpired):
            return set()
        return {f[3] for f in (line.split(";") for line in out.splitlines()) if f[0] == "+" and len(f) > 3}
    try:
        p = subprocess.Popen(["dns-sd", "-B", "_tokenpulse._tcp", "local."], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return set()
    time.sleep(seconds)
    p.terminate()
    out = p.communicate(timeout=5)[0]
    return {line.split()[-1] for line in out.splitlines() if " Add " in line}


def resolve4(host):
    """IPv4 do nome .local. Sem pedir só IPv4, o macOS espera 5 s por um AAAA que o mDNS do ESP8266 não responde."""
    try:
        return socket.getaddrinfo(host, 80, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
    except (OSError, IndexError):
        return None


def board_token(host):
    try:
        return json.load(open(KEYS)).get(host, "")
    except (OSError, ValueError):
        return ""


def save_token(host, token):
    """Guarda o token que a placa entregou no /hello: sem ele, o OTA e a captura pelo WiFi são recusados."""
    try:
        keys = json.load(open(KEYS))
    except (OSError, ValueError):
        keys = {}
    if keys.get(host) == token:
        return
    keys[host] = token
    fd = os.open(KEYS + ".tmp", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(keys, f)
    os.replace(KEYS + ".tmp", KEYS)


def auth_headers(host):
    token = board_token(host)
    if not token:
        return {}
    basic = base64.b64encode(f"tokenpulse:{token}".encode()).decode()
    return {"X-Token": token, "Authorization": f"Basic {basic}"}


def net_hello(ip, host):
    try:
        h = json.loads(http(f"http://{ip}/hello", timeout=3))
        if h.get("token"):
            save_token(host, h["token"])
        return h["board"], h.get("fw", "")
    except (OSError, ValueError, KeyError, TypeError):
        return None


def net_ota(board, path):
    """POST multipart no /update da placa (ESP8266HTTPUpdateServer, campo "firmware")."""
    data = open(path, "rb").read()
    boundary = "tokenpulse" + hashlib.md5(data).hexdigest()
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
    try:
        reply = http(f"http://{board.ip}/update", body,
                     {"Content-Type": f"multipart/form-data; boundary={boundary}"} | auth_headers(board.host), timeout=120)
    except OSError as e:
        return str(e)
    reply = reply.decode(errors="replace")
    return None if "Update Success" in reply else reply.strip()[:120]


def session_list(frame, limit=8):
    """Sessões para a placa sem toque, que não navega pelos workspaces: [[nome, estado, minutos]] e a contagem
    [trabalhando, esperando você, paradas]. A ordem é trabalhando, esperando você (aprovar antes de sua vez) e
    paradas, as mais recentes primeiro em cada grupo. Espera de mais de 1 h conta como parada, e automação do
    Agent SDK também: ninguém espera a sua resposta nela."""
    rows = []
    for r in (frame.get("sess") or {}).get("rows") or []:
        st, mins = r[2], r[4]
        if st in ("block", "turn") and not (0 <= mins <= 60 and r[1] not in ("SDK", "Multica")):
            st = "idle"
        rows.append([r[0][:28], st, mins])
    group = {"work": 0, "block": 1, "turn": 1}
    rows.sort(key=lambda r: (group.get(r[1], 2), r[1] != "block", r[2] if r[2] >= 0 else 10 ** 9))
    count = [sum(1 for r in rows if r[1] == "work"), sum(1 for r in rows if r[1] in ("block", "turn")),
             sum(1 for r in rows if r[1] == "idle")]
    return rows[:limit], count


def net_loop():
    pending = {}  # host -> até quando insistir no /hello (placa achada no mDNS, ou reiniciando do OTA)
    last_browse = 0

    def drop(host):
        net_boards.pop(host, None)
        save_boards()

    while True:
        if time.time() - last_browse > 20:
            last_browse = time.time()
            for name in browse_mdns():
                if f"{name}.local" not in net_boards:
                    pending.setdefault(f"{name}.local", time.time() + 10)
        for host, until in list(pending.items()):
            if (ip := resolve4(host)) and (h := net_hello(ip, host)):
                del pending[host]
                net_boards[host] = board = NetBoard(host, ip, *h)
                save_boards()
                board.log(f"conectada em http://{host}")
            elif time.time() > until:
                del pending[host]
        frame = _latest.get("frame")
        body = None
        if frame:
            slim = {k: frame[k] for k in NET_KEYS if k in frame}
            # o detalhe por modelo usa tudo: tokens, %, entrada, saída, cache lido e escrito, custo e projetos
            slim["mods"] = slim.get("mods", [])[:3]
            if "mc" in slim:  # o SmallTV mostra só os números e o primeiro agente: o resto pesaria no heap dele
                slim["mc"] = {k: v for k, v in slim["mc"].items() if k in ("c", "r")} | {"a": slim["mc"]["a"][:1]}
            slim["ses"], slim["sesn"] = session_list(frame)
            slim["agv"] = AGENT  # a página da placa mostra a versão do agente ao lado da do firmware
            body = json.dumps(slim, ensure_ascii=False).encode()
        for host, board in list(net_boards.items()):
            if path := pending_ota(board.name) or auto_ota(board, warn=False):
                t0 = time.time()
                err = net_ota(board, path)
                board.log(f"ota: {err or 'ok'} ({path}, {time.time() - t0:.0f} s)")
                drop(host)
                pending[host] = time.time() + 90  # volta pelo /hello assim que o firmware novo subir
                continue
            if not body:
                continue
            try:
                req = urllib.request.Request(f"http://{board.ip}/frame", body,
                                             {"Content-Type": "application/json"} | auth_headers(host))
                with urllib.request.urlopen(req, timeout=4) as r:
                    reply, proof = r.read(), r.headers.get("X-Token", "")
                board.fails = 0
                if reply:  # a página da placa pediu uma ação (Provedores, Multica, atualizar): vem na resposta do frame
                    try:
                        act = str(json.loads(reply).get("act", ""))
                    except (ValueError, AttributeError):
                        act = ""
                    token = board_token(host)
                    # pelo WiFi, só o que a página do SmallTV oferece, e só de quem mostra o token que a placa nos deu:
                    # outro aparelho que se anuncie como _tokenpulse não troca a conta nem roda nada no computador
                    if not act.split()[:1] or act.split()[0] not in NET_ACTIONS:
                        board.log(f"ação pelo WiFi recusada: {act[:40]!r}")
                    elif not token or not hmac.compare_digest(proof, token):
                        board.log(f"ação pelo WiFi recusada: placa sem o token pareado ({act[:20]!r})")
                    else:
                        board_action("ACTION " + act)
            except OSError as e:
                board.fails += 1
                if board.fails >= 3:  # 6 s sem resposta: some até o mDNS achar de novo
                    board.log(f"sem resposta: {e}")
                    drop(host)
        time.sleep(NET_EVERY)


DEMO = "--demo" in sys.argv
AGENT = agent_version()
RELEASE = os.environ.get("TOKENPULSE_RELEASE") or AGENT  # o release deste daemon; o env simula um, para testar
DEMO_P5 = int(os.environ.get("TOKENPULSE_DEMO_P5", "42"))  # para mostrar as faixas: 42, 74, 86, 95
DEMO_ST = int(os.environ.get("TOKENPULSE_DEMO_ST", "0"))
DEMO_MULTICA = os.environ.get("TOKENPULSE_DEMO_MULTICA") == "1"
DEMO_CODEX = os.environ.get("TOKENPULSE_DEMO_CODEX") == "1"  # o bloco do Codex no frame de demonstração  # o aviso da página de status: 1 instável, 2 fora do ar


def main():
    log(f"tokend {AGENT}")
    usage, codex = Usage(), CodexUsage()
    boards = {}   # caminho da porta -> Board
    save_boards(boards)  # o arquivo da execução anterior não vale mais
    ignored = {}  # caminho da porta -> quando tentar de novo (não respondeu como placa)
    first_fail = {}  # caminho da porta -> a primeira falha desde que plugou
    if not DEMO:
        threading.Thread(target=usage_loop, daemon=True).start()
        threading.Thread(target=history_loop, daemon=True).start()
        threading.Thread(target=herdr_events, daemon=True).start()
        threading.Thread(target=status_loop, daemon=True).start()
        threading.Thread(target=multica_loop, daemon=True).start()
        threading.Thread(target=release_loop, daemon=True).start()
        threading.Thread(target=login_loop, daemon=True).start()
    if "--once" not in sys.argv:
        threading.Thread(target=net_loop, daemon=True).start()
        threading.Thread(target=ble_loop, daemon=True).start()
    if "--once" in sys.argv:
        time.sleep(3)  # dá tempo da primeira busca na API
    while True:
        woke = _wake.is_set()
        _wake.clear()
        if DEMO:
            frame = demo_frame()
        else:
            if not woke or "--once" in sys.argv:  # acordado por evento do herdr: manda já, os .jsonl esperam o próximo segundo
                usage.poll()
                codex.poll()
            org_name = org()  # antes de limits(), que depende de saber se a org mudou
            # a escolha explícita em Provedores vale mesmo sem uso recente: não muda a placa sozinha depois de 7 dias
            cx = codex.snapshot(chosen=provider_mode() in ("codex", "both"))
            frame = (usage.snapshot() | limits() | account_frame() | ({"st": _claude["st"]} if _claude["st"] else {})
                     | {"clk": datetime.now().strftime("%H:%M"), "t": local_time(), "org": org_name, "dark": dark_mode(), "agv": AGENT})
            if cx or os.path.exists(PROVIDERS):  # depois de escolhido, o ajuste continua nos ajustes da placa
                frame = with_codex(frame, cx, usage.events, codex, usage.recent)
            frame |= multica_frame() | update_frame() | login_frame()
        _latest["frame"] = frame
        if "--once" in sys.argv:
            print(json.dumps(frame, ensure_ascii=False))
            return
        ports = {p.device: p for p in list_ports.comports() if p.vid in BOARD_VIDS}
        for path in [p for p in ignored if p not in ports]:
            del ignored[path]  # desplugou: na próxima vez que aparecer, pergunta de novo
            first_fail.pop(path, None)
        for path, info in sorted(ports.items()):
            if path in boards or ignored.get(path, 0) > time.time():
                continue
            try:
                board = connect(path, info.vid)
            except (OSError, serial.SerialException) as e:
                board = None
                if not MAC and "Errno 13" in str(e) and path not in first_fail:
                    log(f"{path}: sem permissão; rode: sudo usermod -aG {serial_group(path)} $USER e entre de novo na sessão")
            if board:
                first_fail.pop(path, None)
                boards[path] = board
                save_boards(boards)
                board.log(f"conectada em {path}" + (f", último reset: {board.reset}" if board.reset else ""))
            else:
                # ESP32 com USB nativa pode estar no boot (abrir a porta não a reinicia): de 5 em 5 s no primeiro
                # minuto do plug, depois de 60 em 60 s, para não disputar a porta de uma S3 de outro projeto.
                # Abrir uma ponte USB-serial (CH340, CP210x) reinicia o aparelho do outro lado: uma vez por plug
                native = info.vid == ESPRESSIF_VID
                if path not in first_fail:
                    first_fail[path] = time.time()
                    log(f"{path}: sem resposta ao id; " + ("tento de novo" if native else "só testo de novo se replugar"))
                retry = 5 if time.time() - first_fail[path] < 60 else 60
                ignored[path] = time.time() + (retry if native else 10 ** 9)
        global _usb_ble
        _usb_ble = {b.ble for b in boards.values() if b.ble}  # troca do conjunto inteiro: a thread do BLE só lê
        for path, board in list(boards.items()):
            if not serve(board, frame):
                del boards[path]
                save_boards(boards)
        # até 1 s de espera, mas acorda antes com evento do herdr ou com a placa mandando algo (um toque que pede
        # para abrir a issue não espera o próximo frame)
        for _ in range(10):
            if _wake.wait(0.1):
                time.sleep(0.1)  # junta uma rajada de eventos do herdr num frame só
                break
            if any(waiting(b) for b in boards.values()):
                break


if __name__ == "__main__":
    ble_worker() if "--ble-worker" in sys.argv else main()
