#include "screen.h"

#include <LovyanGFX.hpp>
#include <SPI.h>

#include "../ui/fonts/inter_hero_s.h"
#include "../ui/fonts/inter_label_m.h"
#include "../ui/fonts/inter_num_m.h"
#include "../ui/theme.h"
#include "app.h"
#include "config.h"
#include "net.h"

namespace screen {
namespace {

using namespace color;

constexpr int W = 240, H = 240;
constexpr int BACKLIGHT = 5;  // PWM, acende em nível baixo

// ST7789 de 240x240 no SPI de hardware do ESP8266 (SCLK 14, MOSI 13 fixos). Pinos do SmallTV:
// https://github.com/giovi321/smalltv-mod/blob/main/docs/src/content/docs/getting-started/hardware.md
class Lcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;

 public:
  Lcd() {
    auto b = bus.config();
    // o CS do painel é ligado no GND e ele só lê em modo 3. A LovyanGFX repassa spi_mode ao
    // SPI.setDataMode do core, onde o modo 3 é 0x11: o valor 3 vira modo 1 e a tela fica preta
    b.spi_mode = SPI_MODE3;
    b.freq_write = 40000000;
    b.pin_dc = 0;
    bus.config(b);
    panel.setBus(&bus);
    auto p = panel.config();
    p.pin_cs = 15;
    p.pin_rst = 2;
    p.panel_width = p.memory_width = W;
    p.panel_height = H;
    p.memory_height = 320;
    p.invert = true;
    p.readable = false;
    panel.config(p);
    setPanel(&panel);
  }
};

Lcd lcd;
// Sem PSRAM e com ~40 KB de heap, o quadro (115 KB) não cabe: a tela sai em faixas de BAND linhas,
// todas desenhadas com as coordenadas da tela inteira deslocadas por `oy`.
LGFX_Sprite band(&lcd);
int BAND = 30, oy = 0;

struct Vlw {
  const uint8_t* data;
  lgfx::PointerWrapper wrap;
  lgfx::VLWfont font;
};
Vlw LABEL{inter_label_m}, NUM{inter_num_m}, HERO{inter_hero_s};

void text(const char* s, int x, int y, Vlw& f, uint16_t c, uint8_t datum = top_left) {
  if (y > oy + BAND || y + 48 < oy) return;  // fora da faixa: nem passa pela fonte
  band.setFont(&f.font);
  band.setTextDatum(datum);
  band.setTextColor(c, BG);
  band.drawString(s, x, y - oy);
}

int textWidth(const char* s, Vlw& f) {
  band.setFont(&f.font);
  return band.textWidth(s);
}

// Estado do que está na tela: valores que andam até os do frame (a mola), e quando cada alerta começou.
struct Ui {
  float p5 = -1, pw = -1;
  uint32_t tokMs = 0, bandMs = 0, approveMs = 0;
  int level = -1, blocks = -1;  // -1 até o primeiro frame: o boot não dispara alerta
  String tot;
  int dotX = 0;
} ui;

uint32_t since(uint32_t t) { return t ? millis() - t : UINT32_MAX; }
bool bandBlink() { return (config::v.alerts & 2) && since(ui.bandMs) < 1500 && since(ui.bandMs) / 250 % 2; }

void drawMain() {
  const auto& f = app::frame;
  char buf[48];
  const bool live = millis() - app::lastFrameMs <= 10000;
  const bool stale = (f["age"] | 0) > 300;  // o % de 5h e da semana ficou velho
  band.fillScreen(BG);

  String org = f["org"] | "";
  if (org.length() > 18) org = org.substring(0, 17) + "…";
  text(org.c_str(), 8, 6, LABEL, TEXT);
  text(f["clk"] | "", W - 8, 6, LABEL, DIM, top_right);
  text(f["m"] | "Claude", 8, 22, LABEL, MUTED);
  const char* st = live ? "ao vivo" : "offline";
  text(st, W - 8, 22, LABEL, live ? GREEN : RED, top_right);
  ui.dotX = W - 16 - textWidth(st, LABEL);
  band.fillCircle(ui.dotX, 30 - oy, 3, live ? GREEN : RED);
  band.drawFastHLine(8, 42 - oy, W - 16, TRACK);

  // anel da janela de 5 h
  const int p5 = f["p5"] | -1, cx = 64, cy = 100, r = 54;
  const float shown = ui.p5 < 0 ? p5 : ui.p5;  // o anel anda até o valor novo
  const uint16_t ring = stale ? DIM : bandBlink() ? TEXT : levelColor(p5);
  band.fillArc(cx, cy - oy, r - 9, r, 0, 360, TRACK);
  if (shown >= 0.5f) band.fillArc(cx, cy - oy, r - 9, r, 270, 270 + 3.6f * min(shown, 100.f), ring);
  snprintf(buf, sizeof buf, p5 < 0 ? "--" : "%d%%", (int)lroundf(shown));
  Vlw& pf = textWidth(buf, HERO) <= 2 * (r - 9) - 12 ? HERO : NUM;  // "42%" na fonte grande não cabe no anel
  text(buf, cx, cy - (&pf == &HERO ? 18 : 12), pf, p5 < 0 ? TEXT : ring, top_center);
  text("sessão 5h", cx, 162, LABEL, MUTED, top_center);
  if (stale) {
    snprintf(buf, sizeof buf, "há %d min", (int)(f["age"] | 0) / 60);
    text(buf, cx, 178, LABEL, AMBER, top_center);
  } else {
    text(f["r5"] | "", cx, 178, LABEL, p5 >= 70 ? ring : MUTED, top_center);
  }

  // coluna da direita: semana, tokens do dia, ritmo
  const int x = 130, w = W - 8 - x, pw = f["pw"] | -1;
  text("semana", x, 50, LABEL, MUTED);
  snprintf(buf, sizeof buf, pw < 0 ? "--" : "%d%%", pw);
  text(buf, x + w, 50, LABEL, stale ? DIM : TEXT, top_right);
  band.fillRoundRect(x, 68 - oy, w, 6, 3, TRACK);
  const float pwShown = ui.pw < 0 ? pw : ui.pw;
  if (pwShown >= 0.5f) band.fillRoundRect(x, 68 - oy, max(6, (int)(w * min(pwShown, 100.f) / 100)), 6, 3, stale ? DIM : levelColor(pw));
  text(f["rw"] | "", x, 80, LABEL, DIM);
  text("tokens hoje", x, 104, LABEL, MUTED);
  const uint32_t glow = since(ui.tokMs);  // subiu: o número acende em branco e volta à cor em 0,9 s
  text(f["tot"] | "0", x, 120, NUM, glow < 900 ? blend(TEXT, ACCENT, glow / 900.f) : ACCENT);
  text("ritmo/min", x, 150, LABEL, MUTED);
  text(f["rate"] | "0", x, 166, NUM, TEXT);

  // última hora
  JsonArrayConst sp = f["sp"];
  const int n = sp.size(), sx = 8, sw = W - 16, base = 212, sh = 20;  // a moldura cobre as últimas linhas
  uint32_t peak = 1;
  for (uint32_t v : sp) peak = max(peak, v);
  auto yAt = [&](int i) { return base - (int)((uint32_t)sp[i] * sh / peak) - oy; };
  for (int i = 1; i < n; i++) band.drawLine(sx + (i - 1) * sw / (n - 1), yAt(i - 1), sx + i * sw / (n - 1), yAt(i), ACCENT);
  band.drawFastHLine(sx, base + 1 - oy, sw, TRACK);
  text("-60 min", sx, 216, LABEL, DIM);
  text("agora", sx + sw, 216, LABEL, DIM, top_right);
}

void step(int y, bool ok, const char* name, const char* hint) {
  band.fillCircle(30, y + 8 - oy, 6, ok ? GREEN : TRACK);
  text(name, 46, y, LABEL, ok ? TEXT : MUTED);
  if (!ok && hint) text(hint, 46, y + 16, LABEL, YELLOW);
}

// Cabeçalho das telas extras: título à esquerda, relógio à direita.
void header(const char* title) {
  band.fillScreen(BG);
  text(title, 8, 6, LABEL, TEXT);
  text(app::frame["clk"] | "", W - 8, 6, LABEL, DIM, top_right);
  band.drawFastHLine(8, 26 - oy, W - 16, TRACK);
}

uint16_t modelColor(const char* name) {
  if (strstr(name, "Opus")) return ACCENT;
  if (strstr(name, "Sonnet")) return GREEN;
  return AMBER;
}

void drawModels() {
  char buf[64];
  header("modelos · hoje");
  int y = 36;
  for (JsonArrayConst m : app::frame["mods"].as<JsonArrayConst>()) {  // até 3
    const char* name = m[0] | "?";
    const int share = m[2] | 0;
    text(name, 8, y, LABEL, TEXT);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)(m[1] | ""), share);
    text(buf, W - 8, y, LABEL, MUTED, top_right);
    band.fillRoundRect(8, y + 18 - oy, W - 16, 6, 3, TRACK);
    if (share > 0) band.fillRoundRect(8, y + 18 - oy, max(6, (W - 16) * min(share, 100) / 100), 6, 3, modelColor(name));
    snprintf(buf, sizeof buf, "in %s · out %s", (const char*)(m[3] | ""), (const char*)(m[4] | ""));
    text(buf, 8, y + 28, LABEL, DIM);
    snprintf(buf, sizeof buf, "lido %s · escrito %s", (const char*)(m[5] | ""), (const char*)(m[6] | ""));
    text(buf, 8, y + 43, LABEL, DIM);
    y += 62;
  }
}

void drawProjects() {
  char buf[64];
  header("projetos · 1h");
  JsonArrayConst projs = app::frame["projs"];
  constexpr int ROWS = 6;
  int y = 34, i = 0;
  for (JsonArrayConst pr : projs) {
    if (i++ == ROWS) break;
    String name = pr[0] | "?";
    if (name.length() > 24) name = name.substring(0, 23) + "…";
    const char* origin = pr[1] | "CLI";
    const int mins = pr[3] | 0;
    text(name.c_str(), 8, y, LABEL, TEXT);
    text(origin, W - 8, y, LABEL, strcmp(origin, "Desk") ? GREEN : ACCENT, top_right);
    if (mins == 0) snprintf(buf, sizeof buf, "%s · agora", (const char*)(pr[2] | ""));
    else snprintf(buf, sizeof buf, "%s · há %d min", (const char*)(pr[2] | ""), mins);
    text(buf, 8, y + 15, LABEL, DIM);
    y += 30;
  }
  if ((int)projs.size() > ROWS) {
    snprintf(buf, sizeof buf, "+%d", (int)projs.size() - ROWS);
    text(buf, W - 8, 214, LABEL, DIM, top_right);
  }
}

// Corta o texto até caber em maxW pixels, com reticências.
String fit(String s, int maxW, Vlw& f) {
  if (textWidth(s.c_str(), f) <= maxW) return s;
  while (s.length() && textWidth((s + "…").c_str(), f) > maxW) s.remove(s.length() - 1);
  while (s.length() && ((uint8_t)s[s.length() - 1] & 0xC0) == 0x80) s.remove(s.length() - 1);  // UTF-8 inteiro
  return s + "…";
}

void ago(char* buf, size_t n, int mins) {
  if (mins < 0) *buf = 0;
  else if (mins < 1) snprintf(buf, n, "agora");
  else if (mins < 60) snprintf(buf, n, "%d min", mins);
  else if (mins < 1440) snprintf(buf, n, "%d h", mins / 60);
  else snprintf(buf, n, "%d d", mins / 1440);
}

// Uma linha por sessão: trabalhando, esperando você (aprovar, sua vez) e paradas, nessa ordem; o daemon
// já manda ordenado, com espera de mais de 1 h contada como parada.
void drawSessions() {
  char buf[48], when[16];
  header("sessões");
  // o contador é a legenda das cores: cada estado com o número na cor do seu ponto, só os que têm sessão
  JsonArrayConst n = app::frame["sesn"];
  int blocks = 0;
  for (JsonArrayConst s : app::frame["ses"].as<JsonArrayConst>()) blocks += !strcmp(s[1] | "", "block");
  const int act = n[0] | 0, idle = n[2] | 0, turns = max(0, (int)(n[1] | 0) - blocks);
  struct { int count; const char *one, *many; uint16_t c; } parts[] = {
      {act, "ativa", "ativas", GREEN}, {blocks, "aprovar", "aprovar", RED},
      {turns, "sua vez", "sua vez", YELLOW}, {idle, "parada", "paradas", DIM}};
  char seg[4][24];
  int shown = 0;
  for (auto& pt : parts)
    if (pt.count) snprintf(seg[shown++], sizeof seg[0], "%d %s", pt.count, pt.count == 1 ? pt.one : pt.many);
  const char* sep = " · ";
  auto width = [&](int k) {
    int w = 0;
    for (int i = 0; i < k; i++) w += textWidth(seg[i], LABEL) + (i ? textWidth(sep, LABEL) : 0);
    return w;
  };
  int k = shown;
  if (width(k) > W - 16) sep = "  ";
  if (width(k) > W - 16 && idle) k--;  // ainda não coube: as paradas saem do contador (a lista mostra)
  int x = 8;
  for (int i = 0, j = 0; i < 4 && j < k; i++) {
    if (!parts[i].count) continue;
    if (j) text(sep, x, 32, LABEL, DIM), x += textWidth(sep, LABEL);
    text(seg[j], x, 32, LABEL, parts[i].c);
    x += textWidth(seg[j++], LABEL);
  }
  int y = 54;
  for (JsonArrayConst s : app::frame["ses"].as<JsonArrayConst>()) {  // até 8
    const char* st = s[1] | "";
    const bool work = !strcmp(st, "work"), block = !strcmp(st, "block"), turn = !strcmp(st, "turn");
    // uma cor por estado, sem tons vizinhos: verde trabalha, vermelho pede aprovação, amarelo é sua vez
    const uint16_t c = work ? GREEN : block ? RED : turn ? YELLOW : DIM;
    ago(when, sizeof when, s[2] | -1);
    // à direita só o essencial, para sobrar nome: a cor e o contador do topo dizem o resto
    snprintf(buf, sizeof buf, "%s", work ? "ativa" : block ? "aprovar" : when);
    band.fillCircle(12, y + 7 - oy, 4, c);
    const int rw = textWidth(buf, LABEL);
    text(fit(s[0] | "?", W - 8 - 22 - rw - 8, LABEL).c_str(), 22, y, LABEL, work || block || turn ? TEXT : MUTED);
    text(buf, W - 8, y, LABEL, c, top_right);
    y += 20;
  }
}

// Detalhe de um modelo: [nome, tokens, %, entrada, saída, cache lido, cache escrito, custo, [[projeto, tokens]]].
void drawModel(int i) {
  JsonArrayConst m = app::frame["mods"][i];
  const char* name = m[0] | "?";
  const uint16_t c = modelColor(name);
  const int share = m[2] | 0;
  char buf[48];
  header(name);
  text(m[1] | "0", 8, 34, NUM, c);
  snprintf(buf, sizeof buf, "%d%% do dia", share);
  text(buf, W - 8, 40, LABEL, MUTED, top_right);
  band.fillRoundRect(8, 64 - oy, W - 16, 6, 3, TRACK);
  if (share > 0) band.fillRoundRect(8, 64 - oy, max(6, (W - 16) * min(share, 100) / 100), 6, 3, c);
  const char* names[] = {"entrada", "saída", "cache lido", "cache escrito"};
  for (int k = 0; k < 4; k++) {
    const int x = k % 2 ? W / 2 + 4 : 8, y = 80 + k / 2 * 34;
    text(names[k], x, y, LABEL, MUTED);
    text(m[3 + k] | "", x, y + 15, LABEL, TEXT);
  }
  int y = 150;
  if (!config::v.hideCost) {
    text("custo na API", 8, y, LABEL, MUTED);
    text(m[7] | "", W - 8, y, LABEL, ACCENT, top_right);
    y += 22;
  }
  for (JsonArrayConst pr : m[8].as<JsonArrayConst>()) {
    if (y > 200) break;
    const char* tok = pr[1] | "";
    text(fit(pr[0] | "?", W - 16 - textWidth(tok, LABEL) - 8, LABEL).c_str(), 8, y, LABEL, DIM);
    text(tok, W - 8, y, LABEL, DIM, top_right);
    y += 18;
  }
}

// Decimal com vírgula, como o daemon escreve nos outros números.
void comma(char* s) {
  for (; *s; s++)
    if (*s == '.') *s = ',';
}

// Histórico: [dia da semana, dia do mês, tokens em milhões, custo em US$], hoje por último.
void drawHistory() {
  char buf[48];
  header("histórico · 14 dias");
  JsonArrayConst hist = app::frame["hist"];
  const int n = hist.size();
  if (!n) return;
  float peak = 0.1f, sum = 0;
  int cost = 0;
  for (JsonArrayConst d : hist) peak = max(peak, (float)(d[2] | 0.f)), sum += d[2] | 0.f, cost += d[3] | 0;
  snprintf(buf, sizeof buf, "hoje %.1fM", (float)(hist[n - 1][2] | 0.f));
  comma(buf);
  text(buf, 8, 34, LABEL, TEXT);
  snprintf(buf, sizeof buf, "média %.0fM/dia", sum / n);
  text(buf, W - 8, 34, LABEL, MUTED, top_right);
  const int top = 58, base = 188, gap = 4, bw = (W - 16 - gap * (n - 1)) / n;
  for (int i = 0; i < n; i++) {
    const int x = 8 + i * (bw + gap), h = max(2, (int)((base - top) * (float)(hist[i][2] | 0.f) / peak));
    band.fillRect(x, base - h - oy, bw, h, i == n - 1 ? ACCENT : MUTED);
    if ((n - 1 - i) % 2 == 0) {  // um dia sim, um não: dois dígitos não cabem embaixo de cada barra
      snprintf(buf, sizeof buf, "%d", (int)(hist[i][1] | 0));
      text(buf, x + bw / 2, base + 4, LABEL, i == n - 1 ? TEXT : DIM, top_center);
    }
  }
  if (sum >= 1000) snprintf(buf, sizeof buf, "14 dias: %.1fB tokens", sum / 1000);
  else snprintf(buf, sizeof buf, "14 dias: %.0fM tokens", sum);
  comma(buf);
  if (!config::v.hideCost) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " · US$ %d", cost);
  text(buf, 8, 214, LABEL, MUTED);
}

// Orgs do claude-swap: [número, org, ativa, precisa de login, % 5h, % semana, minutos desde a leitura].
// Só leitura: trocar de org pede toque, que esta placa não tem.
void drawOrgs() {
  char buf[48];
  header("orgs");
  int y = 36;
  for (JsonArrayConst a : app::frame["accts"].as<JsonArrayConst>()) {
    if (y > 190) break;
    const bool active = a[2] | 0, relogin = a[3] | 0;
    const int p5 = a[4] | -1, pw = a[5] | -1;
    text(fit(a[1] | "?", 150, LABEL).c_str(), 8, y, LABEL, active ? TEXT : MUTED);
    if (active) text("ativa", W - 8, y, LABEL, ACCENT, top_right);
    else if (relogin) text("precisa de login", W - 8, y, LABEL, AMBER, top_right);
    band.fillRoundRect(8, y + 20 - oy, 96, 6, 3, TRACK);
    if (p5 > 0) band.fillRoundRect(8, y + 20 - oy, max(6, 96 * min(p5, 100) / 100), 6, 3, levelColor(p5));
    snprintf(buf, sizeof buf, p5 < 0 ? "5h --" : "5h %d%%", p5);
    text(buf, 112, y + 15, LABEL, p5 < 0 ? DIM : levelColor(p5));
    snprintf(buf, sizeof buf, pw < 0 ? "sem --" : "sem %d%%", pw);
    text(buf, W - 8, y + 15, LABEL, MUTED, top_right);
    y += 46;
  }
}

// Sem toque nem botão, as telas passam sozinhas a cada config::v.interval s. Tela sem conteúdo
// (nenhum projeto na última hora) fica de fora da volta.
enum Page : uint8_t { MAIN, SESSIONS, MODELS, MODEL, PROJECTS, HISTORY, ORGS };
struct Slot {
  Page page;
  uint8_t sub;  // qual modelo, no detalhe
};

// A volta de agora: a principal e as telas ligadas na configuração que têm conteúdo.
int slots(Slot* out) {
  const auto& f = app::frame;
  const uint8_t on = config::v.pages;
  int n = 0;
  out[n++] = {MAIN, 0};
  if ((on & 4) && f["ses"].size()) out[n++] = {SESSIONS, 0};
  if ((on & 1) && f["mods"].size()) out[n++] = {MODELS, 0};
  if (on & 8)
    for (int i = 0; i < (int)min<size_t>(f["mods"].size(), 3); i++) out[n++] = {MODEL, (uint8_t)i};
  if ((on & 2) && f["projs"].size()) out[n++] = {PROJECTS, 0};
  if ((on & 16) && f["hist"].size()) out[n++] = {HISTORY, 0};
  if ((on & 32) && f["accts"].size() > 1) out[n++] = {ORGS, 0};  // com uma org só, a tela não diz nada
  return n;
}

Slot pageNow() {
  static Slot cur = {MAIN, 0};
  static uint32_t sinceMs = 0;
  Slot list[10];
  const int n = slots(list);
  int at = -1;
  for (int i = 0; i < n; i++)
    if (list[i].page == cur.page && list[i].sub == cur.sub) at = i;
  // alertas passam na frente da volta: sessão pedindo aprovação segura a tela de sessões enquanto durar,
  // e cruzar 70/80/90% segura a principal por 2 min
  if ((config::v.alerts & 1) && ui.blocks > 0) return sinceMs = millis(), cur = {SESSIONS, 0};
  if ((config::v.alerts & 2) && since(ui.bandMs) < 120000) return sinceMs = millis(), cur = {MAIN, 0};
  if (!config::v.interval) return cur = {MAIN, 0};
  if (at < 0 || millis() - sinceMs >= config::v.interval * 1000UL) {  // passou o tempo, ou a tela sumiu
    sinceMs = millis();
    cur = list[(at + 1) % n];
  }
  return cur;
}

void draw(Slot slot) {
  if (slot.page == SESSIONS) drawSessions();
  else if (slot.page == MODELS) drawModels();
  else if (slot.page == MODEL) drawModel(slot.sub);
  else if (slot.page == PROJECTS) drawProjects();
  else if (slot.page == HISTORY) drawHistory();
  else if (slot.page == ORGS) drawOrgs();
  else drawMain();
}

// OTA pelo /update: progresso vindo do Updater a cada bloco gravado.
size_t otaDone = 0, otaTotal = 0;
uint32_t otaMs = 0;  // último bloco: 3 s sem nenhum e sem reiniciar é falha

void drawOta() {
  char buf[48];
  band.fillScreen(BG);
  const bool failed = since(otaMs) > 3000;
  const float frac = min(1.f, (float)otaDone / max<size_t>(otaTotal, 1));
  text(failed ? "falhou" : frac >= 1 ? "firmware gravado" : "atualizando firmware", W / 2, 64, NUM, failed ? RED : TEXT, top_center);
  text(failed ? "a placa continua na versão atual" : frac >= 1 ? "reiniciando…" : "não desligue a placa", W / 2, 96, LABEL, MUTED, top_center);
  band.fillRoundRect(24, 132 - oy, W - 48, 10, 5, TRACK);
  if (otaDone) band.fillRoundRect(24, 132 - oy, max(10, (int)((W - 48) * frac)), 10, 5, failed ? RED : frac >= 1 ? GREEN : ACCENT);
  snprintf(buf, sizeof buf, "%d%% · %u de %u KB", (int)(frac * 100), otaDone / 1024, otaTotal / 1024);
  text(buf, W / 2, 152, LABEL, DIM, top_center);
}

// WiFi ok e nenhum daemon falou: quase sempre é quem acabou de receber a placa. A tela leva à página que
// ensina a instalar o daemon no Mac, por QR code e pelo endereço escrito.
void drawInstall() {
  band.fillScreen(BG);
  text("falta o tokenpulse no Mac", W / 2, 4, LABEL, TEXT, top_center);
  band.qrcode("https://gilsondev.github.io/tokenpulse/comecar/", (W - 148) / 2, 22 - oy, 148);
  // o QR leva ao github.io, que não depende do encurtador; o endereço escrito é o curto, para digitar no Mac
  text("escaneie, ou abra no Mac:", W / 2, 178, LABEL, MUTED, top_center);
  text("tokenpulse.s.gy/comecar", W / 2, 198, LABEL, ACCENT, top_center);  // na fonte de 22 px não cabe em 240
}

// Sem rede: a placa abriu o próprio WiFi. O QR code de WiFi faz a câmera do celular oferecer a conexão; no
// celular, o portal cativo abre a página de configuração sozinho.
void drawAp() {
  char qr[48];
  snprintf(qr, sizeof qr, "WIFI:T:nopass;S:%s;;", net::apName());
  band.fillScreen(BG);
  text("configure o WiFi da placa", W / 2, 4, LABEL, TEXT, top_center);
  band.qrcode(qr, (W - 128) / 2, 22 - oy, 128);
  text("1. escaneie, ou entre no WiFi", W / 2, 156, LABEL, MUTED, top_center);
  text(net::apName(), W / 2, 172, LABEL, ACCENT, top_center);
  text("2. a página abre sozinha; se não,", W / 2, 192, LABEL, MUTED, top_center);
  text("abra 192.168.4.1", W / 2, 208, LABEL, ACCENT, top_center);
}

void drawWaiting() {
  char buf[64];
  if (net::apOn() && !net::online()) return drawAp();
  band.fillScreen(BG);
  text("sem dados do Mac", W / 2, 40, NUM, TEXT, top_center);
  if (app::lastFrameMs) {
    snprintf(buf, sizeof buf, "último dado há %lu min", (millis() - app::lastFrameMs) / 60000);
    text(buf, W / 2, 70, LABEL, MUTED, top_center);
  }
  const bool wifi = net::online();
  const bool daemon = wifi && net::lastDaemonMs() && millis() - net::lastDaemonMs() < 15000;
  // sem nenhum dado nunca e há 20 s no WiFi sem daemon: a tela de instalação no lugar das etapas
  if (wifi && !daemon && !app::lastFrameMs && millis() > 20000) return drawInstall();
  String hint = net::apOn() ? String("conecte em ") + net::apName() : String("procurando a rede salva…");
  step(100, wifi, "WiFi", hint.c_str());
  step(140, daemon, "daemon tokend", "rode host/install.sh no Mac");
  step(180, false, "dados", nullptr);
  text(net::address().c_str(), W / 2, 208, LABEL, DIM, top_center);
}

void backlight(uint8_t v) {
  static int written = -1;
  if (v != written) analogWrite(BACKLIGHT, 255 - v), written = v;
}

float lit = 0;  // brilho de agora; anda até o alvo a cada 15 ms (fade e pulso)

bool night() {
  const char* clk = app::frame["clk"] | "";  // horário de Brasília vindo do Mac
  if (strlen(clk) < 2) return false;
  const int h = atoi(clk);
  return h >= 22 || h < 7;
}

uint8_t brightness(bool data) {
  const int full = config::v.bright * 255 / 100;
  if ((config::v.alerts & 2) && since(ui.bandMs) < 120000) return full;  // cruzou faixa: brilho cheio por 2 min
  return data && config::v.night && night() ? max(6, full * 8 / 100) : full;
}

void backlightTick(bool data) {
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 15) return;
  lastMs = millis();
  float target = brightness(data);
  if ((config::v.alerts & 1) && ui.blocks > 0 && since(ui.approveMs) < 20000)  // pede aprovação: pulsa 20 s
    target *= 0.35f + 0.65f * (1 - breath(1200));
  lit += (target - lit) * 0.3f;
  backlight(lroundf(lit));
}

// Apaga em ~120 ms antes de trocar de tela; o backlightTick acende de novo.
void fadeOut() {
  while (lit > 2) lit *= 0.6f, backlight(lroundf(lit)), delay(15);
  lit = 0, backlight(0);
}

// Frame novo: alvos da mola, número que subiu e alertas.
void onFrame() {
  const auto& f = app::frame;
  const int p5 = f["p5"] | -1, pw = f["pw"] | -1;
  if (ui.p5 < 0) ui.p5 = p5;  // primeiro frame: sem animação
  if (ui.pw < 0) ui.pw = pw;
  const String tot = f["tot"] | "";
  if (ui.tot.length() && tot != ui.tot) ui.tokMs = millis();
  ui.tot = tot;
  const int lv = p5 >= 0 ? level(p5) : 0;
  if (ui.level >= 0 && lv > ui.level) ui.bandMs = millis();
  ui.level = lv;
  int blocks = 0;
  for (JsonArrayConst s : f["ses"].as<JsonArrayConst>()) blocks += !strcmp(s[1] | "", "block");
  if (ui.blocks >= 0 && blocks > ui.blocks) ui.approveMs = millis();
  ui.blocks = blocks;
}

// A mola: os valores mostrados andam 25% do caminho por passo. true enquanto ainda andam.
bool step() {
  bool moving = false;
  auto ease = [&](float& v, int target) {
    if (target < 0 || fabsf(target - v) < 0.3f) return void(v = target);
    v += (target - v) * 0.25f;
    moving = true;
  };
  ease(ui.p5, app::frame["p5"] | -1);
  ease(ui.pw, app::frame["pw"] | -1);
  return moving;
}

void render(int y0, int y1, Slot slot, bool data) {
  for (oy = 0; oy < H; oy += BAND) {
    if (oy + BAND <= y0 || oy >= y1) continue;
    if (otaTotal) drawOta();
    else data ? draw(slot) : drawWaiting();
    band.pushSprite(0, oy);
    yield();
  }
}

// O ponto de "ao vivo" pulsa direto no painel: redesenhar a faixa inteira por ele custaria demais.
void pulseDot() {
  if (millis() - app::lastFrameMs > 10000 || !ui.dotX) return;
  const float b = breath(1500);
  lcd.fillRect(ui.dotX - 5, 25, 11, 11, BG);
  lcd.fillCircle(ui.dotX, 30, 2 + lroundf(b * 2), blend(DARK_GREEN, GREEN, b));
}

}  // namespace

void begin() {
  analogWriteRange(255);
  backlight(0);  // apagada até o primeiro quadro: o painel liga com lixo na memória
  apply(true);
  // sem CS, a subida do clock ao trocar para o modo 3 conta como um bit: deixa o clock alto antes do reset
  SPI.begin();
  SPI.setDataMode(SPI_MODE3);
  lcd.init();
  for (Vlw* f : {&LABEL, &NUM, &HERO}) {
    f->wrap.set(f->data);
    f->font.loadFont(&f->wrap);
  }
  band.setColorDepth(16);
  while (!band.createSprite(W, BAND) && BAND > 10) BAND /= 2;
}

void otaProgress(size_t done, size_t total) {
  static int lastPct = -1;
  const bool first = !otaTotal;
  otaDone = done, otaTotal = total, otaMs = millis();
  const int pct = done * 100 / max<size_t>(total, 1);
  if (!first && pct == lastPct) return;  // a barra só muda de 1 em 1%: o resto do tempo vai para a gravação
  lastPct = pct;
  if (first) lit = config::v.bright * 255 / 100, backlight(lroundf(lit));
  render(first ? 0 : 130, first ? H : 170, Slot{MAIN, 0}, true);
}

void update(bool newFrame) {
  static uint32_t fullMs = 0, animMs = 0, cfg = UINT32_MAX;  // != version: aplica a rotação gravada no primeiro quadro
  static Slot shown{MAIN, 0};
  static bool shownData = false;
  if (otaTotal) {  // durante o OTA a tela é dele; parou sem reiniciar: mostra a falha e volta em 4 s
    if (since(otaMs) > 3000 && since(otaMs) < 3200) render(0, H, Slot{MAIN, 0}, true);
    if (since(otaMs) < 7000) return backlightTick(true);
    otaTotal = otaDone = 0;
  }
  if (newFrame) onFrame();
  bool redraw = newFrame || millis() - fullMs >= 1000;
  if (cfg != config::version) cfg = config::version, lcd.setRotation(config::v.rotation), redraw = true;
  const bool dark = config::v.theme ? config::v.theme == 2 : (bool)(app::frame["dark"] | true);
  if (dark != color::dark) apply(dark), redraw = true;
  const bool data = app::lastFrameMs && millis() - app::lastFrameMs < 60000;
  const Slot slot = data ? pageNow() : Slot{MAIN, 0};
  const bool turned = data != shownData || slot.page != shown.page || slot.sub != shown.sub;
  if (slot.page != MAIN) step(), ui.p5 = app::frame["p5"] | -1, ui.pw = app::frame["pw"] | -1;  // fora da principal: sem mola
  if (turned || redraw) {
    if (turned && shownData && data) fadeOut();
    render(0, H, slot, data);
    fullMs = animMs = millis(), shown = slot, shownData = data;
  } else if (data && slot.page == MAIN && millis() - animMs >= 60) {  // ~15 fps só no que se mexe
    animMs = millis();
    if (step() || bandBlink() || since(ui.bandMs) < 1600) render(42, 190, slot, data);  // anel e coluna da direita
    else if (since(ui.tokMs) < 1000) render(118, 146, slot, data);                    // só o número de tokens
    pulseDot();
  }
  backlightTick(data);
}

bool shot(const String& name, const std::function<void(const uint8_t*, size_t)>& write) {
  if (name == "ota") {  // a tela do OTA a 42%, para conferir o layout sem gravar nada
    otaDone = 196608, otaTotal = 468000, otaMs = millis();
    for (oy = 0; oy < H; oy += BAND) drawOta(), write(reinterpret_cast<const uint8_t*>(band.getBuffer()), min(BAND, H - oy) * W * 2), yield();
    otaDone = otaTotal = 0;
    return true;
  }
  const bool waiting = name == "waiting";
  Slot slot{MAIN, 0};
  if (name == "sessions") slot = {SESSIONS, 0};
  else if (name == "models") slot = {MODELS, 0};
  else if (name.startsWith("model")) slot = {MODEL, (uint8_t)name.substring(5).toInt()};
  else if (name == "projects") slot = {PROJECTS, 0};
  else if (name == "history") slot = {HISTORY, 0};
  else if (name == "orgs") slot = {ORGS, 0};
  else if (name == "ap") {
    for (oy = 0; oy < H; oy += BAND) drawAp(), write(reinterpret_cast<const uint8_t*>(band.getBuffer()), min(BAND, H - oy) * W * 2), yield();
    return true;
  }
  else if (name == "install") {
    for (oy = 0; oy < H; oy += BAND) drawInstall(), write(reinterpret_cast<const uint8_t*>(band.getBuffer()), min(BAND, H - oy) * W * 2), yield();
    return true;
  }
  else if (name != "main" && !waiting) return false;
  if (slot.page == MODEL && slot.sub >= app::frame["mods"].size()) return false;
  for (oy = 0; oy < H; oy += BAND) {
    waiting ? drawWaiting() : draw(slot);
    write(reinterpret_cast<const uint8_t*>(band.getBuffer()), min(BAND, H - oy) * W * 2);
    yield();
  }
  return true;
}

}  // namespace screen
