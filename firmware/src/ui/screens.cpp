#include "screens.h"

#include "../app.h"
#include "../hostlink.h"
#include "../boards/board.h"
#include "anim.h"
#include "gfx.h"
#include "layout.h"
#include "screens_tall.h"
#include "../power.h"
#include "../settings.h"

using namespace color;
using gfx::bar;
using gfx::big;
using gfx::fb;
using gfx::label;
using gfx::s;

namespace screens {
namespace {

int page = 0;
int detail = -1;   // modelo aberto na tela de modelos
int histSel = -1;  // dia tocado no histórico (-1: hoje)
bool settingsOpen = false;  // ajustes abertos pela engrenagem, por cima da tela atual
bool orgFromHeader = false;  // a lista de orgs veio do título da tela principal, não dos ajustes
int about = 0;              // dentro dos ajustes: 1 Sobre, 2 Sobre com o reiniciar armado, 3 Conta
int sessScroll = 0;  // grupo de cartões à mostra
int sessWs = -1;     // workspace aberto na tela de sessões (-1: a lista)
// O daemon reordena workspaces e modelos a cada frame: o aberto é lembrado pelo nome e o índice é
// recalculado, senão a tela trocaria de conteúdo sozinha.
String detailName, sessWsName;

int indexOf(JsonArrayConst list, const String& name) {
  int i = 0;
  for (JsonArrayConst item : list) {
    if (name == (item[0] | "")) return i;
    i++;
  }
  return -1;
}

void resolveOpen() {
  if (detailName.length() && (detail = indexOf(app::frame["mods"], detailName)) < 0) detailName = "";
  if (sessWsName.length() && (sessWs = indexOf(app::frame["sess"]["ws"], sessWsName)) < 0) sessWsName = "", sessScroll = 0;
}

int count() { return gfx::full() ? tall::PAGES : COUNT; }

// LED de status na cor da faixa da janela de 5h, sempre respirando; acima de 90% respira mais rápido.
void updateLed() {
  static const uint8_t RGB[4][3] = {{0, 40, 0}, {40, 30, 0}, {48, 14, 0}, {48, 0, 0}};
  static uint32_t last = 0xFFFFFFFF;
  int p5 = app::frame["p5"] | -1;
  float k = !app::live() || p5 < 0 ? 0 : app::stale() ? 0.3f : 1;
  k *= 0.15f + 0.85f * breath(p5 >= 90 ? 1200 : 3200);
  const uint8_t* c = RGB[level(max(p5, 0))];
  uint8_t r = c[0] * k, g = c[1] * k, b = c[2] * k;
  uint32_t packed = r << 16 | g << 8 | b;
  if (packed != last) board::setLed(r, g, b), last = packed;
}

void drawWaiting() {
  int cx = gfx::width() / 2, cy = gfx::height() / 2;
  int start = millis() * 360 / 1200 % 360;
  fb().fillScreen(BG);
  fb().fillArc(cx, cy - s(20), s(12), s(16), 0, 360, TRACK);
  fb().fillArc(cx, cy - s(20), s(12), s(16), start, start + 90, ACCENT);
  label("aguardando o Mac…", cx, cy + s(14), MUTED, middle_center);
  label("o daemon tokend.py abre a porta USB", cx, cy + s(30), DIM, middle_center);
}

void drawMain(bool live) {
  const Layout L = layoutFor(gfx::width(), gfx::height());
  const auto& f = app::frame;
  const int m = s(8), right = gfx::width() - m;
  char buf[48];
  fb().fillScreen(BG);

  String org = f["org"] | "";
  if (org.length() > 14) {
    while (org.length() > 13) gfx::dropLastChar(org);
    org += "…";
  }
  label(org.c_str(), m, s(6), TEXT);
  if (L.statusY == L.clockY) {  // paisagem: modelo na mesma linha da org
    String model = (org.length() ? " · " : "") + String(f["m"] | "Claude");
    label(model.c_str(), m + fb().textWidth(org), s(6), MUTED);
  } else {
    label(f["m"] | "Claude", m, s(20), MUTED);
  }
  label(f["clk"] | "", right, L.clockY, DIM, top_right);
  float pulse = breath(1500);
  if (live) fb().fillCircle(L.statusX + s(3), L.statusY + s(6), s(2) + pulse * s(2), blend(DARK_GREEN, GREEN, pulse));
  else fb().fillCircle(L.statusX + s(3), L.statusY + s(6), s(3), RED);
  label(live ? "ao vivo" : "offline", L.statusX + s(10), L.statusY, live ? GREEN : RED);
  snprintf(buf, sizeof buf, "CLI %d · Desk %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0));
  label(buf, L.sessX, L.sessY, DIM, L.sessDatum);
  fb().drawFastHLine(m, L.statusY + s(18), gfx::width() - 2 * m, TRACK);

  const bool stale = app::stale();
  const int p5 = f["p5"] | -1;
  bool blinkOff = anim::since(anim::alertMs, 1500) < 1 && (millis() - anim::alertMs) / 250 % 2;
  uint16_t ringColor = stale ? DIM : blinkOff ? TEXT : levelColor(p5);
  fb().fillArc(L.ringX, L.ringY, L.ringR - s(9), L.ringR, 0, 360, TRACK);
  if (anim::p5 >= 0.5f) fb().fillArc(L.ringX, L.ringY, L.ringR - s(9), L.ringR, 270, 270 + 3.6f * min(anim::p5, 100.f), ringColor);
  snprintf(buf, sizeof buf, p5 < 0 ? "--" : "%d%%", (int)lroundf(anim::p5));
  big(buf, L.ringX, L.ringY, stale ? DIM : p5 < 0 ? TEXT : levelColor(p5), middle_center);
  label("sessão 5h", L.ringX, L.ringLabelY, MUTED, middle_center);
  if (stale) {
    snprintf(buf, sizeof buf, "atualizado há %d min", app::dataAge() / 60);
    label(buf, L.ringX, L.resetY, AMBER, middle_center);
  } else {
    label(f["r5"] | "", L.ringX, L.resetY, p5 >= 70 ? levelColor(p5) : MUTED, middle_center);
  }

  const int pw = f["pw"] | -1;
  label("semana", L.weekX, L.weekY, MUTED);
  snprintf(buf, sizeof buf, pw < 0 ? "--" : "%d%% · %s", pw, (const char*)(f["rw"] | ""));
  label(buf, L.weekX + L.weekW, L.weekY, stale ? DIM : MUTED, top_right);
  bar(L.weekX, L.weekY + s(16), L.weekW, s(6), anim::pw / 100, stale ? DIM : levelColor(pw));

  float glow = anim::since(anim::tokensMs, 900);
  label("tokens hoje", L.tokX, L.tokY, MUTED);
  big(f["tot"] | "0", L.tokX, L.tokY + s(16), blend(ACCENT, TEXT, glow));
  label("ritmo/min", L.rateX, L.tokY, MUTED);
  big(f["rate"] | "0", L.rateX, L.tokY + s(16));
  label(f["sub"] | "", L.tokX, L.subY, DIM);

  JsonArrayConst sp = f["sp"];
  uint32_t peak = 1;
  for (uint32_t v : sp) peak = max(peak, v);
  const int n = sp.size();
  auto yAt = [&](int i) { return L.spY - (int)((uint32_t)sp[i] * L.spH / peak); };
  for (int i = 1; i < n; i++) fb().drawLine(L.spX + (i - 1) * L.spW / (n - 1), yAt(i - 1), L.spX + i * L.spW / (n - 1), yAt(i), ACCENT);
  if (glow < 1 && n) fb().fillCircle(L.spX + L.spW, yAt(n - 1), s(2) + (1 - glow) * s(3), ACCENT);
  fb().drawFastHLine(L.spX, L.spY + 1, L.spW, TRACK);
  label("-60 min", L.spX, L.spY + s(4), DIM);
  label("agora", L.spX + L.spW, L.spY + s(4), DIM, top_right);

}

void drawHeader(const char* title, bool live) {
  const int m = s(8), right = gfx::width() - m, cy = s(12), r = s(3);
  label(title, m, s(6), MUTED);
  for (int i = 0; i < COUNT; i++) {
    int x = right - r - (COUNT - 1 - i) * s(10);
    if (i == page) fb().fillCircle(x, cy, r, TEXT);
    else fb().drawCircle(x, cy, r, DIM);
  }
  fb().fillCircle(right - r - COUNT * s(10) - s(4), cy, r, live ? GREEN : RED);
  fb().drawFastHLine(m, s(24), gfx::width() - 2 * m, TRACK);
}

uint16_t modelColor(const char* name) {
  if (strstr(name, "Opus")) return ACCENT;
  if (strstr(name, "Sonnet")) return GREEN;
  return AMBER;
}

void drawModels(bool live) {
  const bool wide = gfx::wide();
  const int x0 = s(8), right = gfx::width() - x0, w = gfx::width() - 2 * x0, rowH = s(wide ? 46 : 62);
  char buf[64];
  fb().fillScreen(BG);
  drawHeader("modelos · hoje", live);
  JsonArrayConst mods = app::frame["mods"];
  if (mods.size() == 0) label("sem uso hoje", gfx::width() / 2, gfx::height() / 2, DIM, middle_center);
  int y = s(34);
  for (JsonArrayConst m : mods) {
    const char* name = m[0] | "?";
    int share = m[2];
    label(name, x0, y, TEXT);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)m[1], share);
    label(buf, right, y, MUTED, top_right);
    bar(x0, y + s(15), w, s(6), anim::grow * share / 100, modelColor(name));
    if (wide) {
      snprintf(buf, sizeof buf, "in %s · out %s · lido %s · escrito %s", (const char*)m[3], (const char*)m[4], (const char*)m[5], (const char*)m[6]);
      label(buf, x0, y + s(24), DIM);
    } else {
      snprintf(buf, sizeof buf, "in %s · out %s", (const char*)m[3], (const char*)m[4]);
      label(buf, x0, y + s(24), DIM);
      snprintf(buf, sizeof buf, "lido %s · escrito %s", (const char*)m[5], (const char*)m[6]);
      label(buf, x0, y + s(38), DIM);
    }
    y += rowH;
  }
}

void drawProjects(bool live) {
  const int x0 = s(8), right = gfx::width() - x0, maxChars = (gfx::width() - s(52)) / s(6);
  const int rowH = s(32), rows = min(8, (gfx::height() - rowH) / rowH);
  char buf[64];
  fb().fillScreen(BG);
  drawHeader("projetos · 1h", live);
  JsonArrayConst projs = app::frame["projs"];
  if (projs.size() == 0) label("nenhuma sessão na última hora", gfx::width() / 2, gfx::height() / 2, DIM, middle_center);
  int y = rowH, i = 0;
  for (JsonArrayConst p : projs) {
    if (i++ == rows) break;
    String name = p[0] | "?";
    if ((int)name.length() > maxChars) {
      while ((int)name.length() > maxChars - 1) gfx::dropLastChar(name);
      name += "…";
    }
    const char* origin = p[1] | "CLI";
    int mins = p[3];
    label(name.c_str(), x0, y, TEXT);
    label(origin, right, y, strcmp(origin, "Desk") ? GREEN : ACCENT, top_right);
    if (mins == 0) snprintf(buf, sizeof buf, "%s · agora", (const char*)p[2]);
    else snprintf(buf, sizeof buf, "%s · há %d min", (const char*)p[2], mins);
    label(buf, x0, y + s(14), DIM);
    y += rowH;
  }
  if ((int)projs.size() > rows) {
    snprintf(buf, sizeof buf, "+%d", (int)projs.size() - rows);
    label(buf, right, gfx::height() - s(14), DIM, top_right);
  }
}

void go(int to, int dir) {
  detail = histSel = -1;
  detailName = sessWsName = "";
  about = 0;
  settingsOpen = orgFromHeader = false;
  sessScroll = 0;
  sessWs = -1;
  if (to == page) return;
  page = to;
  if (dir) gfx::slide(dir);
  anim::reset();
}

}  // namespace

void bootSplash() {
  const char* steps[] = {"iniciando a tela", "lendo o sensor de movimento", "pronto"};
  uint32_t t0 = millis();
  for (float p = 0; p < 1; p = (millis() - t0) / 1400.f) {
    gfx::loading(gfx::BOOT, "tokenpulse", steps[min(2, (int)(p * 3))], p);
    board::setLed(0, 0, 18 * breath(1400));
  }
  gfx::loading(gfx::BOOT, "tokenpulse", "pronto", 1);
}

uint32_t holdUntil = 0;
void holdFor(uint32_t ms) { holdUntil = millis() + ms; }

// Calibração do touch resistivo: um alvo perto de cada canto, a leitura crua média de cada toque e, dos
// quatro, a leitura nas bordas da tela. Sem toque em 60 s desiste e fica a calibração que havia.
bool calibrateNative();

bool calibrateTouch() {
  // a calibração é do painel, em retrato: deitada, gira para calibrar e volta no fim
  const uint8_t rot = gfx::rotation();
  if (rot) gfx::setRotation(0);
  const bool ok = calibrateNative();
  if (rot) gfx::setRotation(rot);
  return ok;
}

bool calibrateNative() {
  const int W = gfx::width(), H = gfx::height(), in = 24;
  // alvos: sup-esq, sup-dir, inf-esq, inf-dir
  const int nx[4] = {in, W - 1 - in, in, W - 1 - in}, ny[4] = {in, in, H - 1 - in, H - 1 - in};
  long rx[4], ry[4];
  auto draw = [&](int i, const char* status, uint16_t c) {
    const int x = nx[i], y = ny[i];
    gfx::frame([&] {
      gfx::clear();
      gfx::text("calibrar o toque", W / 2, H / 2 - 30, gfx::TITLE, TEXT, middle_center);
      gfx::text(status, W / 2, H / 2, gfx::SMALL, c, middle_center);
      char step[16];
      snprintf(step, sizeof step, "%d de 4", i + 1);
      gfx::text(step, W / 2, H / 2 + 22, gfx::SMALL, DIM, middle_center);
      fb().drawCircle(x, y, 10, ACCENT);
      fb().drawFastHLine(x - 16, y, 33, ACCENT);
      fb().drawFastVLine(x, y - 16, 33, ACCENT);
      fb().fillCircle(x, y, 3, ACCENT);
    });
  };
  for (int i = 0; i < 4; i++) {
    draw(i, "toque no centro do alvo com a caneta", MUTED);
    int16_t x, y;
    uint32_t t0 = millis(), quiet = millis();
    while (millis() - quiet < 300) {  // espera soltar o toque anterior
      if (board::touchRaw(x, y)) quiet = millis();
      hostlink::poll();
    }
    long sx = 0, sy = 0, n = 0;
    uint32_t lastHit = 0;
    while (!n || millis() - lastHit < 150) {  // amostra enquanto a caneta está no alvo
      if (millis() - t0 > 60000) return false;
      hostlink::poll();
      if (!board::touchRaw(x, y)) continue;
      sx += x, sy += y, n++, lastHit = millis();
      delay(5);
    }
    if (n < 4) {  // encostou e tirou rápido demais: não conta, pede de novo
      i--;
      continue;
    }
    rx[i] = sx / n, ry[i] = sy / n;
    draw(i, "ok", GREEN);
    delay(250);
  }
  // o eixo cru que mais anda da esquerda para a direita é o x da tela
  const long dx = labs(rx[1] - rx[0]) + labs(rx[3] - rx[2]), dy = labs(ry[1] - ry[0]) + labs(ry[3] - ry[2]);
  const bool swap = dy > dx;
  long a[4], b[4];
  for (int i = 0; i < 4; i++) a[i] = swap ? ry[i] : rx[i], b[i] = swap ? rx[i] : ry[i];
  const float left = (a[0] + a[2]) / 2.f, right = (a[1] + a[3]) / 2.f, top = (b[0] + b[1]) / 2.f, bottom = (b[2] + b[3]) / 2.f;
  if (fabsf(right - left) < 500 || fabsf(bottom - top) < 500) return false;  // toques fora dos alvos
  const float kx = (right - left) / (nx[1] - nx[0]), ky = (bottom - top) / (ny[2] - ny[0]);  // leitura crua por pixel
  settings::cal = {(int16_t)lroundf(left - kx * nx[0]), (int16_t)lroundf(left + kx * (W - nx[0])),
                   (int16_t)lroundf(top - ky * ny[0]), (int16_t)lroundf(top + ky * (H - ny[0])), (uint8_t)swap, 1};
  settings::saveCal();
  Serial.printf("calibração: x %d..%d y %d..%d troca %d\n", settings::cal.x0, settings::cal.x1, settings::cal.y0,
                settings::cal.y1, swap);
  return true;
}

void render() {
  if (millis() < holdUntil) return;
  power::update();
  const bool dark = settings::v.theme == 0 ? (bool)(app::frame["dark"] | true) : settings::v.theme == 2;
  if (dark != color::dark) color::apply(dark);
  if (!gfx::ready()) return board::setLed(breath(800) > 0.5f ? 40 : 0, 0, 0);  // sem canvas: LED vermelho piscando
  // sem dados há mais de 1 min a tela grande troca os números velhos pelo diagnóstico da conexão
  const bool lost = gfx::full() && app::hasData() && millis() - app::lastFrameMs > 60000;
  if (!app::hasData() || lost) {
    board::setLed(0, 0, 0);
    return gfx::frame([] { gfx::full() ? tall::drawWaiting() : drawWaiting(); });
  }
  anim::step();
  updateLed();
  const bool live = app::live();
  resolveOpen();
  if (gfx::full()) tall::setGearActive(settingsOpen);
  gfx::frame([live] {
    if (gfx::full() && settingsOpen) {
      if (about == 3) tall::drawAccount(live);
      else if (about) tall::drawAbout(live, about == 2);
      else tall::drawSettings(live);
    } else if (gfx::full()) {
      if (page == 1) tall::drawSessions(live, page, sessWs, sessScroll);
      else if (page == 2 && detail >= 0) tall::drawModelDetail(live, page, detail);
      else if (page == 2) tall::drawModels(live, page);
      else if (page == 3) tall::drawHistory(live, page, histSel);
      else tall::drawMain(live, page);
    } else if (page == 1) drawModels(live);
    else if (page == 2) drawProjects(live);
    else drawMain(live);
  });
}

void next() { go((page + 1) % count(), -1); }
void prev() { go((page + count() - 1) % count(), 1); }

void scroll(int dir) {
  if (!gfx::full() || page != 1) return;
  int items = app::frame["sess"]["ws"].size(), per = tall::wsPerPage();
  if (sessWs >= 0) {
    items = 0, per = 6;
    for (JsonArrayConst r : app::frame["sess"]["rows"].as<JsonArrayConst>()) items += (int)r[5] == sessWs;
  }
  const int groups = max(1, (items + per - 1) / per);
  const int to = constrain(sessScroll + dir, 0, groups - 1);
  if (to == sessScroll) return;
  sessScroll = to;
  anim::reset();
}

void tap(int x, int y) {
  if (!gfx::full()) return next();
  if (page == 0 && !settingsOpen && (tall::orgTitleAt(x, y) || tall::suggestionAt(x, y))) {  // lista de orgs
    gfx::slide(-1);
    settingsOpen = true, about = 3, orgFromHeader = true;
    anim::reset();
    return;
  }
  if (tall::gearAt(x, y)) {  // engrenagem: abre ou fecha os ajustes
    gfx::slide(settingsOpen ? 1 : -1);
    settingsOpen = !settingsOpen, about = 0, orgFromHeader = false;
    anim::reset();
    return;
  }
  if (settingsOpen) {
    if (about == 3) {
      if (!tall::accountTap(x, y)) {
        about = 0;
        if (orgFromHeader) gfx::slide(1), settingsOpen = false;  // veio do título: fecha direto na tela
        orgFromHeader = false;
      }
      return;
    }
    if (about) {
      const int b = tall::aboutButtonAt(x, y);
      if (b == 0) return void(about = 0);
      if (b == 2) {  // calibrar o toque: some o Sobre e volta para os ajustes
        if (!calibrateTouch()) Serial.println("calibração: desistiu ou toques fora dos alvos");
        about = 0;
        return;
      }
      if (about == 2) ESP.restart();
      about = 2;
      return;
    }
    const int r = tall::settingsTap(x, y);
    if (r == 1) about = 1;
    if (r == 2) about = 3;
    return;
  }
  if (page == 2) {
    const int m = detail >= 0 ? -1 : tall::modelAt(x, y);
    gfx::slide(m >= 0 ? -1 : 1);
    detail = m;
    detailName = m >= 0 ? String(app::frame["mods"][m][0] | "") : "";
    anim::reset();
    return;
  }
  if (page == 1) {
    if (sessWs >= 0 && y < 56) {  // título: volta para a lista de workspaces
      gfx::slide(1);
      sessWs = -1, sessScroll = 0, sessWsName = "";
      anim::reset();
      return;
    }
    const int w = sessWs < 0 ? tall::workspaceAt(x, y, sessScroll) : -1;
    if (w >= 0) {
      gfx::slide(-1);
      sessWs = w, sessScroll = 0, sessWsName = app::frame["sess"]["ws"][w][0] | "";
      anim::reset();
      return;
    }
    if (sessWs >= 0) {  // toque num cartão: traz a sessão para a frente no Mac
      tall::sessionTap(x, y, sessWs, sessScroll);
      return;
    }
  }
  if (page == 3) {
    const int d = tall::historyBarAt(x, y);
    if (d >= 0) return void(histSel = d);
  }

  next();
}
void home() { go(0, 1); }

void homeUp() {
  gfx::slide(gfx::UP);  // sempre sobe, até quando só fecha um painel aberto por cima da principal
  go(0, 0);
}
void goTo(int to) { go(constrain(to, 0, count() - 1), to > page ? -1 : 1); }

}  // namespace screens
