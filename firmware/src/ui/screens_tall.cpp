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

int W() { return gfx::width(); }
bool L() { return gfx::width() > gfx::height(); }  // deitada (Ajustes → Tela → horizontal)
// O layout é desenhado para 320x480 em pé e 480x320 deitado, e escala pela altura: 1 na tela grande, 2/3
// (em pé) ou 3/4 (deitada) na média. As fontes da média não encolhem tanto (12 px no mínimo); onde texto
// empilha, a distância usa below().
float K() { return gfx::height() / (L() ? 320.f : 480.f); }
int u(float v) { return lroundf(v * K()); }
int M() { return u(16); }  // margem
int below(gfx::Font f, int gap) { return max(u(gap), gfx::lineHeight(f)); }  // próxima linha de texto

// Pontos de página no rodapé: todos com 6 px de altura no mesmo eixo; a página atual é uma pílula.
void dots(int page) {
  const int H = u(6), DOT = u(6), PILL = u(18), GAP = u(8);
  const int total = PILL + (PAGES - 1) * (DOT + GAP), y = gfx::height() - u(14) - H / 2;
  int x = W() / 2 - total / 2;
  for (int i = 0; i < PAGES; i++) {
    const int w = i == page ? PILL : DOT;
    fb().fillSmoothRoundRect(x, y, w, H, H / 2, i == page ? TEXT : DIM);
    x += w + GAP;
  }
}

// Etiqueta de origem alinhada à direita em `right`: fundo tingido, texto na cor cheia.
void badge(const char* label, int right, int top, uint16_t c, uint16_t base = BG) {
  const int H = u(22), PADX = u(10);
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
    fb().fillSmoothCircle(x, y, u(9), blend(BG, GREEN, 0.25f));
    gfx::wideLine(x - u(4), y, x - u(1), y + u(3), 1.1f, GREEN);
    gfx::wideLine(x - u(1), y + u(3), x + u(5), y - u(4), 1.1f, GREEN);
  } else if (state == 1) {
    gfx::ring(x, y, u(9), max(2, u(3)), 0.25f + 0.2f * breath(1200), AMBER, TRACK);
  } else {
    fb().fillSmoothCircle(x, y, max(2, u(3)), DIM);
  }
}

// Engrenagem dos ajustes, ao lado do relógio: disco, 8 dentes e o furo no meio.
bool gearActive = false;

void gear(int cx, int cy, int r, uint16_t c) {
  for (int k = 0; k < 8; k++) {
    const float a = k * TAU / 8;
    gfx::wideLine(cx + cosf(a) * r * 0.55f, cy + sinf(a) * r * 0.55f, cx + cosf(a) * r * 0.92f, cy + sinf(a) * r * 0.92f,
                      r * 0.17f, c);
  }
  fb().fillSmoothCircle(cx, cy, r * 0.68f, c);
  fb().fillSmoothCircle(cx, cy, r * 0.28f, BG);
}

// Cabeçalho: org e relógio; embaixo modelo, ponto de status e sessões abertas.
void header(bool live, const char* title, bool dropdown = false) {
  const auto& f = app::frame;
  const char* clk = f["clk"] | "";
  const int clkLeft = W() - M() - u(30) - gfx::textWidth(clk, gfx::TITLE);
  const String t = fit(title, gfx::LABEL, clkLeft - M() - u(10) - (dropdown ? u(18) : 0));
  text(t.c_str(), M(), u(10), gfx::LABEL, TEXT);
  if (dropdown) {  // a org abre a lista de orgs: um "v" pequeno depois do nome
    const int x = M() + gfx::textWidth(t.c_str(), gfx::LABEL) + u(8), y = u(19), d = max(3, u(4));
    gfx::wideLine(x, y - d / 2, x + d, y + d / 2, 1.0f, MUTED);
    gfx::wideLine(x + d, y + d / 2, x + 2 * d, y - d / 2, 1.0f, MUTED);
  }
  text(clk, W() - M() - u(30), u(8), gfx::TITLE, TEXT, top_right);
  gear(W() - M() - u(10), u(19), u(10), gearActive ? ACCENT : MUTED);
  char buf[32];
  if (live) snprintf(buf, sizeof buf, "CLI %d · Desk %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0));
  else snprintf(buf, sizeof buf, "offline há %lu min", (millis() - app::lastFrameMs) / 60000);
  const int sy = u(10) + below(gfx::LABEL, 22), w = gfx::textWidth(buf, gfx::SMALL);
  // o modelo divide a linha com o status: corta o modelo, nunca o status
  text(fit(f["m"] | "Claude", gfx::SMALL, W() - 2 * M() - w - u(24)).c_str(), M(), sy, gfx::SMALL, DIM);
  text(buf, W() - M(), sy, gfx::SMALL, live ? DIM : RED, top_right);
  fb().fillSmoothCircle(W() - M() - w - u(8), sy + gfx::lineHeight(gfx::SMALL) / 2 - u(2), max(2, u(3)), live ? GREEN : RED);
}

// Outra org salva no cswap para sugerir quando a janela esgota: a de menor % conhecido.
const char* suggestedOrg() {
  const char* best = nullptr;
  int bestPct = 1000;
  for (JsonArrayConst a : app::frame["accts"].as<JsonArrayConst>()) {
    if ((int)a[2]) continue;  // a org em uso
    const int pct = (int)(a[4] | -1) < 0 ? 500 : (int)a[4];
    if (pct < bestPct) bestPct = pct, best = a[1] | "";
  }
  return best;
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

// Botão "esgotado · trocar para…": onde foi desenhado por último, para o toque achar o mesmo lugar.
int sugX = 0, sugY = 0, sugW = 0, sugH = 0;

void drawMain(bool live, int page) {
  const auto& f = app::frame;
  const bool stale = app::stale() || !live, land = L();
  const int p5 = f["p5"] | -1, pw = f["pw"] | -1;
  char buf[48];
  gfx::clear();
  header(live, f["org"] | "Claude", true);

  // Anel da janela de 5h: o herói da tela. Em pé fica no alto e centrado; deitada, na coluna da esquerda.
  const int r = u(land ? 108 : 100), thick = u(15);
  const int cx = land ? M() + r + u(4) : W() / 2, cy = u(land ? 182 : 160);
  // o resto ocupa a largura toda em pé, ou a coluna à direita do anel deitada
  const int x0 = land ? cx + r + u(28) : M(), cw = W() - M() - x0;
  const float fade = anim::since(anim::alertMs, 400);  // mudou de faixa: a cor anterior vira a nova
  uint16_t lc = stale ? DIM : fade < 1 ? blend(anim::alertFrom, levelColor(p5), fade) : levelColor(p5);
  static gfx::RegionCache ringCache;
  const float frac = min(anim::p5, 100.f) / 100;
  const uint32_t ringKey = gfx::hashOf({(uint32_t)lroundf(frac * 2000), lc, TRACK, BG});
  if (!ringCache.restore(ringKey, cx - r - 2, cy - r - 2, 2 * r + 5, 2 * r + 5)) {
    gfx::ring(cx, cy, r, thick, frac, lc, TRACK);
    ringCache.store(ringKey);
  }
  if (!stale) gfx::ringHalo(cx, cy, r, thick, frac, lc, anim::halo());
  // com 0% ou "--" o daemon manda s5 ("janela nova · 17h"): sem ele, o 0% do começo da janela parece erro
  const char* s5 = f["s5"] | "";
  // texto dentro do anel: no máximo a corda do furo (raio r - 15) na borda do texto mais longe do centro
  auto inner = [&](int dy) { return (int)(2 * sqrtf(max(0.f, (float)(r - thick - 2) * (r - thick - 2) - dy * dy))); };
  // se não couber, desce para a fonte menor; cortar com reticência só como último recurso
  auto ringText = [&](const char* t, int dy, gfx::Font f, uint16_t c) {
    const int maxW = inner(abs(dy) + u(6));
    if (f != gfx::SMALL && gfx::textWidth(t, f) > maxW) f = gfx::SMALL;
    text(fit(t, f, maxW).c_str(), cx, cy + dy, f, c, middle_center);
  };
  const char* warn = stale || p5 < 80 ? nullptr : p5 >= 100 ? "esgotado" : p5 >= 90 ? "no limite" : "perto do limite";
  // deitada não há espaço sob o anel: o aviso de limite ocupa a linha de cima, dentro dele
  if (land && warn) ringText(warn, u(-46), gfx::SMALL, levelColor(p5));
  else ringText(*s5 ? s5 : "sessão 5h", u(-46), gfx::SMALL, *s5 ? ACCENT : DIM);
  if (p5 < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d%%", (int)lroundf(anim::p5));
  heroNumber(buf, cx, cy, stale ? DIM : p5 < 0 ? TEXT : lc);  // o número na cor da faixa, como o anel
  if (app::stale()) snprintf(buf, sizeof buf, "atualizado há %d min", app::dataAge() / 60);
  ringText(app::stale() ? buf : (const char*)(f["r5"] | ""), u(44), gfx::LABEL, app::stale() ? AMBER : MUTED);
  const char* other = !stale && p5 >= 100 ? suggestedOrg() : nullptr;
  sugW = 0;
  if (!land && other) {  // janela esgotada e há outra org salva: oferece a troca
    sugY = cy + r + u(2), sugH = max(u(26), gfx::lineHeight(gfx::SMALL) + 6);
  } else if (!land && warn) {
    text(warn, cx, cy + r + u(14), gfx::SMALL, levelColor(p5), middle_center);
  }

  // Semana, com o marcador do ritmo linear: barra à frente do marcador = uso adiantado. Em pé, de baixo
  // para cima (gráfico, números, semana): na tela média as fontes são maiores que a escala, e as posições
  // fixas se sobreporiam. Deitada, de cima para baixo na coluna da direita.
  const int base = u(land ? 290 : 456), sh = u(land ? 40 : 34), label = base - sh - below(gfx::SMALL, 18), bh = u(10);
  int wy, ty;
  if (land) {
    wy = u(66);
    ty = wy + below(gfx::LABEL, 26) + bh + u(8) + gfx::lineHeight(gfx::SMALL) + u(16);
  } else {
    ty = min(u(344), label - u(4) - gfx::lineHeight(gfx::NUM) - below(gfx::SMALL, 18));
    wy = min(u(290), ty - u(18) - bh - below(gfx::LABEL, 26));
  }
  const int bw = cw, by = wy + below(gfx::LABEL, 26);
  text("semana", x0, wy, gfx::LABEL, MUTED);
  if (pw < 0) strcpy(buf, "--");
  else if (land) snprintf(buf, sizeof buf, "%d%%", pw);  // a coluna é estreita: o reset vai embaixo da barra
  else snprintf(buf, sizeof buf, "%d%% · reset %s", pw, (const char*)(f["rw"] | ""));
  text(buf, x0 + cw, wy, gfx::LABEL, stale ? DIM : TEXT, top_right);
  const float pace = f["wf"].is<int>() ? (int)f["wf"] / 100.f : -1;
  gfx::bar(x0, by, bw, bh, anim::pw / 100, stale ? DIM : levelColor(pw), anim::since(anim::pwMs, 700), pace,
           stale ? 0 : anim::halo());
  for (int d = 1; d < 7; d++) fb().drawFastVLine(x0 + d * bw / 7, by, bh, BG);  // um trecho por dia
  if (pace >= 0) fb().fillSmoothRoundRect(x0 + (int)(bw * pace) - 1, by - (u(20) - bh) / 2, 3, u(20), 1, TEXT);
  if (land && pw >= 0) {
    snprintf(buf, sizeof buf, "reset %s", (const char*)(f["rw"] | ""));
    text(buf, x0, by + bh + u(8), gfx::SMALL, DIM);
  }

  // Tokens de hoje e ritmo: lado a lado em pé; deitada, o ritmo numa linha embaixo.
  const int col2 = W() / 2 + u(8);
  float glow = anim::since(anim::tokensMs, 900);
  text("tokens hoje", x0, ty, gfx::SMALL, MUTED);
  text(f["tot"] | "0", x0, ty + below(gfx::SMALL, 18), gfx::NUM, blend(ACCENT, TEXT, glow));
  if (land) {
    snprintf(buf, sizeof buf, "%s por minuto", (const char*)(f["rate"] | "0"));
    text(fit(buf, gfx::SMALL, cw).c_str(), x0, ty + below(gfx::SMALL, 18) + gfx::lineHeight(gfx::NUM), gfx::SMALL, MUTED);
  } else {
    text("por minuto", col2, ty, gfx::SMALL, MUTED);
    text(f["rate"] | "0", col2, ty + below(gfx::SMALL, 18), gfx::NUM, TEXT);
  }

  if (land && other) {  // deitada, a oferta de troca fica no lugar do gráfico da última hora
    sugY = base - sh, sugH = max(u(30), gfx::lineHeight(gfx::SMALL) + 8);
  } else {
    // Última hora: linha sem eixos, área suave embaixo e o último ponto em destaque.
    JsonArrayConst sp = f["sp"];
    const int n = sp.size(), h = sh;
    if (n > 1) {
      uint32_t peak = 1;
      for (uint32_t v : sp) peak = max(peak, v);
      const float reveal = anim::grow;  // a linha se desenha da esquerda para a direita ao entrar na tela
      auto px = [&](int i) { return x0 + i * bw / (n - 1); };
      auto py = [&](int i) { return base - (int)((uint32_t)sp[i] * h / peak); };
      const int last = max(1, (int)((n - 1) * reveal));
      static gfx::RegionCache sparkCache;
      uint32_t spKey = gfx::hashOf({(uint32_t)last, peak, ACCENT, BG, (uint32_t)x0});
      for (uint32_t v : sp) spKey = gfx::hashOf({spKey, v});
      if (!sparkCache.restore(spKey, 0, base - h - 2, W(), h + 4)) {
        uint16_t grad[64];  // degradê da área: forte no topo do gráfico, some na base
        for (int k = 0; k <= h; k++) grad[k] = __builtin_bswap16(blend(BG, ACCENT, 0.35f * (h - k) / h));
        auto* buf = static_cast<uint16_t*>(fb().getBuffer());
        int lo, hi;
        gfx::rows(lo, hi);
        for (int i = 1; i <= last; i++)
          for (int x = px(i - 1); x < px(i); x++) {
            int y = py(i - 1) + (py(i) - py(i - 1)) * (x - px(i - 1)) / max(1, px(i) - px(i - 1));
            for (int yy = max(y, lo); yy < min(base, hi); yy++) buf[yy * W() + x] = grad[yy - (base - h)];
          }
        for (int i = 1; i <= last; i++) gfx::wideLine(px(i - 1), py(i - 1), px(i), py(i), 1.0f, MUTED);
        sparkCache.store(spKey);
      }
      fb().fillSmoothCircle(px(last), py(last), u(4) + (1 - min(glow, 1.f)) * u(3), ACCENT);
      text(fmtTokens(sp[n - 1]).c_str(), x0 + cw, label, gfx::SMALL, DIM, top_right);
    }
    text("última hora", x0, label, gfx::SMALL, DIM);
  }
  if (other) {
    snprintf(buf, sizeof buf, "esgotado · trocar para %s ›", other);
    const int sx = land ? x0 : M(), maxW = land ? cw : W() - 2 * M();
    const String t = fit(buf, gfx::SMALL, maxW - u(24));
    sugW = land ? cw : gfx::textWidth(t.c_str(), gfx::SMALL) + u(24), sugX = land ? sx : cx - sugW / 2;
    fb().fillSmoothRoundRect(sugX, sugY, sugW, sugH, sugH / 2, RED);
    text(t.c_str(), sugX + sugW / 2, sugY + sugH / 2, gfx::SMALL, BG, middle_center, RED);
  }
  dots(page);
}

struct Cell {  // um bloco de layout: canto superior esquerdo e largura
  int x, y, w;
};

// Onde fica o modelo i: em pé, um por linha; deitada, em duas colunas (3 modelos em 124 px cada não
// caberiam em 320 de altura).
Cell modelCell(int i) {
  if (!L()) return {M(), u(68) + i * u(124), W() - 2 * M()};
  const int gap = u(16), w = (W() - 2 * M() - gap) / 2;
  return {M() + (i % 2) * (w + gap), u(64) + (i / 2) * u(124), w};
}

void drawModels(bool live, int page) {
  char buf[64];
  gfx::clear();
  header(live, "modelos hoje");
  JsonArrayConst mods = app::frame["mods"];
  if (mods.size() == 0) text("sem uso hoje", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
  int i = 0;
  for (JsonArrayConst m : mods) {
    const Cell c = modelCell(i);
    const int x = c.x, y = c.y, w = c.w;
    const char* name = m[0] | "?";
    int share = m[2];
    text(name, x, y, gfx::TITLE, TEXT);
    snprintf(buf, sizeof buf, "%d%%", share);
    text(buf, x + w, y, gfx::TITLE, i == 0 ? ACCENT : MUTED, top_right);
    const int barY = y + below(gfx::TITLE, 30), y1 = barY + u(18);
    gfx::bar(x, barY, w, u(8), anim::entrance(i) * share / 100, i == 0 ? ACCENT : MUTED);
    snprintf(buf, sizeof buf, "%s tokens", (const char*)m[1]);
    text(buf, x, y1, gfx::LABEL, MUTED);
    snprintf(buf, sizeof buf, "entrada %s · saída %s", (const char*)m[3], (const char*)m[4]);
    const int y2 = y1 + below(gfx::LABEL, 22);
    text(fit(buf, gfx::SMALL, w).c_str(), x, y2, gfx::SMALL, DIM);
    snprintf(buf, sizeof buf, "cache lido %s · escrito %s", (const char*)m[5], (const char*)m[6]);
    text(fit(buf, gfx::SMALL, w).c_str(), x, y2 + below(gfx::SMALL, 18), gfx::SMALL, DIM);
    i++;
  }
  dots(page);
}

// Falta instalar o daemon: QR code da página /comecar e o endereço curto. Em pé, QR em cima; deitada, à esquerda.
void drawInstall() {
  gfx::clear();
  if (L()) {
    const int side = gfx::height() - u(48), cx = (M() + side + W()) / 2;
    fb().qrcode(app::START_URL, M(), u(24), side);
    text("falta o tokenpulse", cx, gfx::height() / 2 - u(44), gfx::TITLE, TEXT, middle_center);
    text("no Mac", cx, gfx::height() / 2 - u(44) + below(gfx::TITLE, 26), gfx::TITLE, TEXT, middle_center);
    text("escaneie, ou abra no Mac:", cx, gfx::height() / 2 + u(20), gfx::SMALL, MUTED, middle_center);
    text(app::START_SHORT, cx, gfx::height() / 2 + u(20) + below(gfx::SMALL, 22), gfx::LABEL, ACCENT, middle_center);
    return;
  }
  const int side = min(W() - 2 * M(), gfx::height() - u(170));
  text("falta o tokenpulse no Mac", W() / 2, u(36), gfx::TITLE, TEXT, middle_center);
  fb().qrcode(app::START_URL, (W() - side) / 2, u(64), side);
  const int y = u(64) + side + u(28);
  text("escaneie, ou abra no Mac:", W() / 2, y, gfx::SMALL, MUTED, middle_center);
  text(app::START_SHORT, W() / 2, y + below(gfx::SMALL, 24), gfx::LABEL, ACCENT, middle_center);
}

// Sem dados do Mac: o que está funcionando e o que falta, em ordem, com o que fazer em cada caso.
void drawWaiting() {
  char buf[64];
  if (hostlink::needsInstall()) return drawInstall();
  gfx::clear();
  // em pé, tudo centrado em coluna; deitada, mascote e título à esquerda e os passos à direita
  const bool land = L();
  const int lw = land ? W() * 2 / 5 : W(), lcx = lw / 2, px = u(8);
  const int sx = land ? lw + u(8) : M(), sw = W() - M() - sx;  // coluna dos passos
  mascot::draw(lcx - mascot::W * px / 2, u(land ? 76 : 70), px, mascot::IDLE, BG);
  text("sem dados do Mac", lcx, u(land ? 178 : 172), gfx::TITLE, TEXT, middle_center);
  const bool usb = hostlink::usbPlugged(), port = usb && hostlink::portOpen();
  const uint32_t id = hostlink::lastIdMs();
  const bool daemon = port && id && millis() - id < 15000;
  if (app::lastFrameMs) snprintf(buf, sizeof buf, "último dado há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "nenhum dado desde que a placa ligou");
  String sub2, sub1 = wrap2(buf, gfx::SMALL, lw - 2 * M(), sub2);
  text(sub1.c_str(), lcx, u(land ? 204 : 198), gfx::SMALL, MUTED, middle_center);
  if (sub2.length()) text(sub2.c_str(), lcx, u(land ? 204 : 198) + below(gfx::SMALL, 18), gfx::SMALL, MUTED, middle_center);

  struct Step {
    const char *name, *ok, *wait;
    bool done;
  } steps[] = {
      {"cabo USB", "o Mac reconheceu a placa", "gire o plugue USB-C ou troque o cabo", usb},
      {"daemon tokend", "abriu a porta e se apresentou", "rode: launchctl kickstart -k gui/$UID/com.gilsondev.tokenpulse", daemon},
      {"dados", "chegando", "esperando o primeiro envio", false},
  };
  int y = u(land ? 64 : 236);
  bool blocked = false;
  for (auto& st : steps) {
    const bool cur = !st.done && !blocked;  // o primeiro passo que falta é o que importa agora
    const uint16_t c = st.done ? GREEN : cur ? AMBER : DIM;
    stepIcon(sx + u(10), y + u(10), st.done ? 0 : cur ? 1 : 2);
    text(st.name, sx + u(32), y, gfx::LABEL, st.done || cur ? TEXT : DIM);
    if (st.done || cur) {  // a dica pode ser longa: até duas linhas na largura disponível
      const int maxW = sw - u(32), y1 = y + below(gfx::LABEL, 22);
      String l2, l1 = wrap2(st.done ? st.ok : st.wait, gfx::SMALL, maxW, l2);
      text(fit(l1.c_str(), gfx::SMALL, maxW).c_str(), sx + u(32), y1, gfx::SMALL, c);
      if (l2.length()) text(fit(l2.c_str(), gfx::SMALL, maxW).c_str(), sx + u(32), y1 + below(gfx::SMALL, 18), gfx::SMALL, c);
    }
    if (!st.done) blocked = true;
    y += u(land ? 76 : 70);
  }
  snprintf(buf, sizeof buf, "%s · %s", board::NAME, hostlink::firmware());
  text(buf, W() / 2, gfx::height() - u(16), gfx::SMALL, DIM, middle_center);
}

// Cartões em 2 colunas em pé e 3 deitada. As sessões são sempre 6 por página (3 × 2 ou 2 × 3).
int COLS() { return L() ? 3 : 2; }
int GRID_Y() { return u(62); }
int CARD_GAP() { return u(8); }
int CARD_H() { return L() ? (gfx::height() - GRID_Y() - u(44) - CARD_GAP()) / 2 : u(120); }  // deitada: sobra a linha da dica
int CARD_W() { return (W() - 2 * M() - (COLS() - 1) * CARD_GAP()) / COLS(); }
int focusReqIdx = -1;  // cartão tocado por último, para piscar a borda enquanto o Mac responde
uint32_t focusReqMs = 0;

// Cartão com borda laranja quando é o que está em foco no Mac (painel ativo do herdr).
void card(int x, int y, int w, int h, int r, uint16_t fill, bool focused) {
  if (focused) {
    fb().fillSmoothRoundRect(x, y, w, h, r, ACCENT);
    fb().fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, max(1, r - 2), fill);
  } else {
    fb().fillSmoothRoundRect(x, y, w, h, r, fill);
  }
}

// Índices em sess.rows das sessões do workspace `ws`, na ordem do daemon (já por estado); devolve quantas.
int sessionsOf(int ws, int* ids, int max) {
  JsonArrayConst rows = app::frame["sess"]["rows"];
  int n = 0;
  for (int r = 0; r < (int)rows.size() && n < max; r++)
    if ((int)rows[r][5] == ws) ids[n++] = r;
  return n;
}
int sessFrom(int scroll, int n) { return min(scroll * 6, max(0, n - 1) / 6 * 6); }

// Primeiro workspace à mostra: o desenho e o toque usam a mesma conta (a lista pode encolher).
int wsFrom(int scroll, int n) { return min(scroll * wsPerPage(), max(0, n - 1) / wsPerPage() * wsPerPage()); }

// Lista de workspaces do herdr: cartões 2 × 5 (deitada, 3 × 3) com o nome e quantas sessões estão em cada estado.
int WS_Y() { return u(60); }
int WS_H() { return u(L() ? 64 : 70); }
int WS_GAP() { return u(7); }

void drawWorkspaces(bool live, int page, int scroll) {
  char buf[48];
  JsonArrayConst ws = app::frame["sess"]["ws"];
  const int n = ws.size(), from = wsFrom(scroll, n);
  snprintf(buf, sizeof buf, "sessões · %d", (int)(app::frame["sess"]["n"] | 0));
  header(live, buf);
  if (n == 0) {
    const int px = u(6);
    mascot::draw(W() / 2 - mascot::W * px / 2, u(180), px, mascot::IDLE, BG);
    text("nenhuma sessão aberta", W() / 2, u(260), gfx::LABEL, DIM, middle_center);
  }
  for (int i = from; i < min(from + wsPerPage(), n); i++) {
    JsonArrayConst w = ws[i];
    const int k = i - from, x = M() + (k % COLS()) * (CARD_W() + CARD_GAP()), y = WS_Y() + (k / COLS()) * (WS_H() + WS_GAP());
    const int total = w[1], work = w[2], block = w[3], turn = w[4];
    const uint16_t cardBg = block ? blend(BG, RED, 0.16f) : blend(BG, TRACK, work || turn ? 0.6f : 0.35f);
    const float in = anim::entrance(k);
    card(x, y + (int)((1 - in) * u(12)), CARD_W(), WS_H(), u(12), cardBg, (int)(w[5] | 0));
    if (in < 0.9f) continue;
    text(fit(w[0], gfx::LABEL, CARD_W() - u(24)).c_str(), x + u(12), y + u(10), gfx::LABEL, TEXT, top_left, cardBg);
    snprintf(buf, sizeof buf, total == 1 ? "1 sessão" : "%d sessões", total);
    int cx = x + u(12);
    const int ly = y + u(10) + below(gfx::LABEL, 30);  // linha da contagem
    if (!work && !block && !turn) {
      text(buf, cx, ly, gfx::SMALL, DIM, top_left, cardBg);
      continue;
    }
    // um ponto colorido com a contagem para cada estado que importa
    const struct { int count; uint16_t c; } chips[] = {{block, RED}, {work, GREEN}, {turn, ACCENT}};
    for (auto& ch : chips) {
      if (!ch.count) continue;
      const int d = max(3, u(4));
      fb().fillSmoothCircle(cx + d, ly + gfx::lineHeight(gfx::SMALL) / 2 - 1, d, ch.c);
      snprintf(buf, sizeof buf, "%d", ch.count);
      text(buf, cx + 3 * d, ly, gfx::SMALL, ch.c, top_left, cardBg);
      cx += 3 * d + gfx::textWidth(buf, gfx::SMALL) + u(10);
    }
  }
  if (n > wsPerPage()) text("arraste para cima: mais grupos", W() / 2, gfx::height() - u(30), gfx::SMALL, DIM, middle_center);
}

int wsPerPage() { return L() ? 9 : 10; }

int workspaceAt(int x, int y, int scroll) {
  const int n = app::frame["sess"]["ws"].size(), row = (y - WS_Y()) / (WS_H() + WS_GAP()), col = (x - M()) / (CARD_W() + CARD_GAP());
  const int i = wsFrom(scroll, n) + row * COLS() + col;
  return y >= WS_Y() && row < wsPerPage() / COLS() && col >= 0 && col < COLS() && i < n ? i : -1;
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
  int ids[48];
  const int shown = sessionsOf(ws, ids, 48), from = sessFrom(scroll, shown);
  JsonArrayConst w = app::frame["sess"]["ws"][ws];
  if (shown > 6) snprintf(buf, sizeof buf, "‹ %s · %d–%d de %d", (const char*)(w[0] | "?"), from + 1, min(from + 6, shown), (int)(w[1] | 0));
  else snprintf(buf, sizeof buf, "‹ %s", (const char*)(w[0] | "?"));
  header(live, buf);
  if (shown > 6) text("arraste para cima: mais sessões", W() / 2, gfx::height() - u(30), gfx::SMALL, DIM, middle_center);
  int i = 0;
  for (int idx = from; idx < min(from + 6, shown); idx++) {
    JsonArrayConst row = rows[ids[idx]];
    const int cw = CARD_W(), x = M() + (i % COLS()) * (cw + CARD_GAP()), y = GRID_Y() + (i / COLS()) * (CARD_H() + CARD_GAP());
    const char* state = row[2] | "";
    const mascot::Mood mood = !strcmp(state, "work")    ? mascot::WORK
                              : !strcmp(state, "turn")  ? mascot::TURN
                              : !strcmp(state, "block") ? mascot::BLOCK
                                                        : mascot::IDLE;
    const uint16_t cardBg = mood == mascot::BLOCK ? blend(BG, RED, 0.16f) : blend(BG, TRACK, mood == mascot::IDLE ? 0.35f : 0.6f);
    // tocado agora há pouco: a borda pisca enquanto o Mac traz a sessão para a frente
    const bool asked = focusReqIdx == ids[idx] && anim::since(focusReqMs, 900) < 1;
    const bool focused = (int)(row[7] | 0) || (asked && (millis() - focusReqMs) / 150 % 2);
    const float in = anim::entrance(i);
    card(x, y + (int)((1 - in) * u(16)), cw, CARD_H(), u(14), cardBg, focused);
    if (in < 0.9f) {
      i++;
      continue;
    }
    const int pad = u(12), statusY = y + CARD_H() - max(u(20), gfx::lineHeight(gfx::SMALL) + 2);
    mascot::draw(x + pad, y + u(14), max(2, u(4)), mood, cardBg, i);
    const char* origin = row[1] | "CLI";
    badge(origin, x + cw - u(10), y + u(12), !strcmp(origin, "Desk") ? ACCENT : GREEN, cardBg);
    text(row[3], x + cw - u(10), y + u(12) + below(gfx::SMALL, 28), gfx::SMALL, DIM, top_right, cardBg);
    // título da sessão em até duas linhas, quebrado nas palavras; se a segunda não couber, uma linha só
    const int maxW = cw - u(24), t1 = y + u(56), t2 = t1 + max(u(19), gfx::lineHeight(gfx::LABEL) - 1);
    String l2, name = wrap2(row[0] | "", gfx::LABEL, maxW, l2);
    if (l2.length() && t2 + gfx::lineHeight(gfx::LABEL) > statusY) name = row[0] | "", l2 = "";
    text(fit(name.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t1, gfx::LABEL, TEXT, top_left, cardBg);
    if (l2.length()) text(fit(l2.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t2, gfx::LABEL, TEXT, top_left, cardBg);
    const int mins = row[4];
    if (mood == mascot::WORK) strcpy(buf, "trabalhando");
    else if (mood == mascot::BLOCK) strcpy(buf, "precisa de você");
    else if (mood == mascot::TURN) snprintf(buf, sizeof buf, mins ? "sua vez · %d min" : "sua vez", mins);
    else if (mins < 0) strcpy(buf, "parada");
    else if (mins >= 120) snprintf(buf, sizeof buf, "parada há %dh", mins / 60);
    else snprintf(buf, sizeof buf, "parada há %d min", mins);
    const uint16_t sc = mood == mascot::WORK ? GREEN : mood == mascot::BLOCK ? RED : mood == mascot::TURN ? ACCENT : DIM;
    text(buf, x + pad, statusY, gfx::SMALL, sc, top_left, cardBg);
    i++;
  }
  dots(page);
}

// Toque num cartão de sessão: pede ao Mac para trazer a sessão (painel do herdr ou Claude Desktop).
bool sessionTap(int x, int y, int ws, int scroll) {
  int ids[48];
  const int shown = sessionsOf(ws, ids, 48), row = (y - GRID_Y()) / (CARD_H() + CARD_GAP()), col = (x - M()) / (CARD_W() + CARD_GAP());
  const int i = sessFrom(scroll, shown) + row * COLS() + col;
  if (y < GRID_Y() || row >= 6 / COLS() || col < 0 || col >= COLS() || i >= shown) return false;
  const char* sid = app::frame["sess"]["rows"][ids[i]][6] | "";
  if (!*sid) return false;
  Serial.printf("ACTION focus %s\n", sid);
  focusReqIdx = ids[i], focusReqMs = millis();
  return true;
}

int modelAt(int x, int y) {
  for (int i = 0; i < (int)app::frame["mods"].size(); i++) {
    const Cell c = modelCell(i);
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + u(118)) return i;
  }
  return -1;
}

// Detalhe de um modelo: tokens e custo de hoje, de onde vêm (entrada, saída, cache) e quais projetos usaram.
void drawModelDetail(bool live, int page, int idx) {
  JsonArrayConst m = app::frame["mods"][idx];
  if (m.isNull()) return drawModels(live, page);
  char buf[64];
  gfx::clear();
  header(live, m[0]);
  // em pé, tudo numa coluna; deitada, composição à esquerda e projetos à direita
  const bool land = L();
  const int gap = u(24), lw = land ? (W() - 2 * M() - gap) / 2 : W() - 2 * M(), rx = land ? M() + lw + gap : M();
  text(m[1], M(), u(68), gfx::NUM, TEXT);
  snprintf(buf, sizeof buf, "%d%%", (int)m[2]);
  text(buf, M() + lw, u(72), gfx::TITLE, ACCENT, top_right);
  if (settings::v.hideCost) strcpy(buf, "tokens hoje");
  else snprintf(buf, sizeof buf, "tokens hoje · %s na API", (const char*)(m[7] | "--"));
  text(fit(buf, gfx::SMALL, lw).c_str(), M(), u(68) + below(gfx::NUM, 38), gfx::SMALL, MUTED);

  // composição numa barra empilhada
  const char* names[] = {"entrada", "saída", "cache lido", "cache escrito"};
  const uint16_t cols[] = {blend(ACCENT, TEXT, 0.5f), ACCENT, GREEN, AMBER};
  float v[4], sum = 0;
  for (int k = 0; k < 4; k++) sum += v[k] = parseTokens(m[3 + k]);
  const int bx = M(), by = u(140), bw = lw, bh = u(14), r = bh / 2;
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
    const int y = u(168) + k * below(gfx::LABEL, 26), d = max(3, u(5));
    fb().fillSmoothCircle(M() + d, y + gfx::lineHeight(gfx::LABEL) / 2 - 1, d, cols[k]);
    text(names[k], M() + u(18), y, gfx::LABEL, MUTED);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)m[3 + k], sum > 0 ? (int)lroundf(100 * v[k] / sum) : 0);
    // coluna estreita (média deitada): sem espaço para o %, fica só o número
    if (u(18) + gfx::textWidth(names[k], gfx::LABEL) + u(10) + gfx::textWidth(buf, gfx::LABEL) > lw) strcpy(buf, m[3 + k] | "");
    text(buf, M() + lw, y, gfx::LABEL, TEXT, top_right);
  }

  const int py0 = land ? u(68) : u(168) + 4 * below(gfx::LABEL, 26) + u(12), pw = W() - M() - rx;
  text(gfx::textWidth("projetos que mais usaram", gfx::SMALL) <= pw ? "projetos que mais usaram" : "projetos", rx, py0, gfx::SMALL, DIM);
  JsonArrayConst projs = m[8];
  float top = 1;
  for (JsonArrayConst p : projs) top = max(top, parseTokens(p[1]));
  int i = 0;
  for (JsonArrayConst p : projs) {
    const int y = py0 + below(gfx::SMALL, 22) + i * max(u(44), gfx::lineHeight(gfx::LABEL) + u(14));
    if (y + gfx::lineHeight(gfx::LABEL) + u(10) > gfx::height() - u(land ? 24 : 40)) break;  // a dica "toque para voltar" fica livre
    text(fit(p[0], gfx::LABEL, pw - u(70)).c_str(), rx, y, gfx::LABEL, TEXT);
    text(p[1], rx + pw, y, gfx::LABEL, MUTED, top_right);
    gfx::bar(rx, y + below(gfx::LABEL, 24), pw, max(4, u(6)), anim::entrance(i + 1) * parseTokens(p[1]) / top, ACCENT);
    i++;
  }
  if (!land) text("toque para voltar", W() / 2, gfx::height() - u(34), gfx::SMALL, DIM, middle_center);
  dots(page);
}

// Histórico: tokens por dia nos últimos 14 dias, a barra tocada (ou hoje) em destaque.
// deitada o gráfico fica mais largo e mais baixo, e o total e a média dividem uma linha
int CH_Y() { return u(L() ? 128 : 150); }
int CH_H() { return u(L() ? 112 : 200); }
int gap() { return max(3, u(5)); }  // entre as barras

int historyBarAt(int x, int y) {
  const int n = app::frame["hist"].size();
  if (!n || y < CH_Y() - u(20) || y > CH_Y() + CH_H() + u(30)) return -1;
  const int slot = (W() - 2 * M() + gap()) / n;
  return constrain((x - M()) / slot, 0, n - 1);
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
  text(buf, M(), u(66), gfx::SMALL, MUTED);
  snprintf(buf, sizeof buf, "%.1fM", (float)s[2]);
  String tok = buf;
  tok.replace('.', ',');
  const int ny = u(66) + below(gfx::SMALL, 18);
  text(tok.c_str(), M(), ny, gfx::NUM, TEXT);
  const bool cost = !settings::v.hideCost;
  snprintf(buf, sizeof buf, "US$ %d", (int)s[3]);
  if (cost) text(buf, W() - M(), ny, gfx::NUM, ACCENT, top_right);

  const int slot = (W() - 2 * M() + gap()) / n, bw = slot - gap(), chY = CH_Y(), chH = CH_H();
  fb().drawFastHLine(M(), chY + chH, W() - 2 * M(), TRACK);
  for (int i = 0; i < n; i++) {
    JsonArrayConst d = hist[i];
    const int x = M() + i * slot, h = max(2, (int)(chH * anim::entrance(i / 2) * (float)d[2] / peak));
    const bool on = i == sel;
    fb().fillSmoothRoundRect(x, chY + chH - h, bw, h, min(bw / 2, 5), on ? ACCENT : blend(BG, MUTED, i == n - 1 ? 0.7f : 0.45f));
    char day[2] = {(d[0] | "?")[0], 0};
    text(day, x + bw / 2, chY + chH + u(14), gfx::SMALL, on ? TEXT : DIM, middle_center);
  }

  if (sumTok >= 1000) snprintf(buf, sizeof buf, "14 dias: %.1fB tokens", sumTok / 1000);
  else snprintf(buf, sizeof buf, "14 dias: %.0fM tokens", sumTok);
  String total = buf;
  total.replace('.', ',');
  if (cost) total += " · US$ " + String(sumCost);
  const int ty = chY + chH + u(34);
  text(total.c_str(), M(), ty, gfx::SMALL, MUTED);
  snprintf(buf, sizeof buf, "média de US$ %d por dia", sumCost / n);
  const bool oneLine = gfx::textWidth(total.c_str(), gfx::SMALL) + u(16) + gfx::textWidth(buf, gfx::SMALL) <= W() - 2 * M();
  if (cost && L()) {  // deitada só há uma linha: a média entra se couber ao lado do total
    if (oneLine) text(buf, W() - M(), ty, gfx::SMALL, MUTED, top_right);
  } else if (cost) {
    snprintf(buf, sizeof buf, "média de US$ %d por dia", sumCost / n);
    text(buf, M(), ty + below(gfx::SMALL, 20), gfx::SMALL, MUTED);
    text("custo equivalente na API", M(), ty + below(gfx::SMALL, 20) + below(gfx::SMALL, 24), gfx::SMALL, DIM);
  }
  dots(page);
}

// Ajustes: o nome em cima e as opções num controle segmentado embaixo. Em pé, um por linha na largura
// toda; deitada, os de 4 opções na largura toda e os outros em duas colunas, para caber em 320 px.
namespace {
struct Row {
  const char* name;
  uint8_t* value;
  const char* opts[4];
  uint8_t count;
};
int SET_Y() { return u(L() ? 58 : 62); }
int ROW_H() { return u(L() ? 62 : 68); }
int SEG_TOP() { return below(gfx::SMALL, 20); }
int SEG_H() { return u(38); }

Row settingRows[] = {
    {"Brilho", &settings::v.bright, {"25%", "50%", "75%", "100%"}, 4},
    {"Escurecer quando parada", &settings::v.idle, {"5 min", "10 min", "30 min", "nunca"}, 4},
    {"Tema", &settings::v.theme, {"auto", "claro", "escuro"}, 3},
    {"Tela", &settings::v.orient, {"vertical", "horizontal"}, 2},
    {"Custo em US$", &settings::v.hideCost, {"mostrar", "ocultar"}, 2},
};
constexpr int ROWS = sizeof settingRows / sizeof *settingRows;

// Onde fica o item i (o nome em y, o controle em y + SEG_TOP). O item ROWS são os botões Conta e Sobre.
Cell cell(int i) {
  const int full = W() - 2 * M();
  if (!L()) return {M(), SET_Y() + i * ROW_H() + (i == ROWS ? u(4) : 0), full};
  const int gap = u(12), half = (full - gap) / 2;
  static const int8_t ROW[] = {0, 1, 2, 2, 3, 3}, COL[] = {-1, -1, 0, 1, 0, 1};  // -1: largura toda
  const int y = SET_Y() + ROW[i] * ROW_H();
  return COL[i] < 0 ? Cell{M(), y, full} : Cell{M() + COL[i] * (half + gap), y, half};
}
}  // namespace

void drawSettings(bool live) {
  gfx::clear();
  header(live, "ajustes");
  const uint16_t track = blend(BG, TRACK, 0.7f);
  for (int i = 0; i < ROWS; i++) {
    const Row& r = settingRows[i];
    const Cell c = cell(i);
    const int sy = c.y + SEG_TOP(), sh = SEG_H();
    text(r.name, c.x, c.y, gfx::SMALL, MUTED);
    fb().fillSmoothRoundRect(c.x, sy, c.w, sh, sh / 2, track);
    for (int k = 0; k < r.count; k++) {
      // bordas calculadas do mesmo jeito para todos: segmentos de largura igual, texto no centro
      const int x0 = c.x + c.w * k / r.count, x1 = c.x + c.w * (k + 1) / r.count;
      const bool on = *r.value == k;
      if (on) fb().fillSmoothRoundRect(x0 + 3, sy + 3, x1 - x0 - 6, sh - 6, (sh - 6) / 2, ACCENT);
      text(r.opts[k], (x0 + x1) / 2, sy + sh / 2, gfx::SMALL, on ? BG : TEXT, middle_center, on ? ACCENT : track);
    }
  }
  // dois botões lado a lado: a conta (org) e o Sobre. Deitada eles ficam na altura dos controles.
  const Cell c = cell(ROWS);
  const uint16_t btn = blend(BG, TRACK, 0.7f);
  const int half = (c.w - u(8)) / 2, ay = L() ? c.y + SEG_TOP() : c.y, bh = L() ? SEG_H() : u(44);
  const char* org = app::frame["org"] | "Conta";
  fb().fillSmoothRoundRect(c.x, ay, half, bh, u(10), btn);
  text(gfx::textWidth(org, gfx::LABEL) <= half - u(40) ? org : "Conta", c.x + u(14), ay + bh / 2, gfx::LABEL, TEXT, middle_left, btn);
  text("›", c.x + half - u(12), ay + bh / 2, gfx::TITLE, MUTED, middle_right, btn);
  fb().fillSmoothRoundRect(c.x + half + u(8), ay, half, bh, u(10), btn);
  text("Sobre", c.x + half + u(22), ay + bh / 2, gfx::LABEL, TEXT, middle_left, btn);
  text("›", c.x + c.w - u(12), ay + bh / 2, gfx::TITLE, MUTED, middle_right, btn);
}

// Toque nos ajustes: 0 trocou um ajuste, 1 abriu o Sobre, 2 abriu a Conta, -1 nada aqui
int settingsTap(int x, int y) {
  for (int i = 0; i <= ROWS; i++) {
    const Cell c = cell(i);
    const bool buttons = i == ROWS;
    const bool tall = buttons && !L();  // em pé, os botões ficam numa linha própria, sem nome em cima
    const int top = tall ? c.y : c.y + SEG_TOP();
    const int h = tall ? u(44) : SEG_H();
    if (x < c.x || x >= c.x + c.w || y < top - 6 || y > top + h + 4) continue;
    if (buttons) return x < c.x + c.w / 2 ? 2 : 1;
    Row& r = settingRows[i];
    const int k = min<int>((x - c.x) * r.count / c.w, r.count - 1);
    const bool turned = r.value == &settings::v.orient && *r.value != k;
    *r.value = k;
    settings::save();
    if (turned) gfx::setRotation(k ? 1 : 0), anim::reset();
    return 0;
  }
  return -1;
}

// Sobre: o que a placa sabe de si mesma, e reiniciar com toque duplo de confirmação.
// Deitada, os 8 itens vão em 2 colunas de 4, rótulo em cima e valor embaixo; os botões vêm logo depois.
int aboutStep() { return L() || gfx::ui() == gfx::LARGE ? max(u(39), gfx::lineHeight(gfx::SMALL) + gfx::lineHeight(gfx::LABEL)) : below(gfx::LABEL, 26) + u(3); }
int RESTART_Y() { return L() ? max(u(232), u(66) + 4 * aboutStep() + u(6)) : u(384); }

void drawAbout(bool live, bool armed) {
  char buf[64];
  gfx::clear();
  header(live, "sobre");
  const uint32_t up = millis() / 60000;
  if (up >= 60) snprintf(buf, sizeof buf, "%luh%02lu", up / 60, up % 60);
  else snprintf(buf, sizeof buf, "%lu min", up);
  String upTxt = buf;
  // livre de quanto: só o livre ("6168 KB") parecia memória acabando
  snprintf(buf, sizeof buf, "%.1f de %.1f MB livres", ESP.getFreePsram() / 1048576.f, ESP.getPsramSize() / 1048576.f);
  String psram = psramFound() ? String(buf) : String("não tem");
  psram.replace('.', ',');
  // o perfil da placa: o que ela tem de hardware além da tela
  const auto& pf = board::PROFILE;
  String hw = String(pf.touch ? "touch · " : "") + (pf.imu ? "giro · " : "") + (pf.led ? "LED · " : "") + (pf.button ? "botão · " : "");
  hw = hw.length() ? hw.substring(0, hw.length() - 4) : String("só a tela");
  snprintf(buf, sizeof buf, "%u de %u KB livres", ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);
  String ram = buf;
  if (live) strcpy(buf, "ao vivo pela USB");
  else if (app::lastFrameMs) snprintf(buf, sizeof buf, "sem dados há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "esperando o Mac");
  String link = buf;
  snprintf(buf, sizeof buf, "atualizado há %d min", app::dataAge() / 60);
  String pct = app::stale() ? String(buf) : String("em dia");
  const char* items[][2] = {
      {"placa", board::NAME},          {"firmware", hostlink::firmware()}, {"ligada há", upTxt.c_str()},
      {"PSRAM", psram.c_str()}, {"RAM interna", ram.c_str()},  {"recursos", hw.c_str()},
      {"conexão", link.c_str()},          {"% de uso", pct.c_str()},
  };
  // tela grande: rótulo em cima, valor embaixo. Na média em pé 8 pares não cabem empilhados: um por linha.
  // Deitada: empilhados, em 2 colunas de 4.
  const bool stacked = L() || gfx::ui() == gfx::LARGE;
  const int cols = L() ? 2 : 1, colW = (W() - 2 * M() - (cols - 1) * u(16)) / cols, step = aboutStep();
  int k = 0;
  for (auto& it : items) {
    const int x = M() + (k / 4 % cols) * (colW + u(16)), y = u(66) + (cols == 2 ? k % 4 : k) * step;
    text(it[0], x, y, gfx::SMALL, DIM);
    if (stacked) text(fit(it[1], gfx::LABEL, colW).c_str(), x, y + below(gfx::SMALL, 18), gfx::LABEL, TEXT);
    else text(fit(it[1], gfx::LABEL, W() - 2 * M() - u(110)).c_str(), W() - M(), y, gfx::LABEL, TEXT, top_right);
    k++;
  }
  // touch resistivo: calibrar o toque divide a linha com o reiniciar
  const bool cal = board::PROFILE.calibrate;
  const int bw = cal ? (W() - 2 * M() - u(8)) / 2 : W() - 2 * M(), rx = W() - M() - bw, by = RESTART_Y(), bh = u(48);
  const uint16_t c = armed ? RED : blend(BG, TRACK, 0.7f);
  if (cal) {
    const uint16_t cb = blend(BG, TRACK, 0.7f);
    fb().fillSmoothRoundRect(M(), by, bw, bh, u(12), cb);
    text("calibrar o toque", M() + bw / 2, by + bh / 2, gfx::LABEL, TEXT, middle_center, cb);
  }
  fb().fillSmoothRoundRect(rx, by, bw, bh, u(12), c);
  const char* label = armed ? (cal ? "toque de novo" : "toque de novo para reiniciar") : "reiniciar a placa";
  text(label, rx + bw / 2, by + bh / 2, gfx::LABEL, armed ? BG : TEXT, middle_center, c);
  // a dica só entra se couber embaixo dos botões (na média deitada não cabe)
  if (by + bh + gfx::lineHeight(gfx::SMALL) < gfx::height() - u(8))
    text("toque fora para voltar aos ajustes", W() / 2, gfx::height() - u(22), gfx::SMALL, DIM, middle_center);
}

int aboutButtonAt(int x, int y) {
  if (y < RESTART_Y() || y >= RESTART_Y() + u(48)) return 0;
  return board::PROFILE.calibrate && x < W() / 2 ? 2 : 1;
}
bool gearAt(int x, int y) { return x >= W() - u(64) && y < u(52); }
bool orgTitleAt(int x, int y) { return x < W() / 2 && y < u(30); }
bool suggestionAt(int x, int y) { return sugW && x >= sugX && x < sugX + sugW && y >= sugY && y < sugY + sugH; }
void setGearActive(bool on) { gearActive = on; }

// Conta: a org atual, as contas que o cswap conhece e o login de uma org nova. A troca roda no Mac
// (cswap ou o login do Claude Code no navegador); a placa só pede e acompanha.
// Em pé, tudo numa coluna. Deitada, a org atual, o status e "entrar em outra org" à esquerda, e a lista
// de orgs à direita.
int ACC_LIST_Y() { return u(L() ? 86 : 200); }
int ACC_ROW() { return u(62); }
int ACC_NEW_Y() { return u(L() ? 212 : 392); }
struct AccCols {
  int lw, rx, rw;  // largura da coluna esquerda; início e largura da lista
};
AccCols accCols() {
  if (!L()) return {W() - 2 * M(), M(), W() - 2 * M()};
  const int gap = u(24), lw = (W() - 2 * M() - gap) / 2;
  return {lw, M() + lw + gap, W() - 2 * M() - lw - gap};
}
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
  const AccCols ac = accCols();
  const int lw = ac.lw, rx = ac.rx, rw = ac.rw;
  text("org atual", M(), u(66), gfx::SMALL, DIM);
  text(app::frame["org"] | "--", M(), u(66) + below(gfx::SMALL, 18), gfx::NUM, TEXT);

  JsonArrayConst act = app::frame["act"];
  const char* st = act[0] | "";
  const char* target = act[1] | "";
  const uint16_t card = blend(BG, TRACK, 0.6f);
  if (*st) {
    fb().fillSmoothRoundRect(M(), u(124), lw, u(72), u(12), card);
    if (!strcmp(st, "switching")) {
      stepIcon(M() + u(20), u(160), 1);
      snprintf(buf, sizeof buf, "trocando para %s…", target);
      text(fit(buf, gfx::LABEL, lw - u(48)).c_str(), M() + u(38), u(160), gfx::LABEL, TEXT, middle_left, card);
    } else if (!strcmp(st, "login")) {
      // o login acontece no navegador do Mac: a placa mostra em que passo está
      stepIcon(M() + u(20), u(142), 0);
      text(fit("login aberto no navegador do Mac", gfx::SMALL, lw - u(48)).c_str(), M() + u(38), u(142), gfx::SMALL, TEXT, middle_left, card);
      stepIcon(M() + u(20), u(166), 1);
      snprintf(buf, sizeof buf, "escolha a org %s e autorize", *target ? target : "desejada");
      text(fit(buf, gfx::SMALL, lw - u(48)).c_str(), M() + u(38), u(166), gfx::SMALL, AMBER, middle_left, card);
      stepIcon(M() + u(20), u(188), 2);
      text(fit("a placa confirma a troca", gfx::SMALL, lw - u(48)).c_str(), M() + u(38), u(188), gfx::SMALL, DIM, middle_left, card);
    } else if (!strcmp(st, "done")) {
      stepIcon(M() + u(20), u(160), 0);
      snprintf(buf, sizeof buf, "pronto: agora na org %s", (const char*)(app::frame["org"] | "?"));
      text(fit(buf, gfx::LABEL, lw - u(48)).c_str(), M() + u(38), u(160), gfx::LABEL, GREEN, middle_left, card);
    } else {
      snprintf(buf, sizeof buf, "não deu certo: %s", (const char*)(act[2] | "erro"));
      text(fit(buf, gfx::SMALL, lw - u(24)).c_str(), M() + u(14), u(160), gfx::SMALL, RED, middle_left, card);
    }
  }

  text("orgs salvas no cswap", rx, ACC_LIST_Y() - below(gfx::SMALL, 20), gfx::SMALL, DIM);
  JsonArrayConst accts = app::frame["accts"];
  if (accts.size() == 0) text("nenhuma: rode cswap add no Mac", rx, ACC_LIST_Y() + u(6), gfx::SMALL, MUTED);
  int i = 0;
  for (JsonArrayConst a : accts) {
    const int y = ACC_LIST_Y() + i * ACC_ROW(), num = a[0];
    const bool active = (int)a[2], relogin = (int)a[3], arm = armed(num);
    const uint16_t rowBg = arm ? blend(BG, RED, 0.18f) : card;
    fb().fillSmoothRoundRect(rx, y, rw, ACC_ROW() - u(6), u(12), rowBg);
    const int nameW = min(u(150), rw - (active ? u(28) : u(120)));
    const String name = fit(a[1], gfx::LABEL, nameW);
    text(name.c_str(), rx + u(14), y + u(8), gfx::LABEL, TEXT, top_left, rowBg);
    const char* tag = active ? "em uso" : relogin ? "login expirado" : "";
    const int tagX = rx + u(20) + gfx::textWidth(name.c_str(), gfx::LABEL);
    text(fit(tag, gfx::SMALL, rx + rw - (active ? u(14) : u(100)) - tagX).c_str(), tagX, y + u(10), gfx::SMALL, active ? GREEN : AMBER, top_left, rowBg);
    // o último % conhecido dessa org (só a org em uso tem o número ao vivo)
    if (arm) strcpy(buf, "troca todas as sessões");
    else if ((int)a[4] >= 0) snprintf(buf, sizeof buf, "5h %d%% · semana %d%% · %s", (int)a[4], (int)a[5], ago(a[6]).c_str());
    else strcpy(buf, active ? "buscando o %…" : "sem % recente");
    text(fit(buf, gfx::SMALL, active ? rw - u(28) : rw - u(120)).c_str(), rx + u(14), y + u(8) + below(gfx::LABEL, 24), gfx::SMALL,
         arm ? RED : DIM, top_left, rowBg);
    if (!active) {
      const uint16_t b = arm ? RED : ACCENT;
      fb().fillSmoothRoundRect(rx + rw - u(92), y + u(12), u(80), u(30), u(15), b);
      text(arm ? "confirmar" : "trocar", rx + rw - u(52), y + u(27), gfx::SMALL, BG, middle_center, b);
    }
    if (++i == 3) break;
  }
  const bool armNew = armed(0);
  const uint16_t nb = armNew ? blend(BG, RED, 0.18f) : card;
  fb().fillSmoothRoundRect(M(), ACC_NEW_Y(), lw, u(40), u(12), nb);
  const char* newTxt = armNew ? "toque de novo: login troca todas as sessões" : "entrar em outra org";
  text(fit(newTxt, gfx::SMALL, lw - u(16)).c_str(), M() + lw / 2, ACC_NEW_Y() + u(20), gfx::SMALL, armNew ? RED : TEXT, middle_center, nb);
  text("toque fora para fechar", W() / 2, gfx::height() - u(18), gfx::SMALL, DIM, middle_center);
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
  const AccCols ac = accCols();
  if (y >= ACC_NEW_Y() && y < ACC_NEW_Y() + u(40) && x < M() + ac.lw) {
    confirm(0, "ACTION login");
    return true;
  }
  JsonArrayConst accts = app::frame["accts"];
  const int i = (y - ACC_LIST_Y()) / ACC_ROW();
  const bool inList = x >= ac.rx && y >= ACC_LIST_Y() && i < min(3, (int)accts.size());
  // fora da lista: na área do status fica; no resto fecha
  if (!inList) return L() ? x < ac.rx && y >= u(56) : y >= u(56) && y < ACC_LIST_Y();
  if ((int)accts[i][2]) return true;  // a org em uso
  char cmd[32];
  snprintf(cmd, sizeof cmd, "ACTION switch %d", (int)accts[i][0]);
  confirm(accts[i][0], cmd);
  return true;
}

}  // namespace screens::tall
