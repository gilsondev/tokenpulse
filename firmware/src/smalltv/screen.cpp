#include "screen.h"

#include <LovyanGFX.hpp>
#include <SPI.h>

#include "../ui/fonts/inter_hero_m.h"
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
Vlw LABEL{inter_label_m}, NUM{inter_num_m}, HERO{inter_hero_m};

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
  band.fillCircle(W - 16 - textWidth(st, LABEL), 30 - oy, 3, live ? GREEN : RED);
  band.drawFastHLine(8, 42 - oy, W - 16, TRACK);

  // anel da janela de 5 h
  const int p5 = f["p5"] | -1, cx = 64, cy = 100, r = 54;
  const uint16_t ring = stale ? DIM : levelColor(p5);
  band.fillArc(cx, cy - oy, r - 9, r, 0, 360, TRACK);
  if (p5 > 0) band.fillArc(cx, cy - oy, r - 9, r, 270, 270 + 3.6f * min(p5, 100), ring);
  snprintf(buf, sizeof buf, p5 < 0 ? "--" : "%d%%", p5);
  text(buf, cx, cy - 22, p5 >= 100 ? NUM : HERO, p5 < 0 ? TEXT : ring, top_center);
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
  if (pw > 0) band.fillRoundRect(x, 68 - oy, max(6, w * min(pw, 100) / 100), 6, 3, stale ? DIM : levelColor(pw));
  text(f["rw"] | "", x, 80, LABEL, DIM);
  text("tokens hoje", x, 104, LABEL, MUTED);
  text(f["tot"] | "0", x, 120, NUM, ACCENT);
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
  JsonArrayConst n = app::frame["sesn"];
  const int act = n[0] | 0, wait = n[1] | 0, idle = n[2] | 0;
  snprintf(buf, sizeof buf, "%d %s · %d %s · %d %s", act, act == 1 ? "ativa" : "ativas", wait,
           wait == 1 ? "espera" : "esperam", idle, idle == 1 ? "parada" : "paradas");
  text(buf, 8, 32, LABEL, (n[1] | 0) ? ORANGE : MUTED);
  int y = 54;
  for (JsonArrayConst s : app::frame["ses"].as<JsonArrayConst>()) {  // até 8
    const char* st = s[1] | "";
    const bool work = !strcmp(st, "work"), block = !strcmp(st, "block"), turn = !strcmp(st, "turn");
    const uint16_t c = work ? GREEN : block ? RED : turn ? ORANGE : DIM;
    ago(when, sizeof when, s[2] | -1);
    if (work) snprintf(buf, sizeof buf, "trabalhando");
    else if (block || turn) snprintf(buf, sizeof buf, "%s · %s", block ? "aprovar" : "sua vez", when);
    else snprintf(buf, sizeof buf, "%s", when);
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

// Sem toque nem botão, as telas passam sozinhas a cada config::v.interval s. Tela sem conteúdo
// (nenhum projeto na última hora) fica de fora da volta.
enum Page : uint8_t { MAIN, SESSIONS, MODELS, MODEL, PROJECTS };
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
  return n;
}

Slot pageNow() {
  static Slot cur = {MAIN, 0};
  static uint32_t sinceMs = 0;
  Slot list[8];
  const int n = slots(list);
  int at = -1;
  for (int i = 0; i < n; i++)
    if (list[i].page == cur.page && list[i].sub == cur.sub) at = i;
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
  else drawMain();
}

void drawWaiting() {
  char buf[64];
  band.fillScreen(BG);
  text("sem dados do Mac", W / 2, 40, NUM, TEXT, top_center);
  if (app::lastFrameMs) {
    snprintf(buf, sizeof buf, "último dado há %lu min", (millis() - app::lastFrameMs) / 60000);
    text(buf, W / 2, 70, LABEL, MUTED, top_center);
  }
  const bool wifi = net::online();
  const bool daemon = wifi && net::lastDaemonMs() && millis() - net::lastDaemonMs() < 15000;
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

bool night() {
  const char* clk = app::frame["clk"] | "";  // horário de Brasília vindo do Mac
  if (strlen(clk) < 2) return false;
  const int h = atoi(clk);
  return h >= 22 || h < 7;
}

uint8_t brightness(bool data) {
  const int full = config::v.bright * 255 / 100;
  return data && config::v.night && night() ? max(6, full * 8 / 100) : full;
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

void update(bool newFrame) {
  static uint32_t lastMs = 0, cfg = UINT32_MAX;  // != version: aplica a rotação gravada no primeiro quadro
  const bool changed = cfg != config::version;
  if (!newFrame && !changed && millis() - lastMs < 1000) return;
  lastMs = millis();
  if (changed) cfg = config::version, lcd.setRotation(config::v.rotation);
  const bool dark = config::v.theme ? config::v.theme == 2 : (bool)(app::frame["dark"] | true);
  if (dark != color::dark) apply(dark);
  const bool data = app::lastFrameMs && millis() - app::lastFrameMs < 60000;
  const Slot slot = data ? pageNow() : Slot{MAIN, 0};
  for (oy = 0; oy < H; oy += BAND) {
    data ? draw(slot) : drawWaiting();
    band.pushSprite(0, oy);
    yield();
  }
  backlight(brightness(data));
}

bool shot(const String& name, const std::function<void(const uint8_t*, size_t)>& write) {
  const bool waiting = name == "waiting";
  Slot slot{MAIN, 0};
  if (name == "sessions") slot = {SESSIONS, 0};
  else if (name == "models") slot = {MODELS, 0};
  else if (name.startsWith("model")) slot = {MODEL, (uint8_t)name.substring(5).toInt()};
  else if (name == "projects") slot = {PROJECTS, 0};
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
