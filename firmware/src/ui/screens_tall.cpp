#include "screens_tall.h"

#include "../app.h"
#include "../hostlink.h"
#include "../settings.h"
#include "anim.h"
#include "gfx.h"
#include "mascot.h"
#include "screens.h"

using namespace color;
using gfx::fb;
using gfx::text;

namespace screens::tall {
namespace {

constexpr int M = 16;  // margem; o conteúdo tem 288 px de largura

int W() { return gfx::width(); }

// Pontos de página no rodapé: todos com 6 px de altura no mesmo eixo; a página atual é uma pílula.
void dots(int page) {
  constexpr int H = 6, DOT = 6, PILL = 18, GAP = 8;
  const int total = PILL + (PAGES - 1) * (DOT + GAP), y = gfx::height() - 14 - H / 2;
  int x = W() / 2 - total / 2;
  for (int i = 0; i < PAGES; i++) {
    const int w = i == page ? PILL : DOT;
    fb().fillSmoothRoundRect(x, y, w, H, H / 2, i == page ? TEXT : DIM);
    x += w + GAP;
  }
}

// Etiqueta de origem alinhada à direita em `right`: fundo tingido, texto na cor cheia.
void badge(const char* label, int right, int top, uint16_t c, uint16_t base = BG) {
  constexpr int H = 22, PADX = 10;
  const int w = gfx::textWidth(label, gfx::SMALL) + 2 * PADX;
  const uint16_t bg = blend(base, c, 0.22f);
  fb().fillSmoothRoundRect(right - w, top, w, H, H / 2, bg);
  text(label, right - w / 2, top + H / 2, gfx::SMALL, c, middle_center, bg);
}

// corta o texto com reticências até caber em `maxW`
String fit(const char* s, gfx::Font f, int maxW) {
  String t = s ? s : "";
  if (gfx::textWidth(t.c_str(), f) <= maxW) return t;
  while (t.length() > 1 && gfx::textWidth((t + "…").c_str(), f) > maxW) gfx::dropLastChar(t);
  return t + "…";
}

// Quebra nas palavras em até duas linhas: devolve a primeira e põe o resto em `l2` (sem cortar).
String wrap2(const char* s, gfx::Font f, int maxW, String& l2) {
  String l1 = s ? s : "";
  l2 = "";
  while (gfx::textWidth(l1.c_str(), f) > maxW && l1.lastIndexOf(' ') > 0) {
    const int cut = l1.lastIndexOf(' ');
    l2 = l1.substring(cut + 1) + (l2.length() ? " " + l2 : "");
    l1 = l1.substring(0, cut);
  }
  return l1;
}

// Ícone de passo de um guia: 0 feito (✓), 1 agora (anel girando), 2 depois (ponto).
void stepIcon(int x, int y, int state) {
  if (state == 0) {
    fb().fillSmoothCircle(x, y, 9, blend(BG, GREEN, 0.25f));
    fb().drawWideLine(x - 4, y, x - 1, y + 3, 1.1f, GREEN);
    fb().drawWideLine(x - 1, y + 3, x + 5, y - 4, 1.1f, GREEN);
  } else if (state == 1) {
    gfx::ring(x, y, 9, 3, 0.25f + 0.2f * breath(1200), AMBER, TRACK);
  } else {
    fb().fillSmoothCircle(x, y, 3, DIM);
  }
}

// Engrenagem dos ajustes, ao lado do relógio: disco, 8 dentes e o furo no meio.
bool gearActive = false;

void gear(int cx, int cy, int r, uint16_t c) {
  for (int k = 0; k < 8; k++) {
    const float a = k * TAU / 8;
    fb().drawWideLine(cx + cosf(a) * r * 0.55f, cy + sinf(a) * r * 0.55f, cx + cosf(a) * r * 0.92f, cy + sinf(a) * r * 0.92f,
                      r * 0.17f, c);
  }
  fb().fillSmoothCircle(cx, cy, r * 0.68f, c);
  fb().fillSmoothCircle(cx, cy, r * 0.28f, BG);
}

// Cabeçalho: org e relógio; embaixo modelo, ponto de status e sessões abertas.
void header(bool live, const char* title, bool dropdown = false) {
  const auto& f = app::frame;
  const char* clk = f["clk"] | "";
  const int clkLeft = W() - M - 30 - gfx::textWidth(clk, gfx::TITLE);
  const String t = fit(title, gfx::LABEL, clkLeft - M - 10 - (dropdown ? 18 : 0));
  text(t.c_str(), M, 10, gfx::LABEL, TEXT);
  if (dropdown) {  // a org abre a lista de orgs: um "v" pequeno depois do nome
    const int x = M + gfx::textWidth(t.c_str(), gfx::LABEL) + 8, y = 19;
    fb().drawWideLine(x, y - 2, x + 4, y + 2, 1.0f, MUTED);
    fb().drawWideLine(x + 4, y + 2, x + 8, y - 2, 1.0f, MUTED);
  }
  text(clk, W() - M - 30, 8, gfx::TITLE, TEXT, top_right);
  gear(W() - M - 10, 19, 10, gearActive ? ACCENT : MUTED);
  text(f["m"] | "Claude", M, 32, gfx::SMALL, DIM);
  char buf[32];
  if (live) snprintf(buf, sizeof buf, "CLI %d · Desk %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0));
  else snprintf(buf, sizeof buf, "offline há %lu min", (millis() - app::lastFrameMs) / 60000);
  text(buf, W() - M, 32, gfx::SMALL, live ? DIM : RED, top_right);
  int w = gfx::textWidth(buf, gfx::SMALL);
  fb().fillSmoothCircle(W() - M - w - 8, 39, 3, live ? GREEN : RED);
}

// Dígitos em células de largura fixa: o número não treme enquanto conta.
void heroNumber(const char* s, int cx, int cy, uint16_t c) {
  const int cell = gfx::textWidth("0", gfx::HERO);
  int total = 0;
  for (const char* p = s; *p; p++) total += isdigit(*p) ? cell : gfx::textWidth(String(*p).c_str(), gfx::HERO);
  int x = cx - total / 2;
  for (const char* p = s; *p; p++) {
    char ch[2] = {*p, 0};
    int w = isdigit(*p) ? cell : gfx::textWidth(ch, gfx::HERO);
    text(ch, x + w / 2, cy, gfx::HERO, c, middle_center);
    x += w;
  }
}

// "194,7M" / "720k" / "98" -> número
float parseTokens(const char* s) {
  String t = s;
  t.replace(',', '.');
  float v = t.toFloat();
  if (t.endsWith("M")) v *= 1e6f;
  else if (t.endsWith("k")) v *= 1e3f;
  return v;
}

String fmtTokens(uint32_t v) {
  char b[16];
  if (v >= 1000000) snprintf(b, sizeof b, "%.1fM", v / 1e6f);
  else if (v >= 1000) snprintf(b, sizeof b, "%luk", v / 1000);
  else snprintf(b, sizeof b, "%lu", v);
  String s = b;
  s.replace('.', ',');
  return s;
}

}  // namespace

void drawMain(bool live, int page) {
  const auto& f = app::frame;
  const bool stale = app::stale() || !live;
  const int p5 = f["p5"] | -1, pw = f["pw"] | -1;
  char buf[48];
  gfx::clear();
  header(live, f["org"] | "Claude", true);

  // Anel da janela de 5h: o herói da tela.
  const int cx = W() / 2, cy = 160, r = 100;
  const float fade = anim::since(anim::alertMs, 400);  // mudou de faixa: a cor anterior vira a nova
  uint16_t lc = stale ? DIM : fade < 1 ? blend(anim::alertFrom, levelColor(p5), fade) : levelColor(p5);
  static gfx::RegionCache ringCache;
  const float frac = min(anim::p5, 100.f) / 100;
  const uint32_t ringKey = gfx::hashOf({(uint32_t)lroundf(frac * 2000), lc, TRACK, BG});
  if (!ringCache.restore(ringKey, cx - r - 2, cy - r - 2, 2 * r + 5, 2 * r + 5)) {
    gfx::ring(cx, cy, r, 15, frac, lc, TRACK);
    ringCache.store(ringKey);
  }
  if (!stale) gfx::ringHalo(cx, cy, r, 15, frac, lc, anim::halo());
  // com 0% ou "--" o daemon manda s5 ("janela nova · 17h"): sem ele, o 0% do começo da janela parece erro
  const char* s5 = f["s5"] | "";
  // texto dentro do anel: no máximo a corda do furo (raio r - 15) na borda do texto mais longe do centro
  auto inner = [&](int dy) { return (int)(2 * sqrtf(max(0.f, (float)(r - 17) * (r - 17) - dy * dy))); };
  // se não couber, desce para a fonte menor; cortar com reticência só como último recurso
  auto ringText = [&](const char* t, int dy, gfx::Font f, uint16_t c) {
    const int maxW = inner(abs(dy) + 6);
    if (f != gfx::SMALL && gfx::textWidth(t, f) > maxW) f = gfx::SMALL;
    text(fit(t, f, maxW).c_str(), cx, cy + dy, f, c, middle_center);
  };
  ringText(*s5 ? s5 : "sessão 5h", -46, gfx::SMALL, *s5 ? ACCENT : DIM);
  if (p5 < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d%%", (int)lroundf(anim::p5));
  heroNumber(buf, cx, cy, stale ? DIM : p5 < 0 ? TEXT : lc);  // o número na cor da faixa, como o anel
  if (app::stale()) snprintf(buf, sizeof buf, "atualizado há %d min", app::dataAge() / 60);
  ringText(app::stale() ? buf : (const char*)(f["r5"] | ""), 44, gfx::LABEL, app::stale() ? AMBER : MUTED);
  if (!stale && p5 >= 80) text(p5 >= 90 ? "no limite" : "perto do limite", cx, cy + r + 14, gfx::SMALL, levelColor(p5), middle_center);

  // Semana, com o marcador do ritmo linear: barra à frente do marcador = uso adiantado.
  const int wy = 290, bw = W() - 2 * M;
  text("semana", M, wy, gfx::LABEL, MUTED);
  if (pw < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d%% · reset %s", pw, (const char*)(f["rw"] | ""));
  text(buf, W() - M, wy, gfx::LABEL, stale ? DIM : TEXT, top_right);
  const float pace = f["wf"].is<int>() ? (int)f["wf"] / 100.f : -1;
  gfx::bar(M, wy + 26, bw, 10, anim::pw / 100, stale ? DIM : levelColor(pw), anim::since(anim::pwMs, 700), pace,
           stale ? 0 : anim::halo());
  for (int d = 1; d < 7; d++) fb().drawFastVLine(M + d * bw / 7, wy + 26, 10, BG);  // um trecho por dia
  if (pace >= 0) fb().fillSmoothRoundRect(M + (int)(bw * pace) - 1, wy + 21, 3, 20, 1, TEXT);

  // Tokens de hoje e ritmo.
  const int ty = 344, col2 = W() / 2 + 8;
  float glow = anim::since(anim::tokensMs, 900);
  text("tokens hoje", M, ty, gfx::SMALL, MUTED);
  text(f["tot"] | "0", M, ty + 18, gfx::NUM, blend(ACCENT, TEXT, glow));
  text("por minuto", col2, ty, gfx::SMALL, MUTED);
  text(f["rate"] | "0", col2, ty + 18, gfx::NUM, TEXT);

  // Última hora: linha sem eixos, área suave embaixo e o último ponto em destaque.
  JsonArrayConst sp = f["sp"];
  const int n = sp.size(), base = 456, h = 34;
  if (n > 1) {
    uint32_t peak = 1;
    for (uint32_t v : sp) peak = max(peak, v);
    const float reveal = anim::grow;  // a linha se desenha da esquerda para a direita ao entrar na tela
    auto px = [&](int i) { return M + i * bw / (n - 1); };
    auto py = [&](int i) { return base - (int)((uint32_t)sp[i] * h / peak); };
    const int last = max(1, (int)((n - 1) * reveal));
    static gfx::RegionCache sparkCache;
    uint32_t spKey = gfx::hashOf({(uint32_t)last, peak, ACCENT, BG});
    for (uint32_t v : sp) spKey = gfx::hashOf({spKey, v});
    if (!sparkCache.restore(spKey, 0, base - h - 2, W(), h + 4)) {
      uint16_t grad[64];  // degradê da área: forte no topo do gráfico, some na base
      for (int k = 0; k <= h; k++) grad[k] = __builtin_bswap16(blend(BG, ACCENT, 0.35f * (h - k) / h));
      auto* buf = static_cast<uint16_t*>(fb().getBuffer());
      for (int i = 1; i <= last; i++)
        for (int x = px(i - 1); x < px(i); x++) {
          int y = py(i - 1) + (py(i) - py(i - 1)) * (x - px(i - 1)) / max(1, px(i) - px(i - 1));
          for (int yy = y; yy < base; yy++) buf[yy * W() + x] = grad[yy - (base - h)];
        }
      for (int i = 1; i <= last; i++) fb().drawWideLine(px(i - 1), py(i - 1), px(i), py(i), 1.0f, MUTED);
      sparkCache.store(spKey);
    }
    fb().fillSmoothCircle(px(last), py(last), 4 + (1 - min(glow, 1.f)) * 3, ACCENT);
    text(fmtTokens(sp[n - 1]).c_str(), W() - M, base - h - 18, gfx::SMALL, DIM, top_right);
  }
  text("última hora", M, base - h - 18, gfx::SMALL, DIM);
  dots(page);
}

void drawModels(bool live, int page) {
  char buf[64];
  gfx::clear();
  header(live, "modelos hoje");
  JsonArrayConst mods = app::frame["mods"];
  if (mods.size() == 0) text("sem uso hoje", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
  int y = 68, i = 0;
  for (JsonArrayConst m : mods) {
    const char* name = m[0] | "?";
    int share = m[2];
    text(name, M, y, gfx::TITLE, TEXT);
    snprintf(buf, sizeof buf, "%d%%", share);
    text(buf, W() - M, y, gfx::TITLE, i == 0 ? ACCENT : MUTED, top_right);
    gfx::bar(M, y + 30, W() - 2 * M, 8, anim::entrance(i) * share / 100, i == 0 ? ACCENT : MUTED);
    snprintf(buf, sizeof buf, "%s tokens", (const char*)m[1]);
    text(buf, M, y + 48, gfx::LABEL, MUTED);
    snprintf(buf, sizeof buf, "entrada %s · saída %s", (const char*)m[3], (const char*)m[4]);
    text(buf, M, y + 70, gfx::SMALL, DIM);
    snprintf(buf, sizeof buf, "cache lido %s · escrito %s", (const char*)m[5], (const char*)m[6]);
    text(buf, M, y + 88, gfx::SMALL, DIM);
    y += 124, i++;
  }
  dots(page);
}

// Sem dados do Mac: o que está funcionando e o que falta, em ordem, com o que fazer em cada caso.
void drawWaiting() {
  char buf[64];
  gfx::clear();
  mascot::draw(W() / 2 - mascot::W * 4, 70, 8, mascot::IDLE, BG);
  text("sem dados do Mac", W() / 2, 172, gfx::TITLE, TEXT, middle_center);
  const bool usb = HWCDC::isPlugged(), port = usb && HWCDC::isConnected();
  const uint32_t id = hostlink::lastIdMs();
  const bool daemon = port && id && millis() - id < 15000;
  if (app::lastFrameMs) snprintf(buf, sizeof buf, "último dado há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "nenhum dado desde que a placa ligou");
  text(buf, W() / 2, 198, gfx::SMALL, MUTED, middle_center);

  struct Step {
    const char *name, *ok, *wait;
    bool done;
  } steps[] = {
      {"cabo USB", "o Mac reconheceu a placa", "use uma porta USB de dados do Mac", usb},
      {"daemon tokend", "abriu a porta e se apresentou", "rode: launchctl kickstart -k gui/$UID/com.gilsondev.tokenpulse", daemon},
      {"dados", "chegando", "esperando o primeiro envio", false},
  };
  int y = 236;
  bool blocked = false;
  for (auto& st : steps) {
    const bool cur = !st.done && !blocked;  // o primeiro passo que falta é o que importa agora
    const uint16_t c = st.done ? GREEN : cur ? AMBER : DIM;
    stepIcon(M + 10, y + 10, st.done ? 0 : cur ? 1 : 2);
    text(st.name, M + 32, y, gfx::LABEL, st.done || cur ? TEXT : DIM);
    if (st.done || cur) {  // a dica pode ser longa: até duas linhas na largura disponível
      const int maxW = W() - M - 32 - M;
      String l2, l1 = wrap2(st.done ? st.ok : st.wait, gfx::SMALL, maxW, l2);
      text(fit(l1.c_str(), gfx::SMALL, maxW).c_str(), M + 32, y + 22, gfx::SMALL, c);
      if (l2.length()) text(fit(l2.c_str(), gfx::SMALL, maxW).c_str(), M + 32, y + 40, gfx::SMALL, c);
    }
    if (!st.done) blocked = true;
    y += 70;
  }
  snprintf(buf, sizeof buf, "%s · %s", board::NAME, hostlink::firmware());
  text(buf, W() / 2, gfx::height() - 16, gfx::SMALL, DIM, middle_center);
}

constexpr int GRID_Y = 62, CARD_W = 140, CARD_H = 120, CARD_GAP = 8;

// Primeiro workspace à mostra: o desenho e o toque usam a mesma conta (a lista pode encolher).
int wsFrom(int scroll, int n) { return min(scroll * 10, max(0, n - 1) / 10 * 10); }

// Lista de workspaces do herdr: cartões 2 × 5 com o nome e quantas sessões estão em cada estado.
constexpr int WS_Y = 60, WS_H = 70, WS_GAP = 7;

void drawWorkspaces(bool live, int page, int scroll) {
  char buf[48];
  JsonArrayConst ws = app::frame["sess"]["ws"];
  const int n = ws.size(), from = wsFrom(scroll, n);
  snprintf(buf, sizeof buf, "sessões · %d", (int)(app::frame["sess"]["n"] | 0));
  header(live, buf);
  if (n == 0) {
    mascot::draw(W() / 2 - mascot::W * 3, 180, 6, mascot::IDLE, BG);
    text("nenhuma sessão aberta", W() / 2, 260, gfx::LABEL, DIM, middle_center);
  }
  for (int i = from; i < min(from + 10, n); i++) {
    JsonArrayConst w = ws[i];
    const int k = i - from, x = M + (k % 2) * (CARD_W + CARD_GAP), y = WS_Y + (k / 2) * (WS_H + WS_GAP);
    const int total = w[1], work = w[2], block = w[3], turn = w[4];
    const uint16_t card = block ? blend(BG, RED, 0.16f) : blend(BG, TRACK, work || turn ? 0.6f : 0.35f);
    const float in = anim::entrance(k);
    fb().fillSmoothRoundRect(x, y + (int)((1 - in) * 12), CARD_W, WS_H, 12, card);
    if (in < 0.9f) continue;
    text(fit(w[0], gfx::LABEL, CARD_W - 24).c_str(), x + 12, y + 10, gfx::LABEL, TEXT, top_left, card);
    snprintf(buf, sizeof buf, total == 1 ? "1 sessão" : "%d sessões", total);
    int cx = x + 12;
    if (!work && !block && !turn) {
      text(buf, cx, y + 40, gfx::SMALL, DIM, top_left, card);
      continue;
    }
    // um ponto colorido com a contagem para cada estado que importa
    const struct { int count; uint16_t c; } chips[] = {{block, RED}, {work, GREEN}, {turn, ACCENT}};
    for (auto& ch : chips) {
      if (!ch.count) continue;
      fb().fillSmoothCircle(cx + 4, y + 48, 4, ch.c);
      snprintf(buf, sizeof buf, "%d", ch.count);
      text(buf, cx + 12, y + 40, gfx::SMALL, ch.c, top_left, card);
      cx += 12 + gfx::textWidth(buf, gfx::SMALL) + 10;
    }
  }
  if (n > 10) text("arraste para cima: mais grupos", W() / 2, gfx::height() - 30, gfx::SMALL, DIM, middle_center);
}

int workspaceAt(int x, int y, int scroll) {
  const int n = app::frame["sess"]["ws"].size(), row = (y - WS_Y) / (WS_H + WS_GAP), col = (x - M) / (CARD_W + CARD_GAP);
  const int i = wsFrom(scroll, n) + row * 2 + col;
  return y >= WS_Y && row < 5 && col >= 0 && col < 2 && i < n ? i : -1;
}

// Sessões de um workspace em cartões 2 × 3, cada um com o mascote no estado da sessão; arrastar na
// vertical mostra as próximas 6. Tocar no título volta para a lista de workspaces.
void drawSessions(bool live, int page, int ws, int scroll) {
  char buf[48];
  gfx::clear();
  if (ws < 0) {
    drawWorkspaces(live, page, scroll);
    return dots(page);
  }
  // as sessões do workspace, na ordem em que vieram (já ordenadas por estado)
  JsonArrayConst rows = app::frame["sess"]["rows"];
  int ids[48], shown = 0;
  for (int r = 0; r < (int)rows.size() && shown < 48; r++)
    if ((int)rows[r][5] == ws) ids[shown++] = r;
  JsonArrayConst w = app::frame["sess"]["ws"][ws];
  const int from = min(scroll * 6, max(0, shown - 1) / 6 * 6);
  if (shown > 6) snprintf(buf, sizeof buf, "‹ %s · %d–%d de %d", (const char*)(w[0] | "?"), from + 1, min(from + 6, shown), (int)(w[1] | 0));
  else snprintf(buf, sizeof buf, "‹ %s", (const char*)(w[0] | "?"));
  header(live, buf);
  if (shown > 6) text("arraste para cima: mais sessões", W() / 2, gfx::height() - 30, gfx::SMALL, DIM, middle_center);
  int i = 0;
  for (int idx = from; idx < min(from + 6, shown); idx++) {
    JsonArrayConst row = rows[ids[idx]];
    const int x = M + (i % 2) * (CARD_W + CARD_GAP), y = GRID_Y + (i / 2) * (CARD_H + CARD_GAP);
    const char* state = row[2] | "";
    const mascot::Mood mood = !strcmp(state, "work")    ? mascot::WORK
                              : !strcmp(state, "turn")  ? mascot::TURN
                              : !strcmp(state, "block") ? mascot::BLOCK
                                                        : mascot::IDLE;
    const uint16_t card = mood == mascot::BLOCK ? blend(BG, RED, 0.16f) : blend(BG, TRACK, mood == mascot::IDLE ? 0.35f : 0.6f);
    const float in = anim::entrance(i);
    fb().fillSmoothRoundRect(x, y + (int)((1 - in) * 16), CARD_W, CARD_H, 14, card);
    if (in < 0.9f) {
      i++;
      continue;
    }
    mascot::draw(x + 12, y + 14, 4, mood, card, i);
    const char* origin = row[1] | "CLI";
    badge(origin, x + CARD_W - 10, y + 12, !strcmp(origin, "Desk") ? ACCENT : GREEN, card);
    text(row[3], x + CARD_W - 10, y + 40, gfx::SMALL, DIM, top_right, card);
    // título da sessão em até duas linhas, quebrado nas palavras
    const int maxW = CARD_W - 24;
    String l2, name = wrap2(row[0] | "", gfx::LABEL, maxW, l2);
    text(fit(name.c_str(), gfx::LABEL, maxW).c_str(), x + 12, y + 56, gfx::LABEL, TEXT, top_left, card);
    if (l2.length()) text(fit(l2.c_str(), gfx::LABEL, maxW).c_str(), x + 12, y + 75, gfx::LABEL, TEXT, top_left, card);
    const int mins = row[4];
    if (mood == mascot::WORK) strcpy(buf, "trabalhando");
    else if (mood == mascot::BLOCK) strcpy(buf, "precisa de você");
    else if (mood == mascot::TURN) snprintf(buf, sizeof buf, mins ? "sua vez · %d min" : "sua vez", mins);
    else if (mins < 0) strcpy(buf, "parada");
    else if (mins >= 120) snprintf(buf, sizeof buf, "parada há %dh", mins / 60);
    else snprintf(buf, sizeof buf, "parada há %d min", mins);
    const uint16_t sc = mood == mascot::WORK ? GREEN : mood == mascot::BLOCK ? RED : mood == mascot::TURN ? ACCENT : DIM;
    text(buf, x + 12, y + 100, gfx::SMALL, sc, top_left, card);
    i++;
  }
  dots(page);
}

int modelAt(int y) {
  const int i = (y - 68) / 124;
  return y >= 68 && i < (int)app::frame["mods"].size() ? i : -1;
}

// Detalhe de um modelo: tokens e custo de hoje, de onde vêm (entrada, saída, cache) e quais projetos usaram.
void drawModelDetail(bool live, int page, int idx) {
  JsonArrayConst m = app::frame["mods"][idx];
  if (m.isNull()) return drawModels(live, page);
  char buf[64];
  gfx::clear();
  header(live, m[0]);
  text(m[1], M, 68, gfx::NUM, TEXT);
  snprintf(buf, sizeof buf, "%d%%", (int)m[2]);
  text(buf, W() - M, 72, gfx::TITLE, ACCENT, top_right);
  if (settings::v.hideCost) strcpy(buf, "tokens hoje");
  else snprintf(buf, sizeof buf, "tokens hoje · %s na API", (const char*)(m[7] | "--"));
  text(buf, M, 106, gfx::SMALL, MUTED);

  // composição numa barra empilhada
  const char* names[] = {"entrada", "saída", "cache lido", "cache escrito"};
  const uint16_t cols[] = {blend(ACCENT, TEXT, 0.5f), ACCENT, GREEN, AMBER};
  float v[4], sum = 0;
  for (int k = 0; k < 4; k++) sum += v[k] = parseTokens(m[3 + k]);
  const int bx = M, by = 140, bw = W() - 2 * M, bh = 14, r = bh / 2;
  const float grow = anim::entrance(0);
  fb().fillSmoothRoundRect(bx, by, bw, bh, r, TRACK);
  // segmentos retos entre as pontas; as pontas são meias-luas na cor do primeiro e do último segmento
  int x = bx, first = -1, last = -1;
  for (int k = 0; k < 4 && sum > 0; k++) {
    const int w = (int)(bw * grow * v[k] / sum + 0.5f);
    if (w <= 0) continue;
    if (first < 0 && w >= r) first = k;  // ponta na cor de um segmento visível, não de um fio de 1 px
    if (w >= r) last = k;
    const int x0 = max(x, bx + r), x1 = min(x + w, bx + bw - r);
    if (x1 > x0) fb().fillRect(x0, by, x1 - x0, bh, cols[k]);
    x += w;
  }
  if (first >= 0) {
    fb().fillSmoothCircle(bx + r, by + r, r, cols[first]);
    if (x >= bx + bw - r) fb().fillSmoothCircle(bx + bw - r - 1, by + r, r, cols[last]);
  }
  for (int k = 0; k < 4; k++) {
    const int y = 168 + k * 26;
    fb().fillSmoothCircle(M + 5, y + 9, 5, cols[k]);
    text(names[k], M + 18, y, gfx::LABEL, MUTED);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)m[3 + k], sum > 0 ? (int)lroundf(100 * v[k] / sum) : 0);
    text(buf, W() - M, y, gfx::LABEL, TEXT, top_right);
  }

  text("projetos que mais usaram", M, 284, gfx::SMALL, DIM);
  JsonArrayConst projs = m[8];
  float top = 1;
  for (JsonArrayConst p : projs) top = max(top, parseTokens(p[1]));
  int i = 0;
  for (JsonArrayConst p : projs) {
    const int y = 306 + i * 44;
    text(fit(p[0], gfx::LABEL, W() - 2 * M - 70).c_str(), M, y, gfx::LABEL, TEXT);
    text(p[1], W() - M, y, gfx::LABEL, MUTED, top_right);
    gfx::bar(M, y + 24, W() - 2 * M, 6, anim::entrance(i + 1) * parseTokens(p[1]) / top, ACCENT);
    i++;
  }
  text("toque para voltar", W() / 2, gfx::height() - 34, gfx::SMALL, DIM, middle_center);
  dots(page);
}

// Histórico: tokens por dia nos últimos 14 dias, a barra tocada (ou hoje) em destaque.
constexpr int CH_Y = 150, CH_H = 200, BAR_GAP = 5;

int historyBarAt(int x, int y) {
  const int n = app::frame["hist"].size();
  if (!n || y < CH_Y - 20 || y > CH_Y + CH_H + 30) return -1;
  const int slot = (W() - 2 * M + BAR_GAP) / n;
  return constrain((x - M) / slot, 0, n - 1);
}

void drawHistory(bool live, int page, int sel) {
  char buf[64];
  gfx::clear();
  header(live, "histórico · 14 dias");
  JsonArrayConst hist = app::frame["hist"];
  const int n = hist.size();
  if (!n) return dots(page);
  if (sel < 0 || sel >= n) sel = n - 1;
  float peak = 0.1f, sumTok = 0;
  int sumCost = 0;
  for (JsonArrayConst d : hist) peak = max(peak, (float)d[2]), sumTok += (float)d[2], sumCost += (int)d[3];

  JsonArrayConst s = hist[sel];
  snprintf(buf, sizeof buf, "%s %d%s", (const char*)s[0], (int)s[1], sel == n - 1 ? " · hoje" : "");
  text(buf, M, 66, gfx::SMALL, MUTED);
  snprintf(buf, sizeof buf, "%.1fM", (float)s[2]);
  String tok = buf;
  tok.replace('.', ',');
  text(tok.c_str(), M, 84, gfx::NUM, TEXT);
  const bool cost = !settings::v.hideCost;
  snprintf(buf, sizeof buf, "US$ %d", (int)s[3]);
  if (cost) text(buf, W() - M, 84, gfx::NUM, ACCENT, top_right);

  const int slot = (W() - 2 * M + BAR_GAP) / n, bw = slot - BAR_GAP;
  fb().drawFastHLine(M, CH_Y + CH_H, W() - 2 * M, TRACK);
  for (int i = 0; i < n; i++) {
    JsonArrayConst d = hist[i];
    const int x = M + i * slot, h = max(2, (int)(CH_H * anim::entrance(i / 2) * (float)d[2] / peak));
    const bool on = i == sel;
    fb().fillSmoothRoundRect(x, CH_Y + CH_H - h, bw, h, min(bw / 2, 5), on ? ACCENT : blend(BG, MUTED, i == n - 1 ? 0.7f : 0.45f));
    char day[2] = {(d[0] | "?")[0], 0};
    text(day, x + bw / 2, CH_Y + CH_H + 14, gfx::SMALL, on ? TEXT : DIM, middle_center);
  }

  if (sumTok >= 1000) snprintf(buf, sizeof buf, "14 dias: %.1fB tokens", sumTok / 1000);
  else snprintf(buf, sizeof buf, "14 dias: %.0fM tokens", sumTok);
  String total = buf;
  total.replace('.', ',');
  if (cost) total += " · US$ " + String(sumCost);
  text(total.c_str(), M, 384, gfx::SMALL, MUTED);
  if (cost) {
    snprintf(buf, sizeof buf, "média de US$ %d por dia", sumCost / n);
    text(buf, M, 404, gfx::SMALL, MUTED);
    text("custo equivalente na API", M, 428, gfx::SMALL, DIM);
  }
  dots(page);
}

// Ajustes: uma linha por item, com o nome à esquerda e as opções num controle segmentado à direita.
namespace {
struct Row {
  const char* name;
  uint8_t* value;
  const char* opts[4];
  uint8_t count;
};
constexpr int SET_Y = 62, ROW_H = 68, SEG_TOP = 20, SEG_H = 38;  // nome em cima, controle na largura toda

Row settingRows[] = {
      {"Brilho", &settings::v.bright, {"25%", "50%", "75%", "100%"}, 4},
      {"Escurecer quando parada", &settings::v.idle, {"5 min", "10 min", "30 min", "nunca"}, 4},
      {"Tema", &settings::v.theme, {"automático", "claro", "escuro"}, 3},
      {"Tela", &settings::v.flip, {"normal", "girada 180°"}, 2},
    {"Custo em US$", &settings::v.hideCost, {"mostrar", "ocultar"}, 2},
};
constexpr int ROWS = sizeof settingRows / sizeof *settingRows;
constexpr int ABOUT_Y = SET_Y + ROWS * ROW_H + 4;
}  // namespace

void drawSettings(bool live) {
  gfx::clear();
  header(live, "ajustes");
  const int segW = W() - 2 * M;
  const uint16_t track = blend(BG, TRACK, 0.7f);
  for (int i = 0; i < ROWS; i++) {
    const Row& r = settingRows[i];
    const int y = SET_Y + i * ROW_H, sy = y + SEG_TOP;
    text(r.name, M, y, gfx::SMALL, MUTED);
    fb().fillSmoothRoundRect(M, sy, segW, SEG_H, SEG_H / 2, track);
    for (int k = 0; k < r.count; k++) {
      // bordas calculadas do mesmo jeito para todos: segmentos de largura igual, texto no centro
      const int x0 = M + segW * k / r.count, x1 = M + segW * (k + 1) / r.count;
      const bool on = *r.value == k;
      if (on) fb().fillSmoothRoundRect(x0 + 3, sy + 3, x1 - x0 - 6, SEG_H - 6, (SEG_H - 6) / 2, ACCENT);
      text(r.opts[k], (x0 + x1) / 2, sy + SEG_H / 2, gfx::SMALL, on ? BG : TEXT, middle_center, on ? ACCENT : track);
    }
  }
  // dois botões lado a lado: a conta (org) e o Sobre
  const uint16_t btn = blend(BG, TRACK, 0.7f);
  const int half = (W() - 2 * M - 8) / 2;
  fb().fillSmoothRoundRect(M, ABOUT_Y, half, 44, 10, btn);
  text(fit(app::frame["org"] | "Conta", gfx::LABEL, half - 40).c_str(), M + 14, ABOUT_Y + 22, gfx::LABEL, TEXT, middle_left, btn);
  text("›", M + half - 12, ABOUT_Y + 22, gfx::TITLE, MUTED, middle_right, btn);
  fb().fillSmoothRoundRect(M + half + 8, ABOUT_Y, half, 44, 10, btn);
  text("Sobre", M + half + 22, ABOUT_Y + 22, gfx::LABEL, TEXT, middle_left, btn);
  text("›", W() - M - 12, ABOUT_Y + 22, gfx::TITLE, MUTED, middle_right, btn);
}

// Toque nos ajustes: 0 trocou um ajuste, 1 abriu o Sobre, 2 abriu a Conta, -1 nada aqui
int settingsTap(int x, int y) {
  if (y >= ABOUT_Y && y < ABOUT_Y + 44) return x < W() / 2 ? 2 : 1;
  const int i = (y - SET_Y) / ROW_H, inRow = (y - SET_Y) % ROW_H;
  if (y < SET_Y || i >= ROWS || inRow < SEG_TOP - 6 || inRow > SEG_TOP + SEG_H + 4 || x < M || x >= W() - M) return -1;
  Row& r = settingRows[i];
  const int k = min<int>((x - M) * r.count / (W() - 2 * M), r.count - 1);
  *r.value = k;
  settings::save();
  if (r.value == &settings::v.flip) gfx::setRotation(k ? 2 : 0);
  return 0;
}

// Sobre: o que a placa sabe de si mesma, e reiniciar com toque duplo de confirmação.
constexpr int RESTART_Y = 372;

void drawAbout(bool live, bool armed) {
  char buf[64];
  gfx::clear();
  header(live, "sobre");
  const uint32_t up = millis() / 60000;
  if (up >= 60) snprintf(buf, sizeof buf, "%luh%02lu", up / 60, up % 60);
  else snprintf(buf, sizeof buf, "%lu min", up);
  String upTxt = buf;
  snprintf(buf, sizeof buf, "%u KB · PSRAM %u KB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  String mem = buf;
  if (live) strcpy(buf, "ao vivo pela USB");
  else if (app::lastFrameMs) snprintf(buf, sizeof buf, "sem dados há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "esperando o Mac");
  String link = buf;
  snprintf(buf, sizeof buf, "atualizado há %d min", app::dataAge() / 60);
  String pct = app::stale() ? String(buf) : String("em dia");
  const char* items[][2] = {
      {"placa", board::NAME},          {"firmware", hostlink::firmware()}, {"ligada há", upTxt.c_str()},
      {"memória livre", mem.c_str()},  {"conexão", link.c_str()},          {"% de uso", pct.c_str()},
  };
  int y = 70;
  for (auto& it : items) {
    text(it[0], M, y, gfx::SMALL, DIM);
    text(it[1], M, y + 18, gfx::LABEL, TEXT);
    y += 48;
  }
  const uint16_t c = armed ? RED : blend(BG, TRACK, 0.7f);
  fb().fillSmoothRoundRect(M, RESTART_Y, W() - 2 * M, 48, 12, c);
  text(armed ? "toque de novo para reiniciar" : "reiniciar a placa", W() / 2, RESTART_Y + 24, gfx::LABEL, armed ? BG : TEXT,
       middle_center, c);
  text("toque fora para voltar aos ajustes", W() / 2, gfx::height() - 34, gfx::SMALL, DIM, middle_center);
}

bool aboutRestartAt(int y) { return y >= RESTART_Y && y < RESTART_Y + 48; }
bool gearAt(int x, int y) { return x >= W() - 64 && y < 52; }
bool orgTitleAt(int x, int y) { return x < W() / 2 && y < 30; }
void setGearActive(bool on) { gearActive = on; }

// Conta: a org atual, as contas que o cswap conhece e o login de uma org nova. A troca roda no Mac
// (cswap ou o login do Claude Code no navegador); a placa só pede e acompanha.
constexpr int ACC_LIST_Y = 200, ACC_ROW = 62, ACC_NEW_Y = 392;
// trocar de org troca o login de todas as sessões do Mac: o primeiro toque arma, o segundo (em 5 s) confirma
int armedAcct = -2;  // número da conta armada; 0 = "entrar em outra org"; -2 nada
uint32_t armedMs = 0;
bool armed(int num) { return armedAcct == num && millis() - armedMs < 5000; }

String ago(int mins) {
  char b[16];
  if (mins < 60) snprintf(b, sizeof b, "há %d min", mins);
  else if (mins < 1440) snprintf(b, sizeof b, "há %dh", mins / 60);
  else snprintf(b, sizeof b, "há %dd", mins / 1440);
  return b;
}


void drawAccount(bool live) {
  char buf[64];
  gfx::clear();
  header(live, "trocar de org");
  text("org atual", M, 66, gfx::SMALL, DIM);
  text(app::frame["org"] | "--", M, 84, gfx::NUM, TEXT);

  JsonArrayConst act = app::frame["act"];
  const char* st = act[0] | "";
  const char* target = act[1] | "";
  const uint16_t card = blend(BG, TRACK, 0.6f);
  if (*st) {
    fb().fillSmoothRoundRect(M, 124, W() - 2 * M, 72, 12, card);
    if (!strcmp(st, "switching")) {
      stepIcon(M + 20, 160, 1);
      snprintf(buf, sizeof buf, "trocando para %s…", target);
      text(buf, M + 38, 160, gfx::LABEL, TEXT, middle_left, card);
    } else if (!strcmp(st, "login")) {
      // o login acontece no navegador do Mac: a placa mostra em que passo está
      stepIcon(M + 20, 142, 0);
      text("login aberto no navegador do Mac", M + 38, 142, gfx::SMALL, TEXT, middle_left, card);
      stepIcon(M + 20, 166, 1);
      snprintf(buf, sizeof buf, "escolha a org %s e autorize", *target ? target : "desejada");
      text(buf, M + 38, 166, gfx::SMALL, AMBER, middle_left, card);
      stepIcon(M + 20, 188, 2);
      text("a placa confirma a troca", M + 38, 188, gfx::SMALL, DIM, middle_left, card);
    } else if (!strcmp(st, "done")) {
      stepIcon(M + 20, 160, 0);
      snprintf(buf, sizeof buf, "pronto: agora na org %s", (const char*)(app::frame["org"] | "?"));
      text(buf, M + 38, 160, gfx::LABEL, GREEN, middle_left, card);
    } else {
      snprintf(buf, sizeof buf, "não deu certo: %s", (const char*)(act[2] | "erro"));
      text(fit(buf, gfx::SMALL, W() - 2 * M - 24).c_str(), M + 14, 160, gfx::SMALL, RED, middle_left, card);
    }
  }

  text("orgs salvas no cswap", M, ACC_LIST_Y - 20, gfx::SMALL, DIM);
  JsonArrayConst accts = app::frame["accts"];
  if (accts.size() == 0) text("nenhuma: rode cswap add no Mac", M, ACC_LIST_Y + 6, gfx::SMALL, MUTED);
  int i = 0;
  for (JsonArrayConst a : accts) {
    const int y = ACC_LIST_Y + i * ACC_ROW, num = a[0];
    const bool active = (int)a[2], relogin = (int)a[3], arm = armed(num);
    const uint16_t rowBg = arm ? blend(BG, RED, 0.18f) : card;
    fb().fillSmoothRoundRect(M, y, W() - 2 * M, ACC_ROW - 6, 12, rowBg);
    const String name = fit(a[1], gfx::LABEL, 150);
    text(name.c_str(), M + 14, y + 8, gfx::LABEL, TEXT, top_left, rowBg);
    const char* tag = active ? "em uso" : relogin ? "login expirado" : "";
    text(tag, M + 20 + gfx::textWidth(name.c_str(), gfx::LABEL), y + 10, gfx::SMALL, active ? GREEN : AMBER, top_left, rowBg);
    // o último % conhecido dessa org (só a org em uso tem o número ao vivo)
    if (arm) strcpy(buf, "troca todas as sessões");
    else if ((int)a[4] >= 0) snprintf(buf, sizeof buf, "5h %d%% · semana %d%% · %s", (int)a[4], (int)a[5], ago(a[6]).c_str());
    else strcpy(buf, active ? "buscando o %…" : "sem % recente");
    text(fit(buf, gfx::SMALL, active ? W() - 2 * M - 28 : W() - 2 * M - 120).c_str(), M + 14, y + 32, gfx::SMALL,
         arm ? RED : DIM, top_left, rowBg);
    if (!active) {
      const uint16_t b = arm ? RED : ACCENT;
      fb().fillSmoothRoundRect(W() - M - 92, y + 12, 80, 30, 15, b);
      text(arm ? "confirmar" : "trocar", W() - M - 52, y + 27, gfx::SMALL, BG, middle_center, b);
    }
    if (++i == 3) break;
  }
  const bool armNew = armed(0);
  const uint16_t nb = armNew ? blend(BG, RED, 0.18f) : card;
  fb().fillSmoothRoundRect(M, ACC_NEW_Y, W() - 2 * M, 40, 12, nb);
  text(armNew ? "toque de novo: login troca todas as sessões" : "entrar em outra org", W() / 2, ACC_NEW_Y + 20, gfx::SMALL,
       armNew ? RED : TEXT, middle_center, nb);
  text("toque fora para fechar", W() / 2, gfx::height() - 18, gfx::SMALL, DIM, middle_center);
}

// Toque na Conta: arma ou confirma uma troca (true) ou devolve false para fechar o painel.
bool accountTap(int x, int y) {
  auto confirm = [](int num, const char* action) {
    if (armed(num)) {
      Serial.println(action);
      armedAcct = -2;
    } else {
      armedAcct = num, armedMs = millis();
    }
  };
  if (y >= ACC_NEW_Y && y < ACC_NEW_Y + 40) {
    confirm(0, "ACTION login");
    return true;
  }
  JsonArrayConst accts = app::frame["accts"];
  const int i = (y - ACC_LIST_Y) / ACC_ROW;
  if (y < ACC_LIST_Y || i >= min(3, (int)accts.size())) return y >= 56 && y < ACC_LIST_Y;  // área do status: fica
  if ((int)accts[i][2]) return true;  // a org em uso
  char cmd[32];
  snprintf(cmd, sizeof cmd, "ACTION switch %d", (int)accts[i][0]);
  confirm(accts[i][0], cmd);
  return true;
}

}  // namespace screens::tall
