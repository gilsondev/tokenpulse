#include "screens.h"

#include "../app.h"
#include "../hostlink.h"
#include "../webui.h"
#include "../boards/board.h"
#include "anim.h"
#include "gfx.h"
#include "layout.h"
#include "screens_tall.h"
#include "../power.h"
#include "../settings.h"
#include "../sound.h"
#include "../ble.h"

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
int about = 0;              // dentro dos ajustes: 1 Sobre, 2 Sobre com o reiniciar armado, 3 Conta, 4 página web
int sessScroll = 0;  // a página à mostra: da lista de workspaces ou das sessões do aberto
int sessWs = -1;     // workspace aberto na tela de sessões (-1: a lista)
int wsListPage = 0;  // a página da lista de onde o workspace foi aberto: o voltar cai nela
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
  if (gfx::full()) sessScroll = min(sessScroll, tall::pagesOf(sessWs) - 1);  // a lista encolheu
}

int count() { return gfx::full() ? tall::PAGES : COUNT; }

// A principal do Codex é uma segunda face da página 0, logo depois da do Claude, quando o daemon manda o bloco
// "cx": as outras páginas mantêm os números de sempre (volta automática, comando "page N", alertas).
int codexFace = 0;
bool hasCodex() { return !app::frame["cx"].isNull(); }
bool mainStep(int dir) {
  if (page != 0 || !hasCodex() || codexFace + dir < 0 || codexFace + dir > 1) return false;
  codexFace += dir;
  gfx::slide(-dir);
  anim::reset();
  return true;
}

// O Multica é uma face a mais da página de sessões, depois da última página da lista de workspaces: as outras
// páginas mantêm os números de sempre. Dentro dele, a página, os projetos (view 1) ou a issue de um agente (view 2).
int mcFace = 0, mcView = 0, mcFilter = 0, mcProj = 0;  // mcFilter: o número escolhido; mcProj: o projeto aberto
String mcIssue;  // a issue aberta, pelo identificador: o daemon reordena os agentes a cada leitura
bool hasMulti() { return !app::frame["mc"].isNull(); }
bool mcIssueLive() {
  for (JsonArrayConst a : app::frame["mc"]["a"].as<JsonArrayConst>())
    if (mcIssue == (a[2] | "")) return true;
  return false;
}
bool multiStep(int dir) {
  if (!gfx::full() || page != 1 || sessWs >= 0 || !hasMulti()) return false;
  if (dir > 0 && !mcFace && sessScroll >= tall::pagesOf(-1) - 1) mcFace = 1, mcView = 0;
  else if (dir < 0 && mcFace && mcView) mcView = mcView == 3 ? 1 : 0;  // as issues de um projeto voltam aos projetos
  else if (dir < 0 && mcFace) mcFace = 0;
  else return false;
  gfx::slide(-dir);
  anim::reset();
  return true;
}

// LED de status na cor da faixa da janela de 5h, sempre respirando; acima de 90% respira mais rápido.
// Com sessão pedindo aprovação, pisca vermelho no lugar da faixa.
void updateLed() {
  static const uint8_t RGB[4][3] = {{0, 40, 0}, {40, 30, 0}, {48, 14, 0}, {48, 0, 0}};
  static uint32_t last = 0xFFFFFFFF;
  int p5 = app::frame["p5"] | -1;
  float k = !app::live() || p5 < 0 ? 0 : app::stale() ? 0.3f : 1;
  k *= 0.15f + 0.85f * breath(p5 >= 90 ? 1200 : 3200);
  const uint8_t* c = RGB[level(max(p5, 0))];
  if (app::blocks > 0 && settings::web.alerts & 1) k = breath(800), c = RGB[3];
  uint8_t r = c[0] * k, g = c[1] * k, b = c[2] * k;
  uint32_t packed = r << 16 | g << 8 | b;
  if (packed != last) board::setLed(r, g, b), last = packed;
}

// Falta instalar o daemon: o mesmo QR code das telas grandes, no tamanho da tela pequena.
void drawInstall() {
  const int w = gfx::width(), h = gfx::height();
  fb().fillScreen(BG);
  if (gfx::wide()) {
    const int side = h - s(16), cx = (side + s(8) + w) / 2;
    fb().qrcode(app::START_URL, s(8), s(8), side);
    label("Falta o tokenpulse", cx, h / 2 - s(30), TEXT, middle_center);
    label("no computador.", cx, h / 2 - s(12), MUTED, middle_center);
    label("Escaneie ou abra:", cx, h / 2 + s(4), MUTED, middle_center);
    label(app::START_SHORT, cx, h / 2 + s(24), ACCENT, middle_center);
    return;
  }
  const int side = w - s(16);
  label("Falta o tokenpulse", w / 2, s(14), TEXT, middle_center);
  fb().qrcode(app::START_URL, s(8), s(30), side);
  label("Escaneie ou abra no computador:", w / 2, s(30) + side + s(16), MUTED, middle_center);
  label(app::START_SHORT, w / 2, s(30) + side + s(34), ACCENT, middle_center);
}

// Primeiro uso com o Codex no Mac (o daemon manda "pask" até alguém escolher): a placa pergunta uma vez. Respondida
// aqui, a pergunta some na hora; o próximo frame já vem sem ela.
bool askAnswered = false;
bool askOpen() {
  if (app::frame["pask"].isNull()) return askAnswered = false;
  return !askAnswered;
}
void answerAsk(const char* mode) {
  char cmd[24];
  snprintf(cmd, sizeof cmd, "ACTION prov %s", mode);
  hostlink::action(cmd);
  askAnswered = true;
  gfx::slide(-1);
  anim::reset();
}

// Na tela compacta, sem toque: o aviso e onde escolher; o BOOT deixa no automático.
void drawAsk() {
  const int cx = gfx::width() / 2, cy = gfx::height() / 2;
  const bool both = !strcmp(app::frame["pask"] | "", "both");
  fb().fillScreen(BG);
  label(both ? "Este computador usa" : "Este computador usa", cx, cy - s(52), TEXT, middle_center);
  label(both ? "o Claude e o Codex" : "o Codex", cx, cy - s(38), TEXT, middle_center);
  label("Escolha o que mostrar", cx, cy - s(12), MUTED, middle_center);
  label(webui::online() ? "na página da placa:" : "em outra placa", cx, cy + s(2), MUTED, middle_center);
  if (webui::online()) label((String(webui::host()) + ".local").c_str(), cx, cy + s(16), ACCENT, middle_center);
  label("BOOT: automático", cx, cy + s(44), DIM, middle_center);
}

void drawWaiting() {
  if (hostlink::needsInstall()) return drawInstall();
  int cx = gfx::width() / 2, cy = gfx::height() / 2;
  int start = millis() * 360 / 1200 % 360;
  fb().fillScreen(BG);
  fb().fillArc(cx, cy - s(20), s(12), s(16), 0, 360, TRACK);
  fb().fillArc(cx, cy - s(20), s(12), s(16), start, start + 90, ACCENT);
  label("Aguardando o computador", cx, cy + s(14), MUTED, middle_center);
  label("O daemon tokend.py abre a porta USB", cx, cy + s(30), DIM, middle_center);
}

void drawMain(bool live) {
  const Layout L = layoutFor(gfx::width(), gfx::height());
  // a principal do Codex é esta mesma tela lendo o bloco "cx"
  JsonVariantConst f = tall::codexMain ? app::frame["cx"].as<JsonVariantConst>() : app::frame.as<JsonVariantConst>();
  const int m = s(8), right = gfx::width() - m;
  char buf[48];
  fb().fillScreen(BG);

  String org = tall::codexMain ? String("Codex · ") + (const char*)(f["plan"] | "") : String((const char*)(f["org"] | ""));
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
  label(live ? "Ao vivo" : "Offline", L.statusX + s(10), L.statusY, live ? GREEN : RED);
  const int st = f["st"] | 0;  // a página de status do serviço mostrado: 1 instável, 2 fora do ar
  const bool cxShown = tall::codexMain || tall::codexOnly();
  if (st) snprintf(buf, sizeof buf, st > 1 ? "%s fora" : "%s instável", cxShown ? "Codex" : "Claude");
  else if (tall::codexMain) snprintf(buf, sizeof buf, "Codex %d", (int)(f["n"] | 0));
  else if (tall::codexOnly()) snprintf(buf, sizeof buf, "Codex %d", (int)(f["cxn"] | 0));
  else snprintf(buf, sizeof buf, "CLI %d · Desk %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0));
  label(buf, L.sessX, L.sessY, st > 1 ? RED : st ? AMBER : DIM, L.sessDatum);
  fb().drawFastHLine(m, L.statusY + s(18), gfx::width() - 2 * m, TRACK);

  const bool stale = !tall::codexMain && app::stale();  // o "atualizado há" é da API do Claude
  const int p5 = f["p5"] | -1;
  bool blinkOff = anim::since(anim::alertMs, 1500) < 1 && (millis() - anim::alertMs) / 250 % 2;
  uint16_t ringColor = stale ? DIM : blinkOff ? TEXT : levelColor(p5);
  fb().fillArc(L.ringX, L.ringY, L.ringR - s(9), L.ringR, 0, 360, TRACK);
  if (anim::p5 >= 0.5f) fb().fillArc(L.ringX, L.ringY, L.ringR - s(9), L.ringR, 270, 270 + 3.6f * min(anim::p5, 100.f), ringColor);
  snprintf(buf, sizeof buf, p5 < 0 ? "--" : "%d%%", (int)lroundf(anim::p5));
  big(buf, L.ringX, L.ringY, stale ? DIM : p5 < 0 ? TEXT : levelColor(p5), middle_center);
  const char* s5 = f["s5"] | "";
  const char* warn = stale || p5 < 80 ? nullptr : p5 >= 100 ? "Esgotado" : p5 >= 90 ? "No limite" : "Perto do limite";
  label(warn ? warn : *s5 ? s5 : "Sessão 5h", L.ringX, L.ringLabelY, warn ? levelColor(p5) : *s5 ? ACCENT : MUTED, middle_center);
  if (stale) {
    snprintf(buf, sizeof buf, "Atualizado há %d min", app::dataAge() / 60);
    label(buf, L.ringX, L.resetY, AMBER, middle_center);
  } else {
    label(f["r5"] | "", L.ringX, L.resetY, p5 >= 70 ? levelColor(p5) : MUTED, middle_center);
  }

  const int pw = f["pw"] | -1;
  label("Semana", L.weekX, L.weekY, MUTED);
  snprintf(buf, sizeof buf, pw < 0 ? "--" : "%d%% · %s", pw, (const char*)(f["rw"] | ""));
  label(buf, L.weekX + L.weekW, L.weekY, stale ? DIM : MUTED, top_right);
  bar(L.weekX, L.weekY + s(16), L.weekW, s(6), anim::pw / 100, stale ? DIM : levelColor(pw));

  float glow = anim::since(anim::tokensMs, 900);
  label("Tokens hoje", L.tokX, L.tokY, MUTED);
  big(f["tot"] | "0", L.tokX, L.tokY + s(16), blend(ACCENT, TEXT, glow));
  label("Por minuto", L.rateX, L.tokY, MUTED);
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
  label("Última hora", L.spX, L.spY + s(4), DIM);
  label("Agora", L.spX + L.spW, L.spY + s(4), DIM, top_right);

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
  drawHeader("Modelos · hoje", live);
  JsonArrayConst mods = app::frame["mods"];
  if (mods.size() == 0) label("Sem uso hoje", gfx::width() / 2, gfx::height() / 2, DIM, middle_center);
  int y = s(34);
  for (JsonArrayConst m : mods) {
    const char* name = m[0] | "?";
    int share = m[2];
    label(name, x0, y, TEXT);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)m[1], share);
    label(buf, right, y, MUTED, top_right);
    bar(x0, y + s(15), w, s(6), anim::grow * share / 100, modelColor(name));
    const bool written = *(m[6] | "");  // os modelos do Codex não escrevem cache: vem vazio
    if (wide) {
      if (written) snprintf(buf, sizeof buf, "In %s · out %s · lido %s · escrito %s", (const char*)m[3], (const char*)m[4], (const char*)m[5], (const char*)m[6]);
      else snprintf(buf, sizeof buf, "In %s · out %s · lido %s", (const char*)m[3], (const char*)m[4], (const char*)m[5]);
      label(buf, x0, y + s(24), DIM);
    } else {
      snprintf(buf, sizeof buf, "In %s · out %s", (const char*)m[3], (const char*)m[4]);
      label(buf, x0, y + s(24), DIM);
      if (written) snprintf(buf, sizeof buf, "Lido %s · escrito %s", (const char*)m[5], (const char*)m[6]);
      else snprintf(buf, sizeof buf, "Lido %s", (const char*)m[5]);
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
  drawHeader("Projetos · 1h", live);
  JsonArrayConst projs = app::frame["projs"];
  if (projs.size() == 0) label("Nenhuma sessão na última hora", gfx::width() / 2, gfx::height() / 2, DIM, middle_center);
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
    label(origin, right, y, !strcmp(origin, "Desk") ? ACCENT : !strcmp(origin, "Codex") ? 0x150F : GREEN, top_right);
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
  codexFace = mcFace = mcView = 0;
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
  const char* steps[] = {"Iniciando a tela", "Lendo o sensor de movimento", "Pronto"};
  uint32_t t0 = millis();
  for (float p = 0; p < 1; p = (millis() - t0) / 1400.f) {
    gfx::loading(gfx::BOOT, "tokenpulse", steps[min(2, (int)(p * 3))], p);
    board::setLed(0, 0, 18 * breath(1400));
  }
  gfx::loading(gfx::BOOT, "tokenpulse", "Pronto", 1);
}

uint32_t holdUntil = 0;
void holdFor(uint32_t ms) { holdUntil = millis() + ms; }

uint32_t installUntil = 0;
// Nos próximos 5 s o render desenha a tela do QR: o screenshot de placa sem PSRAM sai no quadro seguinte, e
// segurar o quadro atual (como no splash) não servia para ela.
void installPreview() { installUntil = millis() + 5000; }
uint32_t offlineUntil = 0;
uint32_t shutHeldMs = 0;
void shuttingDown(uint32_t heldMs) {
  shutHeldMs = heldMs;
  if (heldMs) power::wake();
}
void offlinePreview() { offlineUntil = millis() + 5000; }

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
      gfx::text("Calibrar o toque", W / 2, H / 2 - 30, gfx::TITLE, TEXT, middle_center);
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
    draw(i, "Toque no centro do alvo com a caneta", MUTED);
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

// Automação não espera ninguém: o Agent SDK e as runs do Multica (o aviso delas é o do próprio Multica).
bool automated(JsonArrayConst r) { return !strcmp(r[1] | "", "SDK") || !strcmp(r[1] | "", "Multica"); }

// Sessão nova pedindo aprovação: nas telas cheias abre o workspace dela, e o power pulsa o brilho por 20 s.
// A tela não fica presa como no SmallTV: com toque, a pessoa navega para onde quiser.
void checkApproval() {
  static uint32_t seenFrame = 0;
  static int last = -1;  // -1 até o primeiro frame: o boot não dispara alerta
  if (app::lastFrameMs == seenFrame) return;
  seenFrame = app::lastFrameMs;
  int n = 0, ws = -1;
  // a regra do SmallTV: só conta quem espera há até 1 h, e automação do Agent SDK não espera ninguém
  for (JsonArrayConst r : app::frame["sess"]["rows"].as<JsonArrayConst>()) {
    const int mins = r[4] | -1;
    if (strcmp(r[2] | "", "block") || mins < 0 || mins > 60 || automated(r)) continue;
    if (n++ == 0) ws = r[5] | -1;
  }
  app::blocks = n;
  // o som e o destaque em tela cheia ficam com o checkAttention; aqui só o brilho pulsando e, nas telas
  // pequenas, abrir as sessões
  if (last >= 0 && n > last && settings::web.alerts & 1) {
    app::approveMs = millis();
    power::wake();
    if (!gfx::full() && board::PROFILE.speaker && settings::web.alerts & 2 && !app::muted())
      board::play(settings::web.sound, settings::volumePct());
  }
  (void)ws;
  last = n;
}

// Destaque: a sessão que passou a pedir aprovação ocupa a tela inteira por até 60 s, com o som. "Sua vez" (o
// Claude terminou e espera uma mensagem nova) não conta: acontece a cada resposta e cansaria. A regra do
// SmallTV: espera de até 1 h, e o Agent SDK não espera ninguém.
String attnSid;
uint32_t attnMs = 0;
int attentionRow() {
  if (!attnSid.length() || millis() - attnMs > 60000) return -1;
  int i = 0;
  for (JsonArrayConst r : app::frame["sess"]["rows"].as<JsonArrayConst>()) {
    const char* st = r[2] | "";
    if (attnSid == (r[6] | "")) return !strcmp(st, "block") ? i : -1;
    i++;
  }
  return -1;  // a sessão sumiu da lista
}
void dismissAttention() { attnSid = ""; }

void attentionPreview() {  // a primeira sessão que espera você, em destaque, para screenshot
  for (JsonArrayConst r : app::frame["sess"]["rows"].as<JsonArrayConst>())
    if (!strcmp(r[2] | "", "block")) {
      attnSid = (const char*)(r[6] | ""), attnMs = millis();
      const auto& w = settings::web;  // com o som do caso, como um destaque de verdade
      if (board::PROFILE.speaker && w.alerts & 2 && !app::muted() && w.sound < sound::COUNT)
        board::play(w.sound, settings::volumePct(), 0, 2);
      return power::wake();
    }
}

// Aviso do Multica: o daemon manda "mca" por 60 s quando uma issue vai para revisão ou uma run falha; a placa
// mostra uma vez cada um (pelo id), com o som e o destaque das sessões.
int mcaSeen = -1;  // -1 até o primeiro frame: o aviso que já estava lá no boot não toca
uint32_t mcaMs = 0;
bool mcaOpen = false;
void checkMcAlert() {
  const int id = app::frame["mca"]["id"] | 0;
  if (!app::hasData() || id == mcaSeen) return;
  const bool first = mcaSeen < 0;
  mcaSeen = id;
  if (first || !id) return;
  const auto& w = settings::web;
  if (board::PROFILE.speaker && w.alerts & 2 && !app::muted() && w.sound < sound::COUNT) board::play(w.sound, settings::volumePct(), 0, 2);
  if (gfx::full() && w.alerts & 1) mcaOpen = true, mcaMs = millis(), power::wake();
}
void mcAlertPreview() {
  if (!app::frame["mca"].isNull()) mcaOpen = true, mcaMs = millis(), power::wake();
}
bool mcAlertOpen() {
  if (mcaOpen && (millis() - mcaMs > 60000 || app::frame["mca"].isNull())) mcaOpen = false;
  return mcaOpen;
}

// Perguntas do primeiro uso depois dos provedores: o Multica (só com ele no Mac) e o login do Claude (só sem
// ele). Respondida aqui, some na hora; o daemon para de mandar quando a resposta chega.
bool mcAskDone = false, loginDone = false;
bool mcAskOpen() { return !app::frame["mask"].isNull() && !mcAskDone; }
bool loginOpen() { return !app::frame["lask"].isNull() && !loginDone; }

// Versão nova: a placa pergunta uma vez por versão; "Depois" esconde até a próxima.
bool onboarding() { return askOpen() || mcAskOpen() || loginOpen(); }
bool updShown();
// "Depois" pergunta de novo em 24 h.
String updSeen, updTarget;  // updTarget: a versão pedida no "Atualizar agora", até o firmware novo gravar
uint32_t updLaterMs = 0, updatingMs = 0;
bool updOpen() {
  const char* v = app::frame["upd"] | "";
  return *v && (updSeen != v || millis() - updLaterMs > 24UL * 3600 * 1000);
}
bool updShown() { return updOpen() && !onboarding(); }

void checkAttention() {
  static uint32_t seenFrame = 0;
  static String waiting;  // "id,id," de quem já esperava no frame anterior
  static bool first = true;
  if (app::lastFrameMs == seenFrame) return;
  seenFrame = app::lastFrameMs;
  String now;
  int pick = -1, i = 0;
  bool pickBlock = false;
  for (JsonArrayConst r : app::frame["sess"]["rows"].as<JsonArrayConst>()) {
    const char* st = r[2] | "";
    const int mins = r[4] | -1;
    const bool block = !strcmp(st, "block");
    if (block && mins >= 0 && mins <= 60 && !automated(r)) {
      const String sid = r[6] | "";
      now += sid + ",";
      if (!first && waiting.indexOf(sid + ",") < 0 && pick < 0) pick = i, pickBlock = true;
    }
    i++;
  }
  waiting = now, first = false;
  if (pick < 0) return;
  const auto& w = settings::web;
  const uint8_t snd = w.sound;
  Serial.printf("destaque: %s (%s), som %d%s\n", (const char*)(app::frame["sess"]["rows"][pick][6] | ""),
                pickBlock ? "Aprovar" : "Sua vez", snd, app::muted() ? " silenciado" : "");
  if (board::PROFILE.speaker && w.alerts & 2 && !app::muted() && snd < sound::COUNT) board::play(snd, settings::volumePct(), 0, 2);
  if (gfx::full() && w.alerts & 1) {
    attnSid = (const char*)(app::frame["sess"]["rows"][pick][6] | "");
    attnMs = millis();
    power::wake();
  }
}

// Sons dos limites, de ~2 s: a janela de 5h esgotou (lembra de trocar de org), renovou (o uso voltou para perto
// de 0%) e o limite da semana esgotou.
void checkWindowSounds() {
  static uint32_t seenFrame = 0;
  static int last = -1, lastW = -1;  // -1 até o primeiro frame: o boot não toca nada
  if (app::lastFrameMs == seenFrame) return;
  seenFrame = app::lastFrameMs;
  const int p5 = app::frame["p5"] | -1, pw = app::frame["pw"] | -1;
  const auto& w = settings::web;
  const bool can = board::PROFILE.speaker && w.alerts & 2 && !app::muted();
  auto play = [&](uint8_t s) {
    if (can && s < sound::COUNT) board::play(s, settings::volumePct(), 2000);
  };
  if (p5 >= 0) {
    if (last >= 0 && last < 100 && p5 >= 100) play(w.soundOut);
    if (last >= 10 && p5 <= 5) play(w.soundNew);
    last = p5;
  }
  if (pw >= 0) {
    if (lastW >= 0 && lastW < 100 && pw >= 100) play(w.soundWeek);
    lastW = pw;
  }
}

// Sons de estado da placa: bateria fraca (uma vez abaixo de 15% fora do USB), Mac sumiu (uma vez depois de 1 min
// sem dados) e Bluetooth (uma vez ao conectar, duas ao cair).
void checkDeviceSounds() {
  const auto& w = settings::web;
  const bool can = board::PROFILE.speaker && w.alerts & 2 && !app::muted();
  auto play = [&](uint8_t s, uint8_t times = 1) {
    if (can && s < sound::COUNT) board::play(s, settings::volumePct(), 0, times);
  };
  static bool lowSaid = false, goneSaid = false, wasLinked = false;
  board::PowerState ps;
  if (board::powerState(ps) && ps.battery && ps.pct >= 0) {
    tall::chargeEtaMin(ps);  // acompanha o ritmo da carga mesmo com o Sobre fechado
    if (!ps.usb && ps.pct < 15 && !lowSaid) play(w.soundBat), lowSaid = true;
    if (ps.usb || ps.pct >= 20) lowSaid = false;  // carregou: avisa de novo na próxima descida
  }
  const bool gone = app::hasData() && millis() - app::lastFrameMs > 60000;
  if (gone && !goneSaid) play(w.soundMac);
  goneSaid = gone;
  const bool linked = ble::connected();
  if (linked != wasLinked) play(w.soundBle, linked ? 1 : 2);
  wasLinked = linked;
}

// Troca automática (ajuste da página web): passa para a próxima tela ligada depois de N s sem toque nem
// botão. Não tira a pessoa das sessões enquanto houver sessão pedindo aprovação.
void autoTurn() {
  static uint32_t lastMs = 0;
  const auto& w = settings::web;
  if (!w.interval || settingsOpen || (w.alerts & 1 && app::blocks > 0 && page == 1)) return;
  if (millis() - max(power::lastActiveMs(), lastMs) < w.interval * 1000UL) return;
  lastMs = millis();
  // tela cheia: 0 principal, 1 sessões, 2 modelos, 3 histórico; compacta: 0 principal, 1 modelos, 2 projetos
  auto on = [&](int p) {
    if (p == 0) return true;
    if (gfx::full()) return (bool)(w.pages & (p == 1 ? 1 : p == 2 ? 2 : 4));
    return (bool)(w.pages & (p == 1 ? 2 : 1));
  };
  if (mainStep(1)) return;  // a principal do Codex entra na volta logo depois da do Claude
  for (int i = 1; i < count(); i++)
    if (on((page + i) % count())) return go((page + i) % count(), -1);
}

// Conectou pelo WiFi da placa com a página web aberta: mostra o QR do endereço por 20 s e volta para a principal.
void webDone() {
  static bool viaAp = false;
  static uint32_t closeAt = 0;
  if (!settingsOpen || about != 4) {
    viaAp = false, closeAt = 0;
    return;
  }
  if (webui::apOn() && !webui::online()) viaAp = true;
  if (!viaAp || !webui::online()) return;
  if (!closeAt) closeAt = millis() + 20000;
  if (millis() > closeAt) settingsOpen = false, about = 0, viaAp = false, closeAt = 0, gfx::slide(1), anim::reset();
}

void render() {
  checkApproval();
  checkAttention();
  checkMcAlert();
  checkWindowSounds();
  checkDeviceSounds();
  autoTurn();
  webDone();
  if (millis() < holdUntil) return;
  power::update();
  if (shutHeldMs > 500 && gfx::full()) {  // o main desliga a placa aos 2 s
    const float p = min(1.f, (shutHeldMs - 500) / 1500.f);
    return gfx::frame([p] { tall::drawShutdown(p); });
  }
  if (const int r = attentionRow(); r >= 0 && gfx::full() && !settingsOpen && app::hasData())
    return gfx::frame([r] { tall::drawAttention(r, millis() - attnMs); });
  if (gfx::full() && !app::frame["fwup"].isNull()) return updatingMs = 0, gfx::frame(tall::drawFwUp);
  // pediu a atualização: o progresso até a contagem do firmware chegar (2 min no máximo, se algo falhar no caminho)
  if (gfx::full() && updatingMs && millis() - updatingMs < 120000) return gfx::frame([] { tall::drawUpdating(updTarget.c_str()); });
  if (mcAlertOpen() && !settingsOpen) return gfx::frame([] { tall::drawMcAlert(millis() - mcaMs); });
  if (askOpen() && !settingsOpen && app::hasData()) return gfx::frame([] { gfx::full() ? tall::drawProvAsk() : drawAsk(); });
  if (gfx::full() && mcAskOpen() && !settingsOpen && app::hasData()) return gfx::frame(tall::drawMcAsk);
  if (gfx::full() && loginOpen() && !settingsOpen && app::hasData()) return gfx::frame(tall::drawLoginAsk);
  // a versão nova só depois das perguntas do primeiro uso: atualizar no meio delas não faz sentido
  if (gfx::full() && updShown() && !settingsOpen && app::hasData()) return gfx::frame(tall::drawUpdate);
  if (board::PROFILE.oled && power::idle()) return gfx::frame(tall::drawSaver);
  const bool dark = settings::v.theme == 0 ? (bool)(app::frame["dark"] | true) : settings::v.theme == 2;
  if (dark != color::dark) color::apply(dark);
  if (!gfx::ready()) return board::setLed(breath(800) > 0.5f ? 40 : 0, 0, 0);  // sem canvas: LED vermelho piscando
  if (millis() < installUntil) return gfx::frame([] { gfx::full() ? tall::drawInstall() : drawInstall(); });
  // sem dados há mais de 1 min a tela grande troca os números velhos pelo diagnóstico da conexão
  const bool lost = gfx::full() && app::hasData() && millis() - app::lastFrameMs > 60000;
  if (!app::hasData() || lost || millis() < offlineUntil) {
    board::setLed(0, 0, 0);
    return gfx::frame([] { gfx::full() ? tall::drawWaiting() : drawWaiting(); });
  }
  if (!hasCodex()) codexFace = 0;  // o Codex saiu do frame com a face dele na tela
  if (!hasMulti()) mcFace = 0;
  if (mcView == 2 && !mcIssueLive()) mcView = 0;  // a run da issue aberta acabou
  tall::codexMain = page == 0 && codexFace;
  tall::multiShown = page == 1 && mcFace;
  anim::step(tall::codexMain ? app::frame["cx"].as<JsonVariantConst>() : app::frame.as<JsonVariantConst>());
  updateLed();
  const bool live = app::live();
  resolveOpen();
  if (gfx::full()) tall::setGearActive(settingsOpen);
  gfx::frame([live] {
    if (gfx::full() && settingsOpen) {
      if (about == 3) tall::drawAccount(live);
      else if (about == 4) tall::drawWeb(live);
      else if (about) tall::drawAbout(live, about == 2);
      else tall::drawSettings(live);
    } else if (gfx::full()) {
      if (page == 1 && mcFace) tall::drawMulti(live, mcView, mcIssue.c_str(), mcView == 3 ? mcProj : mcFilter);
      else if (page == 1) tall::drawSessions(live, page, sessWs, sessScroll);
      else if (page == 2 && detail >= 0) tall::drawModelDetail(live, page, detail);
      else if (page == 2) tall::drawModels(live, page);
      else if (page == 3) tall::drawHistory(live, page, histSel);
      else tall::drawMain(live, page);
    } else if (page == 1) drawModels(live);
    else if (page == 2) drawProjects(live);
    else drawMain(live);
  });
}

// Sessões: a lista de workspaces ocupa uma página por grupo de cartões, no caminho das outras telas (da
// última vem a próxima). Dentro de um workspace, as páginas são as sessões dele; antes da primeira, a lista.
void closeWs() {
  sessWs = -1, sessWsName = "", sessScroll = wsListPage;
  gfx::slide(1);
  anim::reset();
}
bool sessionStep(int dir) {
  if (!gfx::full() || page != 1 || mcFace) return false;
  const int to = sessScroll + dir;
  if (sessWs >= 0 && to < 0) return closeWs(), true;
  if (to < 0 || to >= tall::pagesOf(sessWs)) return sessWs >= 0;  // dentro do workspace, a última página para
  sessScroll = to;
  gfx::slide(-dir);
  anim::reset();
  return true;
}
void next() {
  if (askOpen() && !gfx::full()) return answerAsk("auto");  // compacta: o BOOT responde "automático"
  if (attentionRow() >= 0) return dismissAttention();  // o botão fecha o destaque
  if (mcaOpen) return void(mcaOpen = false);
  if (!sessionStep(1) && !multiStep(1) && !mainStep(1)) go((page + 1) % count(), -1);
}
void prev() {
  if (sessionStep(-1) || multiStep(-1) || mainStep(-1)) return;
  const int to = (page + count() - 1) % count();
  go(to, 1);
  if (to == 0 && hasCodex()) codexFace = 1;  // voltando das sessões: a principal do Codex, a mais próxima
  if (to == 1 && gfx::full()) sessScroll = tall::pagesOf(-1) - 1, mcFace = hasMulti();  // voltando dos modelos: a última
}

void scroll(int dir) {  // arrastar na vertical rola o Sobre ou passa as páginas de sessões
  if (settingsOpen && (about == 1 || about == 2)) return void(tall::aboutScroll(dir));
  if (settingsOpen && !about) return void(tall::settingsScroll(dir));
  if (page == 1 && mcFace && mcView != 2) return void(tall::multiScroll(dir, mcView));
  sessionStep(dir);
}

void tap(int x, int y) {
  if (const int r = attentionRow(); r >= 0 && gfx::full()) {  // destaque: o botão traz a sessão, o resto fecha
    if (tall::attentionCenter(x, y)) tall::focusRow(r);
    return dismissAttention();
  }
  if (gfx::full() && !app::frame["fwup"].isNull()) return void(hostlink::action("ACTION fwlater"));
  if (gfx::full() && updShown() && app::hasData()) {
    const int b = tall::updateTap(x, y);
    if (b == 1) hostlink::action("ACTION update"), updatingMs = millis(), updTarget = app::frame["upd"] | "";
    if (b) updSeen = app::frame["upd"] | "", updLaterMs = millis();
    return;
  }
  if (gfx::full() && !askOpen() && mcAskOpen() && app::hasData()) {
    const int b = tall::mcAskTap(x, y);
    if (b) hostlink::action(b == 1 ? "ACTION mc 1" : "ACTION mc 0"), mcAskDone = true, gfx::slide(-1), anim::reset();
    return;
  }
  if (gfx::full() && !askOpen() && !mcAskOpen() && loginOpen() && app::hasData()) {
    if (tall::loginSkipAt(x, y)) hostlink::action("ACTION lskip"), loginDone = true, gfx::slide(-1), anim::reset();
    return;
  }
  if (mcaOpen) {  // aviso do Multica: o botão abre a issue no navegador do Mac, o resto fecha
    if (tall::attentionCenter(x, y)) {
      char cmd[40];
      snprintf(cmd, sizeof cmd, "ACTION mcopen %s", (const char*)(app::frame["mca"]["i"] | ""));
      hostlink::action(cmd);
    }
    return void(mcaOpen = false);
  }
  if (askOpen() && gfx::full() && app::hasData()) {  // primeiro uso: só as opções respondem
    if (const char* mode = tall::provAskTap(x, y)) answerAsk(mode);
    return;
  }
  if (!gfx::full()) return next();
  if (page == 0 && !codexFace && !tall::codexOnly() && !settingsOpen && (tall::orgTitleAt(x, y) || tall::suggestionAt(x, y))) {
    gfx::slide(-1);
    settingsOpen = true, about = 3, orgFromHeader = true;
    anim::reset();
    return;
  }
  if (tall::gearAt(x, y)) {  // engrenagem: abre ou fecha os ajustes
    gfx::slide(settingsOpen ? 1 : -1);
    settingsOpen = !settingsOpen, about = 0, orgFromHeader = false, tall::settingsHome();
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
    if (about == 4) {  // a página web: o título volta para o Sobre; sem rede, o resto pede o WiFi ao computador de novo
      if (webui::online() || tall::backAt(x, y)) return void(about = 1);
      return hostlink::action("ACTION wifi");
    }
    if (about) {
      const int b = tall::aboutButtonAt(x, y);
      if (b == 3) {  // página web: sem rede, abre o WiFi da placa para configurá-la
        if (!webui::online()) {
          webui::openAp();
          hostlink::action("ACTION wifi");  // o daemon pede a senha do WiFi do computador e manda pelo cabo
        }
        return void(about = 4);
      }
      if (b == 0) return void(about = 0);
      if (b == 2) {  // calibrar o toque: some o Sobre e volta para os ajustes
        if (!calibrateTouch()) Serial.println("Calibração: desistiu ou toques fora dos alvos");
        about = 0;
        return;
      }
      if (about == 2) ESP.restart();
      about = 2;
      return;
    }
    const int r = tall::settingsTap(x, y);
    if (r == 1) about = 1, tall::aboutTop();
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
  if (page == 1 && mcFace) {
    auto open = [](int view, int dir) { mcView = view, gfx::slide(dir), anim::reset(); };
    if (mcView == 3) {  // issues do projeto: a tocada abre no Mac; o título volta para os projetos
      const int i = tall::mcListTap(x, y);
      if (i == -2) return open(1, 1);
      if (i < 0) return;
      char cmd[40];
      snprintf(cmd, sizeof cmd, "ACTION mcopen %s", (const char*)(app::frame["mc"]["pi"][1][i][0] | ""));
      return void(hostlink::action(cmd));
    }
    if (mcView == 1) {  // projetos: o tocado abre as issues dele (o daemon passa a mandá-las); o resto volta
      const int p = tall::mcProjTap(x, y);
      if (p < 0) return open(0, 1);
      char cmd[24];
      snprintf(cmd, sizeof cmd, "ACTION mcproj %d", p);
      hostlink::action(cmd);
      mcProj = p;
      tall::multiFilter();
      return open(3, -1);
    }
    if (mcView == 2) return open(0, 1);  // o detalhe de um cartão: o toque volta
    const int t = tall::multiTap(x, y);
    if (t == -1) return;  // toque fora de um alvo não troca de página: isso é do arrasto
    if (t <= -10) {  // um número: filtra os cartões na mesma tela
      mcFilter = -10 - t;
      tall::multiFilter();
      return anim::reset();
    }
    if (t == -2) return tall::multiFilter(), open(1, -1);
    mcIssue = app::frame["mc"]["a"][t][2] | "";
    return open(2, -1);
  }
  if (page == 1 && sessWs >= 0) {
    if (tall::backAt(x, y)) return closeWs();  // o título: volta para a lista
    tall::sessionTap(x, y, sessWs, sessScroll);  // toque num cartão: traz a sessão para a frente no Mac
    return;
  }
  if (page == 1) {
    const int w = tall::workspaceAt(x, y, sessScroll);
    if (w >= 0 && tall::directSession(w) >= 0) return void(tall::focusRow(tall::directSession(w)));
    if (w >= 0) {
      gfx::slide(-1);
      wsListPage = sessScroll;
      sessWs = w, sessScroll = 0, sessWsName = app::frame["sess"]["ws"][w][0] | "";
      anim::reset();
      return;
    }
  }
  if (page == 3) {
    const int d = tall::historyBarAt(x, y);
    if (d >= 0) return void(histSel = d);
    // fora das barras, a metade esquerda volta um dia e a direita avança: a barra tem 2 a 4 mm de largura
    const int n = app::frame["hist"].size();
    if (n) histSel = constrain((histSel < 0 ? n - 1 : histSel) + (x < gfx::width() / 2 ? -1 : 1), 0, n - 1);
  }
  if (page == 1 || page == 3) return;  // telas com alvos: o toque que erra não troca de página

  next();
}
void home() { go(0, 1); }

void pullDown() {
  if (!gfx::full() || settingsOpen) return;
  settingsOpen = true, about = 0, orgFromHeader = false;
  tall::settingsHome();
  anim::reset();
}

void homeUp() {
  gfx::slide(gfx::UP);  // sempre sobe, até quando só fecha um painel aberto por cima da principal
  go(0, 0);
}
void goTo(int to) { go(constrain(to, 0, count() - 1), to > page ? -1 : 1); }

}  // namespace screens
