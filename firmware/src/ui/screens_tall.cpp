#include "screens_tall.h"

#include "../app.h"
#include "../hostlink.h"
#include "../power.h"
#include "../webui.h"
#include "../ble.h"
#include "../sd.h"
#include "../settings.h"
#include "../sound.h"
#include "anim.h"
#include "gfx.h"
#include "flipclock.h"
#include "fonts/inter_giant.h"
#include "mascot.h"
#include "screens.h"

using namespace color;
using gfx::fb;
using gfx::text;

namespace screens::tall {
int pagesOf(int ws);
namespace {

int W() { return gfx::width(); }
bool L() { return gfx::width() > gfx::height(); }  // deitada (Ajustes → Tela → horizontal)
// O layout é desenhado para 320x480 em pé e 480x320 deitado, e escala pela altura: 1 na tela grande, 2/3
// (em pé) ou 3/4 (deitada) na média. As fontes da média não encolhem tanto (12 px no mínimo); onde texto
// empilha, a distância usa below().
float K() { return gfx::height() / (L() ? 320.f : 480.f); }
int u(float v) { return lroundf(v * K()); }
int M() { return u(16); }  // margem
// alvo de toque mínimo: 7,5 mm na tela física (a faixa dos 44 pt dos guias de toque). Com 9 mm, a AMOLED ficava
// com elementos enormes para o tamanho de texto que ela tem
int TAPPX() { return lroundf(7.5f * board::PROFILE.pxPerMm); }
// a fonte de um controle pela altura dele: o texto acompanha o alvo de toque, que cresce nas telas densas
gfx::Font fontFor(int h) {
  return h >= 2 * gfx::lineHeight(gfx::TITLE) ? gfx::TITLE : h >= 2 * gfx::lineHeight(gfx::LABEL) ? gfx::LABEL : gfx::SMALL;
}
int below(gfx::Font f, int gap) { return max(u(gap), gfx::lineHeight(f)); }  // próxima linha de texto

// Pontos de página no rodapé: todos com 6 px de altura no mesmo eixo; a página atual é uma pílula.
// Na lista de workspaces cada página de cartões conta uma página; dentro de um workspace, os pontos são só
// as páginas das sessões dele.
int shownWs = -1, shownSessPage = 0;
void dots(int page) {
  const bool inWs = page == 1 && shownWs >= 0;
  const int lp = pagesOf(-1);
  const int cx = app::frame["cx"].isNull() ? 0 : 1;  // a principal do Codex conta um ponto, depois da do Claude
  const int mc = app::frame["mc"].isNull() ? 0 : 1;  // o Multica, depois da última página das sessões
  const int n = inWs ? pagesOf(shownWs) : PAGES - 1 + lp + cx + mc;
  const int at = inWs                ? shownSessPage
                 : page == 0         ? codexMain
                 : page == 1         ? 1 + cx + (multiShown ? lp : shownSessPage)
                                     : page + lp - 1 + cx + mc;
  const int H = u(6), DOT = u(6), PILL = u(18), GAP = u(8);
  if (n > 12) {  // muitas páginas de sessões: os pontos não cabem na largura, vai o número
    char buf[12];
    snprintf(buf, sizeof buf, "%d/%d", at + 1, n);
    return text(buf, W() / 2, gfx::height() - u(14), gfx::SMALL, DIM, middle_center);
  }
  const int total = PILL + (n - 1) * (DOT + GAP), y = gfx::height() - u(14) - H / 2;
  int x = W() / 2 - total / 2;
  for (int i = 0; i < n; i++) {
    const int w = i == at ? PILL : DOT;
    fb().fillSmoothRoundRect(x, y, w, H, H / 2, i == at ? TEXT : DIM);
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

// Faz o texto caber em `maxW`: primeiro tira trechos inteiros do fim ("a · b · c" vira "a · b"), que meia palavra
// com reticências não diz nada; só um trecho único que ainda não cabe é cortado com reticências.
String fit(const char* s, gfx::Font f, int maxW) {
  String t = s ? s : "";
  while (gfx::textWidth(t.c_str(), f) > maxW && t.lastIndexOf(" · ") > 0) t.remove(t.lastIndexOf(" · "));
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

// Título que não cabe numa linha quebra em duas, em vez de virar reticências. Devolve a altura usada.
int title2(const char* t, int cx, int y, int maxW, uint16_t c, gfx::Font f = gfx::TITLE) {
  String l2, l1 = wrap2(t, f, maxW, l2);
  const int lh = gfx::lineHeight(f) + u(4);
  if (!l2.length()) return text(l1.c_str(), cx, y, f, c, top_center), lh;
  text(l1.c_str(), cx, y, f, c, top_center);
  text(l2.c_str(), cx, y + lh, f, c, top_center);
  return 2 * lh;
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

// do tamanho dos números do relógio, ao lado dele e na mesma linha de centro
int GEAR_R() { return max(u(10), gfx::lineHeight(gfx::TITLE) * 2 / 5); }
void gear(int cx, int cy, int r, uint16_t c) {
  for (int k = 0; k < 8; k++) {
    const float a = k * TAU / 8;
    gfx::wideLine(cx + cosf(a) * r * 0.55f, cy + sinf(a) * r * 0.55f, cx + cosf(a) * r * 0.95f, cy + sinf(a) * r * 0.95f,
                      max(1.5f, r * 0.24f), c);
  }
  fb().fillSmoothCircle(cx, cy, r * 0.68f, c);
  fb().fillSmoothCircle(cx, cy, r * 0.28f, BG);
}

// Cabeçalho: org e relógio; embaixo modelo, ponto de status e sessões abertas.
// Energia, à esquerda do relógio: bateria com o nível e o % (raio quando ligada no USB, nível verde carregando,
// vermelho abaixo de 15%), ou um plugue quando a placa roda só pelo USB, sem bateria. Devolve a largura usada (0 sem PMU).
int powerIcon(int right, int cy, bool draw) {
  board::PowerState s;
  if (!board::PROFILE.battery || !board::powerState(s)) return 0;
  const int bw = u(22), bh = u(12), cap = u(2), gap = u(6);
  if (!s.battery) {  // só USB: o plugue
    const int pw = u(16);
    if (draw) {
      const int x = right - pw;
      fb().fillSmoothRoundRect(x, cy - u(5), u(10), u(10), u(2), MUTED);         // o corpo
      fb().fillRect(x + u(10), cy - u(4), u(4), u(2), MUTED);                    // os dois pinos
      fb().fillRect(x + u(10), cy + u(2), u(4), u(2), MUTED);
      fb().fillRect(x - u(4), cy - u(1), u(4), u(2), MUTED);                     // o cabo
    }
    return pw + u(4);
  }
  char pct[8] = "";
  if (s.pct >= 0) snprintf(pct, sizeof pct, "%d%%", s.pct);
  const int tw = pct[0] ? gfx::textWidth(pct, gfx::SMALL) + gap : 0;
  if (draw) {
    const int x = right - bw - cap;
    const uint16_t c = s.charging ? GREEN : s.pct >= 0 && s.pct < 15 ? RED : TEXT;
    fb().drawRoundRect(x, cy - bh / 2, bw, bh, u(3), MUTED);
    fb().fillRect(x + bw, cy - u(3), cap, u(6), MUTED);
    const int fill = (bw - u(4)) * max(0, s.pct) / 100;
    if (fill > 0) fb().fillRoundRect(x + u(2), cy - bh / 2 + u(2), fill, bh - u(4), u(1), c);
    if (s.usb) {  // o raio: ligada no USB (o nível fica verde enquanto carrega)
      const int mx = x + bw / 2;
      fb().fillTriangle(mx + u(1), cy - u(6), mx - u(4), cy + u(1), mx, cy + u(1), BG);
      fb().fillTriangle(mx - u(1), cy + u(6), mx + u(4), cy - u(1), mx, cy - u(1), BG);
      fb().fillTriangle(mx + u(1), cy - u(5), mx - u(3), cy + u(1), mx, cy + u(1), TEXT);
      fb().fillTriangle(mx - u(1), cy + u(5), mx + u(3), cy - u(1), mx, cy - u(1), TEXT);
    }
    if (pct[0]) text(pct, x - gap, cy, gfx::SMALL, s.pct < 15 && !s.usb ? RED : MUTED, middle_right);
  }
  return bw + cap + tw + u(4);
}

// Bluetooth, à esquerda da bateria: o símbolo em cinza ligado à espera do Mac, em azul conectado. Devolve a
// largura usada (0 em placa sem BLE).
int bleIcon(int right, int cy, bool draw) {
#ifdef TOKENPULSE_BLE
  const int h = u(14), w = u(4), x = right - w - u(1), y = cy - h / 2;
  if (draw) {
    const uint16_t c = ble::connected() ? 0x3D9F : MUTED;  // azul do Bluetooth quando conectado
    const float r = max(1.f, u(1) * 0.8f);
    gfx::wideLine(x - w, y + h / 4, x + w, y + 3 * h / 4, r, c);
    gfx::wideLine(x + w, y + 3 * h / 4, x, y + h, r, c);
    gfx::wideLine(x, y + h, x, y, r, c);
    gfx::wideLine(x, y, x + w, y + h / 4, r, c);
    gfx::wideLine(x + w, y + h / 4, x - w, y + 3 * h / 4, r, c);
  }
  return 2 * w + u(6);
#else
  (void)right, (void)cy, (void)draw;
  return 0;
#endif
}

// Som silenciado pelo BOOT: um sino riscado, à esquerda do Bluetooth. Devolve a largura usada.
int muteIcon(int right, int cy, bool draw) {
  if (!app::muted()) return 0;
  const int r = u(5), x = right - r - u(2);
  if (draw) {
    fb().fillCircle(x, cy - u(1), r, MUTED);                       // o corpo do sino
    fb().fillRect(x - r - u(1), cy, 2 * r + u(3), u(3), MUTED);    // a borda
    fb().fillCircle(x, cy + u(5), u(2), MUTED);                    // o badalo
    gfx::wideLine(x - r - u(2), cy - r - u(2), x + r + u(2), cy + r + u(3), max(1.f, u(1) * 0.9f), RED);
  }
  return 2 * r + u(8);
}

// sub: a linha do modelo e de CLI/Desk embaixo do título. SUB_WARN: só quando há aviso (offline ou a página de
// status), nas telas em que modelo e sessões repetiriam o que já está nelas. Offline ela sempre aparece: é onde
// está o aviso.
constexpr int SUB_WARN = 2;
bool subLine = true;  // o último cabeçalho desenhou a linha de baixo (modelo, CLI · Desk ou o aviso sem dados)
// sem a linha de baixo, o conteúdo das telas de posição fixa sobe o espaço dela
int lift() { return subLine ? 0 : u(L() ? 58 : 62) - u(46); }
// devolve true se o título perdeu um trecho por falta de espaço (quem chama pode mostrar o trecho em outro lugar)
bool header(bool live, const char* title, bool dropdown = false, int sub = 1) {
  const auto& f = app::frame;
  JsonVariantConst cx = f["cx"];
  // a página de status do serviço mostrado: 1 instável, 2 fora do ar (na principal do Codex, a da OpenAI)
  const int st = live ? (codexMain ? (cx["st"] | 0) : (f["st"] | 0)) : 0;
  subLine = sub == 1 || !live || (sub == SUB_WARN && st);
  char clkBuf[8];  // a hora contínua (RTC + frames) anda entre os frames e sem o Mac; sem ela, a do frame
  const int32_t secs = app::secondsOfDay();
  if (secs >= 0) snprintf(clkBuf, sizeof clkBuf, "%02d:%02d", (int)(secs / 3600), (int)(secs / 60 % 60));
  const gfx::Font HT = gfx::LABEL, HS = gfx::SMALL;  // na tela densa, o gfx já sobe um tamanho
  const char* clk = secs >= 0 ? clkBuf : (const char*)(f["clk"] | "");
  const int CLK_R = 2 * GEAR_R() + u(8);  // a borda direita do relógio, antes da engrenagem
  const int clkW = gfx::textWidth(clk, gfx::TITLE), clkCy = u(8) + gfx::lineHeight(gfx::TITLE) / 2;
  const int pw = powerIcon(W() - M() - CLK_R - clkW - u(8), clkCy, true);
  const int bw = bleIcon(W() - M() - CLK_R - clkW - u(8) - pw, clkCy, true);
  const int mw = muteIcon(W() - M() - CLK_R - clkW - u(8) - pw - bw, clkCy, true);
  const int clkLeft = W() - M() - CLK_R - clkW - (pw + bw + mw ? pw + bw + mw + u(8) : 0);
  const int tw = clkLeft - M() - u(10) - (dropdown ? u(18) : 0);
  const String t = fit(title, HT, tw);
  const bool cut = t != title;
  if (!strncmp(title, "‹", strlen("‹"))) {  // título que volta: um botão em pílula, para ver que dá para tocar
    const uint16_t pill = blend(BG, TRACK, 0.85f);
    const int ph = gfx::lineHeight(HT) + u(8);
    fb().fillSmoothRoundRect(M() - u(8), u(10) - u(4), gfx::textWidth(t.c_str(), HT) + u(18), ph, ph / 2, pill);
    text(t.c_str(), M(), u(10), HT, TEXT, top_left, pill);
  } else {
    text(t.c_str(), M(), u(10), HT, TEXT);
  }
  if (dropdown) {  // a org abre a lista de orgs: um "v" pequeno depois do nome
    const int x = M() + gfx::textWidth(t.c_str(), HT) + u(8), y = u(10) + gfx::lineHeight(HT) / 2, d = max(3, u(4));
    gfx::wideLine(x, y - d / 2, x + d, y + d / 2, 1.0f, MUTED);
    gfx::wideLine(x + d, y + d / 2, x + 2 * d, y - d / 2, 1.0f, MUTED);
  }
  text(clk, W() - M() - CLK_R, u(8), gfx::TITLE, TEXT, top_right);
  gear(W() - M() - GEAR_R(), clkCy, GEAR_R(), gearActive ? ACCENT : MUTED);
  if (!subLine) return cut;
  char buf[64];
  const char* svc = codexMain || codexOnly() ? "Codex" : "Claude";
  if (st) snprintf(buf, sizeof buf, st > 1 ? "%s fora do ar" : "%s instável", svc);
  else if (live && codexMain) snprintf(buf, sizeof buf, "Codex %d", (int)(cx["n"] | 0));
  else if (live && codexOnly()) snprintf(buf, sizeof buf, "Codex %d", (int)(f["cxn"] | 0));
  else if (live && !cx.isNull())
    snprintf(buf, sizeof buf, "CLI %d · Desk %d · Codex %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0), (int)(cx["n"] | 0));
  else if (live) snprintf(buf, sizeof buf, "CLI %d · Desk %d", (int)(f["cli"] | 0), (int)(f["desk"] | 0));
  else snprintf(buf, sizeof buf, "Offline há %lu min", (millis() - app::lastFrameMs) / 60000);
  // o modelo divide a linha com o status. Sem espaço sai, nesta ordem: o esforço ("· high"), o "Multica N" (está na
  // página dele) e por fim o modelo. O status fica sempre.
  const int mcRun = live && !st ? (int)(f["mc"]["r"] | 0) : 0;
  String status = buf, model = codexMain ? (cx["m"] | "Codex") : (f["m"] | "Claude");
  if (mcRun) status += " · Multica " + String(mcRun);
  auto fits = [&] { return gfx::textWidth(model.c_str(), HS) + u(24) + gfx::textWidth(status.c_str(), HS) <= W() - 2 * M(); };
  if (!fits() && model.indexOf(" · ") > 0) model.remove(model.indexOf(" · "));
  if (!fits() && mcRun) status = buf;
  const uint16_t sc = !live || st > 1 ? RED : st ? AMBER : GREEN;
  const int sy = u(10) + below(HT, 22), w = gfx::textWidth(status.c_str(), HS);
  if (fits()) text(model.c_str(), M(), sy, HS, DIM);
  text(status.c_str(), W() - M(), sy, HS, sc == GREEN ? DIM : sc, top_right);
  fb().fillSmoothCircle(W() - M() - w - u(8), sy + gfx::lineHeight(HS) / 2 - u(2), max(2, u(3)), sc);
  return cut;
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

// Dígitos em células de largura fixa: o número não treme enquanto conta. Mais largo que `maxW` ("100%" num anel
// pequeno): o "%" sai menor, no alto, ao lado dos dígitos.
void heroNumber(const char* s, int cx, int cy, uint16_t c, int maxW = 0) {
  const int cell = gfx::textWidth("0", gfx::HERO);
  auto width = [&](gfx::Font pf) {
    int total = 0;
    for (const char* p = s; *p; p++) total += isdigit(*p) ? cell : gfx::textWidth(String(*p).c_str(), *p == '%' ? pf : gfx::HERO);
    return total;
  };
  const gfx::Font pf = maxW && width(gfx::HERO) > maxW ? gfx::NUM : gfx::HERO;
  int x = cx - width(pf) / 2;
  for (const char* p = s; *p; p++) {
    char ch[2] = {*p, 0};
    const gfx::Font f = *p == '%' ? pf : gfx::HERO;
    int w = isdigit(*p) ? cell : gfx::textWidth(ch, f);
    const int dy = f == gfx::HERO ? 0 : (gfx::lineHeight(gfx::NUM) - gfx::lineHeight(gfx::HERO)) / 3;
    text(ch, x + w / 2, cy + dy, f, c, middle_center);
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
  // a do Codex é esta mesma tela lendo o bloco "cx": sem troca de org e sem o "atualizado há" da API do Claude
  JsonVariantConst f = codexMain ? app::frame["cx"].as<JsonVariantConst>() : app::frame.as<JsonVariantConst>();
  const bool apiStale = !codexMain && app::stale(), stale = apiStale || !live, land = L();
  const int p5 = f["p5"] | -1, pw = f["pw"] | -1;
  char buf[48];
  gfx::clear();
  if (codexMain) {
    const char* plan = f["plan"] | "";
    snprintf(buf, sizeof buf, *plan ? "Codex · %s" : "Codex", plan);
    header(live, buf, false);
  } else {
    header(live, f["org"] | "Claude", !codexOnly());  // só o Claude troca de org
  }

  // Anel da janela de 5h: o herói da tela. Em pé fica no alto e centrado; deitada, na coluna da esquerda.
  // Semana, tokens e o gráfico, em pé, de baixo para cima (gráfico, números, semana): na tela média as fontes são
  // maiores que a escala, e as posições fixas se sobreporiam. Deitada, de cima para baixo na coluna da direita.
  const int base = u(land ? 290 : 456), sh = u(land ? 40 : 34), label = base - sh - below(gfx::SMALL, 18), bh = u(10);
  int wy, ty;
  if (land) {
    wy = u(66);
    ty = wy + below(gfx::LABEL, 26) + bh + u(8) + gfx::lineHeight(gfx::SMALL) + u(16);
  } else {
    ty = min(u(344), label - u(4) - gfx::lineHeight(gfx::NUM) - below(gfx::SMALL, 18));
    wy = min(u(290), ty - u(12) - bh - below(gfx::LABEL, 26));
  }
  // Em pé o anel fica entre o cabeçalho e a semana, com folga dos dois lados; só encolhe se não houver altura.
  const int headBottom = u(10) + (subLine ? below(gfx::LABEL, 22) + gfx::lineHeight(gfx::SMALL) : gfx::lineHeight(gfx::LABEL));
  const int r = land ? u(108) : min(u(100), (wy - headBottom - u(16)) / 2);
  const int thick = r < u(100) ? u(12) : u(15);  // anel menor, traço mais fino: o furo segura o texto de dentro
  // tela quadrada (AMOLED): o anel vai para a esquerda só com o %, e o texto dele numa coluna à direita
  const bool side = !land && W() * 10 >= gfx::height() * 9;
  const int cx = land || side ? M() + r + u(4) : W() / 2, cy = land ? u(182) : max(u(160), headBottom + u(8) + r);
  const int rx = cx + r + u(22), rw = W() - M() - rx;
  const bool warnInside = land || (!side && cy + r + u(14) + gfx::lineHeight(gfx::SMALL) / 2 > wy);  // sem vão embaixo: o aviso entra no anel
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
  const char* warn = stale || p5 < 80 ? nullptr : p5 >= 100 ? "Esgotado" : p5 >= 90 ? "No limite" : "Perto do limite";
  const char* other = !stale && !codexMain && p5 >= 100 ? suggestedOrg() : nullptr;
  if (apiStale) snprintf(buf, sizeof buf, "Atualizado há %d min", app::dataAge() / 60);
  else strlcpy(buf, f["r5"] | "", sizeof buf);
  sugW = 0;
  if (side) {  // coluna à direita: janela, reset e o aviso, centrados na altura do anel, um nível maiores que no anel
    const char* win = *s5 ? s5 : "Sessão 5h";
    auto big = [&](const char* t, gfx::Font f, gfx::Font smaller) { return gfx::textWidth(t, f) <= rw ? f : smaller; };
    const gfx::Font WF = big(win, gfx::LABEL, gfx::SMALL), RF = big(buf, gfx::TITLE, gfx::LABEL);
    const gfx::Font AF = warn ? big(warn, gfx::LABEL, gfx::SMALL) : gfx::SMALL;
    const int h = gfx::lineHeight(WF) + u(4) + gfx::lineHeight(RF) + (warn ? u(10) + gfx::lineHeight(AF) : 0);
    int y = cy - h / 2 - (other ? u(20) : 0);
    text(fit(win, WF, rw).c_str(), rx, y, WF, *s5 ? ACCENT : DIM);
    y += gfx::lineHeight(WF) + u(4);
    text(fit(buf, RF, rw).c_str(), rx, y, RF, apiStale ? AMBER : TEXT);
    y += gfx::lineHeight(RF) + u(10);
    if (warn) text(warn, rx, y, AF, levelColor(p5)), y += gfx::lineHeight(AF) + u(10);
    if (other) sugY = y, sugH = max(u(30), gfx::lineHeight(gfx::SMALL) + 8);
    if (p5 < 0) strcpy(buf, "--");
    else snprintf(buf, sizeof buf, "%d%%", (int)lroundf(anim::p5));
    heroNumber(buf, cx, cy, stale ? DIM : p5 < 0 ? TEXT : lc, inner(gfx::lineHeight(gfx::HERO) * 7 / 20));
  } else {
    // sem espaço sob o anel (deitada, AMOLED): o aviso de limite ocupa a linha de cima, dentro dele
    if (warnInside && warn) ringText(warn, u(-46), gfx::SMALL, levelColor(p5));
    else ringText(*s5 ? s5 : "Sessão 5h", u(-46), gfx::SMALL, *s5 ? ACCENT : DIM);
    if (p5 < 0) strcpy(buf, "--");
    else snprintf(buf, sizeof buf, "%d%%", (int)lroundf(anim::p5));
    heroNumber(buf, cx, cy, stale ? DIM : p5 < 0 ? TEXT : lc);  // o número na cor da faixa, como o anel
    if (apiStale) snprintf(buf, sizeof buf, "Atualizado há %d min", app::dataAge() / 60);
    else strlcpy(buf, f["r5"] | "", sizeof buf);
    ringText(buf, u(44), gfx::LABEL, apiStale ? AMBER : MUTED);
    if (!land && other) {  // janela esgotada e há outra org salva: oferece a troca
      sugY = cy + r + u(2), sugH = max(u(26), gfx::lineHeight(gfx::SMALL) + 6);
    } else if (!warnInside && warn) {
      text(warn, cx, cy + r + u(14), gfx::SMALL, levelColor(p5), middle_center);
    }
  }

  // Semana, com o marcador do ritmo linear: barra à frente do marcador = uso adiantado.
  const int bw = cw, by = wy + below(gfx::LABEL, 26);
  text("Semana", x0, wy, gfx::LABEL, MUTED);
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
    snprintf(buf, sizeof buf, "Reset %s", (const char*)(f["rw"] | ""));
    text(buf, x0, by + bh + u(8), gfx::SMALL, DIM);
  }

  // Tokens de hoje e ritmo: lado a lado em pé; deitada, o ritmo numa linha embaixo.
  const int col2 = W() / 2 + u(8);
  float glow = anim::since(anim::tokensMs, 900);
  text("Tokens hoje", x0, ty, gfx::SMALL, MUTED);
  text(f["tot"] | "0", x0, ty + below(gfx::SMALL, 18), gfx::NUM, blend(ACCENT, TEXT, glow));
  if (land) {
    snprintf(buf, sizeof buf, "%s por minuto", (const char*)(f["rate"] | "0"));
    text(fit(buf, gfx::SMALL, cw).c_str(), x0, ty + below(gfx::SMALL, 18) + gfx::lineHeight(gfx::NUM), gfx::SMALL, MUTED);
  } else {
    text("Por minuto", col2, ty, gfx::SMALL, MUTED);
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
    text("Última hora", x0, label, gfx::SMALL, DIM);
  }
  if (other) {
    if (side) strcpy(buf, "Trocar org ›");  // ao lado, o "Esgotado" já está acima
    else snprintf(buf, sizeof buf, "Esgotado · trocar para %s ›", other);
    const int sx = land ? x0 : side ? rx : M(), maxW = land ? cw : side ? rw : W() - 2 * M();
    const String t = fit(buf, gfx::SMALL, maxW - u(24));
    sugW = land || side ? maxW : gfx::textWidth(t.c_str(), gfx::SMALL) + u(24), sugX = land || side ? sx : cx - sugW / 2;
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
// Em pé, até 3 linhas de 124 px; numa tela baixa para a fonte (AMOLED), o que sobra acima dos pontos dividido por 3.
int modelPitch() { return min(u(124), (gfx::height() - u(68) + lift() - u(24)) / 3); }
// a linha do cache só entra se couber no bloco; o detalhe do modelo mostra tudo
bool modelCacheFits() {
  return below(gfx::TITLE, 30) + u(18) + below(gfx::LABEL, 22) + below(gfx::SMALL, 18) + gfx::lineHeight(gfx::SMALL) + u(8) <= modelPitch();
}
Cell modelCell(int i) {
  if (!L()) return {M(), u(68) - lift() + i * modelPitch(), W() - 2 * M()};
  const int gap = u(16), w = (W() - 2 * M() - gap) / 2;
  return {M() + (i % 2) * (w + gap), u(64) - lift() + (i / 2) * u(124), w};
}

void drawModels(bool live, int page) {
  char buf[64];
  gfx::clear();
  header(live, "Modelos hoje", false, SUB_WARN);
  JsonArrayConst mods = app::frame["mods"];
  if (mods.size() == 0) text("Sem uso hoje", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
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
    snprintf(buf, sizeof buf, "Entrada %s · saída %s", (const char*)m[3], (const char*)m[4]);
    const int y2 = y1 + below(gfx::LABEL, 22);
    text(fit(buf, gfx::SMALL, w).c_str(), x, y2, gfx::SMALL, DIM);
    if (*(m[6] | "")) snprintf(buf, sizeof buf, "Cache lido %s · escrito %s", (const char*)m[5], (const char*)m[6]);
    else snprintf(buf, sizeof buf, "Cache lido %s", (const char*)m[5]);  // o Codex não escreve cache
    if (L() || modelCacheFits()) text(fit(buf, gfx::SMALL, w).c_str(), x, y2 + below(gfx::SMALL, 18), gfx::SMALL, DIM);
    i++;
  }
  dots(page);
}

// Página web: com WiFi, o QR do endereço .local; sem, o QR que coloca o celular no WiFi da placa, onde o
// portal abre a configuração da rede. Toque em qualquer lugar volta para o Sobre.
void drawWeb(bool) {
  char qr[64];
  gfx::clear();
  // só o título: modelo e contadores de sessão não dizem nada nesta tela
  text("‹ Página web", M(), u(22), gfx::TITLE, TEXT, middle_left);
  if (webui::online()) text("Toque para voltar", W() - M(), u(22), gfx::SMALL, DIM, middle_right);
  const bool net = webui::online();
  if (net) snprintf(qr, sizeof qr, "http://%s.local/", webui::host());
  else snprintf(qr, sizeof qr, "WIFI:T:nopass;S:%s;;", webui::apName());
  int top = u(48);
  // com o daemon por perto, o caminho principal é a senha digitada no computador; o QR fica como alternativa
  const bool pc = !net && hostlink::lastIdMs() && millis() - hostlink::lastIdMs() < 15000;
  if (pc) {
    const String st = webui::joinStatus();
    top += title2(st.length() ? st.c_str() : "Digite a senha do WiFi no computador (toque para pedir de novo)", W() / 2, top - u(8), W() - 2 * M(), ACCENT,
                  gfx::LABEL);
    text("ou pelo celular:", W() / 2, top, gfx::SMALL, DIM, top_center);
    top += below(gfx::SMALL, 22);
  }
  const int textH = below(gfx::SMALL, 22) * 3 + below(gfx::LABEL, 24) + u(8);
  const int side = min(W() - 2 * M(), gfx::height() - top - textH - u(8));
  fb().qrcode(qr, (W() - side) / 2, top, side);
  int y = top + side + u(14);
  if (net) {
    text("Escaneie ou abra no navegador:", W() / 2, y, gfx::SMALL, MUTED, middle_center);
    snprintf(qr, sizeof qr, "%s.local", webui::host());
    text(qr, W() / 2, y + below(gfx::SMALL, 22), gfx::LABEL, ACCENT, middle_center);
  } else {
    text("1. escaneie para entrar no WiFi", W() / 2, y, gfx::SMALL, MUTED, middle_center);
    text(webui::apName(), W() / 2, y + below(gfx::SMALL, 20), gfx::LABEL, ACCENT, middle_center);
    const int y2 = y + below(gfx::SMALL, 20) + below(gfx::LABEL, 22);
    text("2. a página abre sozinha; senão,", W() / 2, y2, gfx::SMALL, MUTED, middle_center);
    text("Abra 192.168.4.1", W() / 2, y2 + below(gfx::SMALL, 18), gfx::SMALL, MUTED, middle_center);
  }
}

// Falta instalar o daemon: título, QR code da página /comecar e o endereço curto, numa coluna centrada na altura.
// O QR cresce até metade da largura com o que sobra; numa tela baixa (deitada, 240 px) ele encolhe em vez de
// empurrar o texto para fora. Na redonda, o bloco centrado fica longe das bordas curvas.
void drawInstall() {
  gfx::clear();
  const int cx = W() / 2, maxW = W() - 2 * M() - (board::PROFILE.oled ? u(40) : 0), gap = u(18);
  String l2;
  wrap2("Falta o tokenpulse no computador", gfx::TITLE, maxW, l2);
  const int titleH = (gfx::lineHeight(gfx::TITLE) + u(4)) * (l2.length() ? 2 : 1);
  const int textH = gfx::lineHeight(gfx::LABEL) + u(8) + gfx::lineHeight(gfx::TITLE) + u(10) + gfx::lineHeight(gfx::SMALL);
  const int side = max(u(90), min(W() / 2, gfx::height() - titleH - textH - 2 * gap - u(24)));
  int y = max(u(10), (gfx::height() - (titleH + gap + side + gap + textH)) / 2);
  y += title2("Falta o tokenpulse no computador", cx, y, maxW, TEXT) + gap;
  fb().qrcode(app::START_URL, cx - side / 2, y, side);
  y += side + gap;
  text("No computador, abra:", cx, y, gfx::LABEL, MUTED, top_center);
  y += gfx::lineHeight(gfx::LABEL) + u(8);
  text(app::START_SHORT, cx, y, gfx::TITLE, ACCENT, top_center);
  y += gfx::lineHeight(gfx::TITLE) + u(10);
  text("ou escaneie para receber o link", cx, y, gfx::SMALL, DIM, top_center);
}

// Destaque de uma sessão que passou a esperar você: o mascote grande, o título, o workspace e o estado, na cor
// do estado. O botão "Abrir Sessão" traz a janela da sessão para a frente; tocar fora dele, ou o KEY3, fecha.
// o botão fica logo acima da barra de tempo (em h - u(22)): com a altura de 9 mm, ancorado no topo ele a cobria
int ATTN_BTN_H() { return max(u(50), TAPPX()); }
int ATTN_BTN_Y() { return gfx::height() - u(36) - ATTN_BTN_H(); }
void drawAttention(int r, uint32_t ageMs) {
  JsonArrayConst row = app::frame["sess"]["rows"][r];
  const bool block = !strcmp(row[2] | "", "block");
  const uint16_t c = block ? RED : YELLOW, bg = blend(BG, c, 0.12f);
  gfx::clear(bg);
  const int cx = W() / 2, h = gfx::height(), maxW = W() - 2 * M();
  const char* ask = row[8] | "";  // o que a sessão pede (o daemon só manda para quem espera você)
  text(block ? "Pede aprovação" : "Sua vez", cx, u(28), gfx::TITLE, c, middle_center, bg);
  // com a pergunta, o mascote encolhe para ela caber acima do botão
  const int px = max(3, *ask ? u(5) : u(8)), my = u(52);
  mascot::draw(cx - mascot::W * px / 2, my, px, block ? mascot::BLOCK : mascot::TURN, bg);
  int y = my + 8 * px + u(14);
  const int lh = gfx::lineHeight(gfx::NUM);
  String t2, t1 = wrap2(row[0] | "", gfx::NUM, maxW, t2);  // o título em até duas linhas, sem cortar no meio
  const int ql = gfx::lineHeight(gfx::LABEL);
  int askN = 0;
  for (String rest = ask; rest.length() && askN < 3; askN++) wrap2(String(rest).c_str(), gfx::LABEL, maxW, rest);
  // o título só ganha a segunda linha se a pergunta inteira ainda couber acima do botão
  if (t2.length() && y + 2 * (lh + u(2)) + gfx::lineHeight(gfx::SMALL) + u(12) + askN * ql > ATTN_BTN_Y() - u(10))
    t1 = fit(row[0] | "", gfx::NUM, maxW), t2 = "";
  text(fit(t1.c_str(), gfx::NUM, maxW).c_str(), cx, y, gfx::NUM, TEXT, top_center, bg);
  y += lh + u(2);
  if (t2.length()) text(fit(t2.c_str(), gfx::NUM, maxW).c_str(), cx, y, gfx::NUM, TEXT, top_center, bg), y += lh + u(2);
  char buf[64];
  const int mins = row[4] | 0;
  const char* ws = app::frame["sess"]["ws"][(int)(row[5] | 0)][0] | "";
  if (mins > 0) snprintf(buf, sizeof buf, "%s · %s · há %d min", ws, (const char*)(row[1] | ""), mins);
  else snprintf(buf, sizeof buf, "%s · %s · agora", ws, (const char*)(row[1] | ""));
  text(fit(buf, gfx::SMALL, maxW).c_str(), cx, y, gfx::SMALL, MUTED, top_center, bg);
  y += gfx::lineHeight(gfx::SMALL) + u(12);
  // a pergunta em até 3 linhas, quebrada nas palavras; o que não couber termina em reticências
  const int room = (ATTN_BTN_Y() - u(10) - y) / ql;
  String rest = ask;
  for (int line = 0; line < min(3, room) && rest.length(); line++) {
    String l2, l1 = wrap2(rest.c_str(), gfx::LABEL, maxW, l2);
    if (gfx::textWidth(l1.c_str(), gfx::LABEL) > maxW) l1 = fit(l1.c_str(), gfx::LABEL, maxW);  // palavra sem espaço
    const bool last = line == min(3, room) - 1 && l2.length();
    if (last) l1 = fit((l1 + " " + l2).c_str(), gfx::LABEL, maxW);
    text(l1.c_str(), cx, y, gfx::LABEL, TEXT, top_center, bg);
    y += ql, rest = last ? "" : l2;
  }
  const int by = ATTN_BTN_Y(), bh = ATTN_BTN_H(), bw2 = maxW;
  fb().fillSmoothRoundRect(cx - bw2 / 2, by, bw2, bh, min(bh / 2, u(26)), c);
  text("Abrir Sessão", cx, by + bh / 2, fontFor(bh) == gfx::SMALL ? gfx::LABEL : fontFor(bh), BG, middle_center, c);
  // o tempo até o destaque sair sozinho
  const int left = maxW * (60000 - min<uint32_t>(ageMs, 60000)) / 60000;
  fb().fillRoundRect(M(), h - u(22), maxW, u(4), u(2), blend(bg, c, 0.25f));
  if (left > 0) fb().fillRoundRect(M(), h - u(22), left, u(4), u(2), c);
}

bool attentionCenter(int x, int y) {  // o botão "abrir no computador", com folga para o dedo
  const int bw2 = W() - 2 * M();
  return abs(x - W() / 2) < bw2 / 2 + u(10) && y > ATTN_BTN_Y() - u(10) && y < ATTN_BTN_Y() + ATTN_BTN_H() + u(10);
}

// Primeiro uso com o Codex no Mac: a pergunta de quais provedores mostrar, uma vez só (a resposta vale para todas
// as placas). O daemon manda "pask" com o que o Mac tem: "both" (Claude e Codex) ou "codex" (sem Claude Code).
namespace {
struct AskOpt {
  const char *name, *hint, *mode;
};
const AskOpt ASK_BOTH[] = {{"Claude + Codex", "Uma principal para cada", "both"},
                           {"Só Claude", "", "claude"},
                           {"Só Codex", "", "codex"},
                           {"Automático", "Quem teve uso em 7 dias", "auto"}};
const AskOpt ASK_CODEX[] = {{"Mostrar o Codex", "As telas de sempre, com o Codex", "codex"},
                            {"Automático", "Entra o Claude quando ele aparecer", "auto"}};
bool askBoth() { return !strcmp(app::frame["pask"] | "", "both"); }
int askLineY = 0, askLine = 0;  // a geometria do último desenho, para o toque
}  // namespace

// As opções têm alvo de 9 mm ou mais: sem altura para isso numa coluna, os mascotes saem e as opções viram uma
// grade de 2 colunas (a AMOLED com 4 opções).
int askCols = 1, askCellW = 0;
void drawProvAsk() {
  const AskOpt* opts = askBoth() ? ASK_BOTH : ASK_CODEX;
  const int n = askBoth() ? 4 : 2, land = L(), cx = W() / 2;
  const int titleH = (land || !askBoth() ? 1 : 2) * below(gfx::TITLE, 30) + below(gfx::SMALL, 20) + u(8);
  const int px = u(7), mascotH = 8 * px + u(18);
  const bool mascots = !land && gfx::height() - u(22) - mascotH - titleH - u(16) >= n * TAPPX();
  gfx::clear();
  int y = u(land ? 12 : 22);
  if (mascots) {  // os mascotes de quem o Mac tem, quando sobra altura
    const int gap = u(10), w = mascot::W * px;
    if (askBoth()) {
      mascot::draw(cx - w - gap / 2, y, px, mascot::WORK, BG, 0);
      mascot::draw(cx + gap / 2, y, px, mascot::WORK, BG, 1, true);
    } else {
      mascot::draw(cx - w / 2, y, px, mascot::WORK, BG, 0, true);
    }
    y += mascotH;
  }
  if (land || !askBoth()) {
    y += title2(askBoth() ? "Achei o Claude e o Codex" : "Achei o Codex", cx, y, W() - 2 * M(), TEXT) + u(4);
  } else {
    text("Achei o Claude", cx, y, gfx::TITLE, TEXT, top_center);
    y += below(gfx::TITLE, 30);
    text("e o Codex", cx, y, gfx::TITLE, TEXT, top_center);
    y += below(gfx::TITLE, 30) + u(4);
  }
  text(askBoth() ? "O que a placa mostra?" : "Nenhuma sessão do Claude Code por aqui", cx, y, gfx::SMALL, MUTED, top_center);
  y += below(gfx::SMALL, 20) + u(8);
  // sem "agora não": o automático já é uma das respostas
  const int room = gfx::height() - u(land ? 10 : 16) - y, gap = u(10);
  askCols = room / n >= TAPPX() ? 1 : 2;
  const int rows = (n + askCols - 1) / askCols;
  askLineY = y, askLine = (room + gap) / rows, askCellW = (W() - 2 * M() - (askCols - 1) * gap) / askCols;
  const uint16_t track = blend(BG, TRACK, 0.7f);
  for (int k = 0; k < n; k++) {
    const int x = M() + (k % askCols) * (askCellW + gap), ly = y + (k / askCols) * askLine, h = askLine - gap;
    fb().fillSmoothRoundRect(x, ly, askCellW, h, u(12), track);
    const int tx = askCols == 1 ? x + u(46) : x + u(14), tw = askCellW - (tx - x) - u(10);
    const bool tall = h >= 2 * gfx::lineHeight(gfx::TITLE) + gfx::lineHeight(gfx::SMALL);
    const gfx::Font NF = tall && gfx::textWidth(opts[k].name, gfx::TITLE) <= tw ? gfx::TITLE : gfx::LABEL;
    const bool hint = *opts[k].hint && h >= gfx::lineHeight(NF) + gfx::lineHeight(gfx::SMALL) + u(12);
    const int mid = ly + h / 2;
    if (askCols == 1) {
      fb().fillSmoothCircle(x + u(24), mid, u(10), MUTED);
      fb().fillSmoothCircle(x + u(24), mid, u(10) - max(2, u(2)), track);
    }
    if (hint) {
      text(fit(opts[k].name, NF, tw).c_str(), tx, mid - 1, NF, TEXT, bottom_left, track);
      String l2, l1 = wrap2(opts[k].hint, gfx::SMALL, tw, l2);
      text(fit(l1.c_str(), gfx::SMALL, tw).c_str(), tx, mid + u(3), gfx::SMALL, DIM, top_left, track);
      if (askCols > 1 && l2.length()) text(fit(l2.c_str(), gfx::SMALL, tw).c_str(), tx, mid + u(3) + gfx::lineHeight(gfx::SMALL), gfx::SMALL, DIM, top_left, track);
    } else {
      text(fit(opts[k].name, NF, tw).c_str(), tx, mid, NF, TEXT, middle_left, track);
    }
  }
}

const char* provAskTap(int x, int y) {
  const int n = askBoth() ? 4 : 2, row = (y - askLineY) / max(1, askLine), col = (x - M()) / max(1, askCellW + u(10));
  const int k = row * askCols + col;
  if (y >= askLineY && x >= M() && x <= W() - M() && col < askCols && k < n) return (askBoth() ? ASK_BOTH : ASK_CODEX)[k].mode;
  return nullptr;
}

// Descanso da OLED: as telas escolhidas (settings::web.restPages), em versão resumida, uma a cada 15 s.
// Cada troca põe o bloco num lugar novo da tela, e o fundo é preto: o vidro não marca.
namespace {
constexpr uint16_t REST_TEXT = 0xBDF7, REST_MUTED = 0x8410, REST_TRACK = 0x2104;
constexpr uint32_t REST_MS = 15000;
int DRIFT() { return u(28); }  // folga em volta do bloco, para ele mudar de lugar a cada troca

lgfx::VLWfont& giantFont(bool& ok) {
  static lgfx::PointerWrapper wrap;
  static lgfx::VLWfont font;
  static bool loaded = (wrap.set(inter_giant), font.loadFont(&wrap));
  ok = loaded;
  return font;
}

// O anel da home, grande, com o % de 5h no meio na cor da faixa e a semana embaixo.
bool restUsage(int x0, int y0, bool measure, int& w, int& h) {
  const auto& f = app::frame;
  const int p5 = f["p5"] | -1, pw = f["pw"] | -1;
  if (p5 < 0) return false;
  const int r = W() / 2 - u(26), thick = u(20);  // sobra em volta para o anel mudar de lugar
  w = h = 2 * r;
  if (measure) return true;
  const int cx = x0 + r, cy = y0 + r;
  const uint16_t lc = levelColor(p5);
  gfx::ring(cx, cy, r, thick, min(p5, 100) / 100.f, lc, REST_TRACK, 0);
  char buf[24];
  snprintf(buf, sizeof buf, "%d%%", p5);
  bool ok;
  auto& giant = giantFont(ok);
  if (ok) {  // antes dos textos: o fundo do glifo gigante cobriria o que estivesse embaixo
    auto& fb = gfx::fb();
    fb.setFont(&giant);
    fb.setTextDatum(middle_center);
    fb.setTextColor(lc, 0);
    fb.drawString(buf, cx, cy);
  } else {
    text(buf, cx, cy, gfx::HERO, lc, middle_center, 0);
  }
  text("Sessão 5h", cx, cy - u(82), gfx::TITLE, REST_MUTED, middle_center, 0);
  if (pw >= 0) {
    snprintf(buf, sizeof buf, "Semana %d%%", pw);
    text(buf, cx, cy + u(86), gfx::NUM, pw >= 70 ? levelColor(pw) : REST_MUTED, middle_center, 0);
  }
  return true;
}

bool restClock(int x0, int y0, bool measure, int& w, int& h) {
  int32_t secs = app::secondsOfDay();
  if (secs < 0) {  // daemon antigo, sem o campo "t": a hora do "clk"
    const char* clk = app::frame["clk"] | "";
    if (strlen(clk) >= 5) secs = atoi(clk) * 3600 + atoi(clk + 3) * 60;
  }
  if (secs < 0) return false;
  const bool s = settings::web.restSecs;
  flip::size(w, h, s);
  if (!measure) flip::draw(x0, y0, secs, s);
  return true;
}

// Sessões: quantas trabalham, esperam você e pedem aprovação, com as cores de estado da placa.
bool restSessions(int x0, int y0, bool measure, int& w, int& h) {
  int work = 0, block = 0, turn = 0, total = 0;
  for (JsonArrayConst g : app::frame["sess"]["ws"].as<JsonArrayConst>())
    total += (int)g[1], work += (int)g[2], block += (int)g[3], turn += (int)g[4];
  if (!total) return false;
  struct Line {
    int n;
    const char* label;
    uint16_t c;
  } lines[] = {{block, "Pedem aprovação", RED}, {turn, "Esperam você", YELLOW}, {work, "Trabalhando", GREEN},
               {total - work - block - turn, "Paradas", REST_MUTED}};
  const int numW = gfx::textWidth("00", gfx::HERO);
  // o rótulo desce para a fonte do título se na do número não couber ao lado dele
  gfx::Font LF = gfx::NUM;
  int shown = 0, labelW = 0;
  for (int pass = 0; pass < 2; pass++, LF = gfx::TITLE) {
    shown = labelW = 0;
    for (auto& l : lines)
      if (l.n) shown++, labelW = max(labelW, gfx::textWidth(l.label, LF));
    if (numW + u(18) + labelW <= W() - DRIFT()) break;
  }
  const int title = gfx::lineHeight(gfx::TITLE) + u(14), nh = gfx::lineHeight(gfx::HERO);
  const int step = max(nh, (gfx::height() - DRIFT() - title) / shown);
  w = max(numW + u(18) + labelW, gfx::textWidth("Sessões", gfx::TITLE)), h = title + shown * step;
  if (measure) return true;
  text("Sessões", x0, y0, gfx::TITLE, REST_MUTED, top_left, 0);
  int y = y0 + title + (step - nh) / 2;
  char buf[8];
  for (auto& l : lines) {
    if (!l.n) continue;
    snprintf(buf, sizeof buf, "%d", l.n);
    text(buf, x0 + numW, y, gfx::HERO, l.c, top_right, 0);
    text(l.label, x0 + numW + u(18), y + nh / 2, LF, l.c == REST_MUTED ? REST_MUTED : REST_TEXT, middle_left, 0);
    y += step;
  }
  return true;
}

// Modelos de hoje: os três que mais usaram, com os tokens e uma barra do quanto do dia.
bool restModels(int x0, int y0, bool measure, int& w, int& h) {
  JsonArrayConst mods = app::frame["mods"];
  const int n = min<int>(mods.size(), 3);
  if (!n) return false;
  const int title = gfx::lineHeight(gfx::TITLE) + u(14), nh = gfx::lineHeight(gfx::NUM), bar = u(12);
  const int step = max(nh + bar + u(16), (gfx::height() - DRIFT() - title) / n);
  w = W() - DRIFT(), h = title + n * step;
  if (measure) return true;
  text("Modelos hoje", x0, y0, gfx::TITLE, REST_MUTED, top_left, 0);
  for (int i = 0; i < n; i++) {
    JsonArrayConst m = mods[i];
    const int y = y0 + title + i * step + (step - nh - bar - u(10)) / 2;
    text(m[0] | "?", x0, y, gfx::NUM, REST_TEXT, top_left, 0);
    text(m[1] | "", x0 + w, y, gfx::NUM, REST_MUTED, top_right, 0);
    const int by = y + nh + u(10), bw = w * min(100, (int)(m[2] | 0)) / 100;
    gfx::fb().fillSmoothRoundRect(x0, by, w, bar, bar / 2, REST_TRACK);
    if (bw > bar) gfx::fb().fillSmoothRoundRect(x0, by, bw, bar, bar / 2, i ? REST_MUTED : ACCENT);
  }
  return true;
}

// Histórico: 14 barras de tokens por dia, hoje em destaque, e o total de hoje embaixo.
bool restHistory(int x0, int y0, bool measure, int& w, int& h) {
  JsonArrayConst hist = app::frame["hist"];
  const int n = hist.size();
  if (!n) return false;
  const int title = gfx::lineHeight(gfx::TITLE) + u(14), nh = gfx::lineHeight(gfx::NUM), gap = u(6);
  w = W() - DRIFT(), h = gfx::height() - DRIFT();
  if (measure) return true;
  const int bar = (w - (n - 1) * gap) / n, bh = h - title - u(14) - nh;
  float peak = 0.1f;
  for (JsonArrayConst d : hist) peak = max(peak, (float)d[2]);
  text("14 dias", x0, y0, gfx::TITLE, REST_MUTED, top_left, 0);
  for (int i = 0; i < n; i++) {
    const int bhi = max(u(4), (int)(bh * (float)hist[i][2] / peak)), x = x0 + i * (bar + gap);
    gfx::fb().fillSmoothRoundRect(x, y0 + title + bh - bhi, bar, bhi, min(bar / 3, u(5)), i == n - 1 ? ACCENT : 0x4208);
  }
  char buf[24];
  snprintf(buf, sizeof buf, "Hoje %.1fM", (float)hist[n - 1][2]);
  String s = buf;
  s.replace('.', ',');
  text(s.c_str(), x0, y0 + title + bh + u(14), gfx::NUM, REST_TEXT, top_left, 0);
  return true;
}
}  // namespace

void drawSaver() {
  gfx::clear(0);
  const uint32_t slot = millis() / REST_MS;
  const uint32_t rnd = (slot + 1) * 2654435761u;  // posição nova a cada troca, espalhada pela tela
  auto place = [&](int w, int h, int& x, int& y) {
    x = (int)(rnd % max(1, W() - w)), y = (int)(rnd / 7 % max(1, gfx::height() - h));
  };
  int x, y, w, h;
  int cw, chh;  // sem o Mac: com a hora do RTC, alterna o relógio e o mascote
  if ((!app::hasData() || millis() - app::lastFrameMs > 60000) && slot % 2 && restClock(0, 0, true, cw, chh)) {
    place(cw, chh, x, y);
    return (void)restClock(x, y, false, cw, chh);
  }
  if (!app::hasData() || millis() - app::lastFrameMs > 60000) {  // o mascote tentando ligar o cabo
    const char* s = "Sem dados do computador";
    const int px = u(6), mw = mascot::W_OFFLINE * px, gap = u(14), th = gfx::lineHeight(gfx::LABEL);
    place(mw, 10 * px + gap + th, x, y);
    mascot::draw(x, y + 2 * px, px, mascot::OFFLINE, 0);
    return text(s, x + mw / 2, y + 10 * px + gap, gfx::LABEL, 0x6B4D, top_center, 0);
  }
  using Page = bool (*)(int, int, bool, int&, int&);
  static const Page PAGES[] = {restClock, restUsage, restSessions, restModels, restHistory};
  const uint8_t on = settings::web.restPages ? settings::web.restPages : 1;
  // a página da vez entre as ligadas; sem dado para ela, a próxima
  int n = 0;
  for (int i = 0; i < 5; i++) n += on >> i & 1;
  for (int k = 0, want = slot % n; k < 5 * 2; k++) {
    const int i = k % 5;
    if (!(on >> i & 1)) continue;
    if (want-- > 0) continue;
    if (!PAGES[i](0, 0, true, w, h)) continue;
    place(w, h, x, y);
    PAGES[i](x, y, false, w, h);
    return;
  }
}

// PWR segurado: o anel enche até a PMU cortar a energia, com o mascote pegando no sono.
void drawShutdown(float p) {
  gfx::clear(0);
  const int cx = W() / 2, cy = gfx::height() / 2, r = W() / 2 - u(30), px = u(7);
  gfx::ring(cx, cy, r, u(12), p, ORANGE, REST_TRACK, 0);
  mascot::draw(cx - mascot::W * px / 2, cy - u(70), px, mascot::IDLE, 0);
  text(p < 1 ? "Desligando" : "Até logo", cx, cy + u(36), gfx::TITLE, REST_TEXT, middle_center, 0);
  if (p < 1) text("Solte para cancelar", cx, cy + u(66), gfx::SMALL, REST_MUTED, middle_center, 0);
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
  mascot::draw(lcx - mascot::W_OFFLINE * px / 2, u(land ? 76 : 70), px, mascot::OFFLINE, BG);
  // deitada na CYD a coluna tem ~130 px: o título quebra em duas linhas e o resto desce junto
  // de cima para baixo pelas alturas das fontes: na tela densa as posições fixas se sobreporiam
  const int titleY = u(land ? 166 : 160);
  int subY = titleY + title2("Sem dados do computador", lcx, titleY, lw - 2 * M(), TEXT);
  const bool usb = hostlink::usbPlugged(), port = usb && hostlink::portOpen();
  const uint32_t id = hostlink::lastIdMs();
  const bool daemon = port && id && millis() - id < 15000;
  if (app::lastFrameMs) snprintf(buf, sizeof buf, "Último dado há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "Nenhum dado desde que a placa ligou");
  String sub2, sub1 = wrap2(buf, gfx::SMALL, lw - 2 * M(), sub2);
  text(sub1.c_str(), lcx, subY, gfx::SMALL, MUTED, top_center);
  if (sub2.length()) text(sub2.c_str(), lcx, subY += below(gfx::SMALL, 18), gfx::SMALL, MUTED, top_center);
  subY += below(gfx::SMALL, 18);

  struct Step {
    const char *name, *ok, *wait;
    bool done;
  } steps[] = {
      {"Cabo USB", "O computador reconheceu a placa", "Gire o plugue USB-C ou troque o cabo", usb},
      {"Programa no computador", "Abriu a porta e se apresentou", "Rode o instalador de tokenpulse.s.gy/comecar", daemon},
      {"Dados", "Chegando", "Esperando o primeiro envio", false},
  };
  int y = land ? u(64) : max(u(236), subY + u(16));
  bool blocked = false;
  for (auto& st : steps) {
    const bool cur = !st.done && !blocked;  // o primeiro passo que falta é o que importa agora
    const uint16_t c = st.done ? GREEN : cur ? AMBER : DIM;
    stepIcon(sx + u(10), y + u(10), st.done ? 0 : cur ? 1 : 2);
    text(st.name, sx + u(32), y, gfx::LABEL, st.done || cur ? TEXT : DIM);
    int lines = 0;
    if (st.done || cur) {  // a dica pode ser longa: até duas linhas na largura disponível
      const int maxW = sw - u(32), y1 = y + below(gfx::LABEL, 22);
      String l2, l1 = wrap2(st.done ? st.ok : st.wait, gfx::SMALL, maxW, l2);
      text(fit(l1.c_str(), gfx::SMALL, maxW).c_str(), sx + u(32), y1, gfx::SMALL, c);
      if (l2.length()) text(fit(l2.c_str(), gfx::SMALL, maxW).c_str(), sx + u(32), y1 + below(gfx::SMALL, 18), gfx::SMALL, c);
      lines = l2.length() ? 2 : 1;
    }
    if (!st.done) blocked = true;
    y += max(u(land ? 76 : 70), below(gfx::LABEL, 22) + lines * below(gfx::SMALL, 18) + u(12));
  }
  snprintf(buf, sizeof buf, "%s · %s", board::NICK, hostlink::firmware());
  text(buf, W() / 2, gfx::height() - u(16), gfx::SMALL, DIM, middle_center);
}

// Workspaces e sessões em grade, em páginas com quantos cartões a tela comporta sem apertar. O cartão
// precisa do mascote, de duas linhas de título e do status; a grade sai disso.
// sem a linha do modelo (header com sub = false), a grade sobe para logo abaixo do título
// o título com "‹" volta: a faixa dele tem pelo menos 6 mm, e o conteúdo começa abaixo dela
int BACK_H() { return TAPPX() * 4 / 5; }
int GRID_Y() { return max(app::live() ? u(10) + max(gfx::lineHeight(gfx::TITLE), u(22)) + u(12) : u(62), BACK_H() + u(4)); }
int CARD_GAP() { return u(8); }
int GRID_BOTTOM() { return gfx::height() - u(30); }  // acima dos pontos de página
int COLS() { return W() >= 460 ? 3 : 2; }
int CARD_MIN_H() { return u(56) + 2 * gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(8); }
int ROWS() {
  const int fixed = board::GRID_ROWS[L() ? 1 : 0];
  return fixed ? fixed : max(1, (GRID_BOTTOM() - GRID_Y() + CARD_GAP()) / (CARD_MIN_H() + CARD_GAP()));
}
int PER_PAGE() { return COLS() * ROWS(); }
int CARD_H() { return (GRID_BOTTOM() - GRID_Y() - (ROWS() - 1) * CARD_GAP()) / ROWS(); }
int CARD_W() { return (W() - 2 * M() - (COLS() - 1) * CARD_GAP()) / COLS(); }
bool COMPACT() { return CARD_H() < CARD_MIN_H(); }  // cartão baixo: mascote menor e título numa linha
int MASCOT_PX() { return COMPACT() ? 2 : max(2, u(4)); }
int TITLE_Y() { return COMPACT() ? u(10) + 8 * MASCOT_PX() + u(4) : u(56); }  // a partir do topo do cartão
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
int cardX(int i) { return M() + (i % COLS()) * (CARD_W() + CARD_GAP()); }
int cardY(int i) { return GRID_Y() + (i / COLS()) * (CARD_H() + CARD_GAP()); }
// O cartão da grade sob o toque (0 a PER_PAGE() - 1), ou -1
int cardAt(int x, int y) {
  const int row = (y - GRID_Y()) / (CARD_H() + CARD_GAP()), col = (x - M()) / (CARD_W() + CARD_GAP());
  return y < GRID_Y() || row >= ROWS() || col < 0 || col >= COLS() ? -1 : row * COLS() + col;
}

// Índices em sess.rows das sessões do workspace, por prioridade: pedem aprovação, trabalhando, sua vez,
// paradas (a ordem que o daemon já manda). Devolve quantas.
int sessionsOf(int ws, int* ids, int max) {
  JsonArrayConst rows = app::frame["sess"]["rows"];
  int n = 0;
  for (int r = 0; r < (int)rows.size() && n < max; r++)
    if ((int)rows[r][5] == ws) ids[n++] = r;
  return n;
}

mascot::Mood moodOf(const char* state) {
  return !strcmp(state, "work") ? mascot::WORK : !strcmp(state, "turn") ? mascot::TURN : !strcmp(state, "block") ? mascot::BLOCK : mascot::IDLE;
}
uint16_t moodBg(mascot::Mood m) { return m == mascot::BLOCK ? blend(BG, RED, 0.16f) : blend(BG, TRACK, m == mascot::IDLE ? 0.35f : 0.6f); }

int pagesOf(int ws) {
  int ids[48];
  const int n = ws < 0 ? (int)app::frame["sess"]["ws"].size() : sessionsOf(ws, ids, 48);
  return max(1, (n + PER_PAGE() - 1) / PER_PAGE());
}

void sessionCard(int r, int k, bool desk = false);  // mais abaixo

// O ícone do Claude Desktop, desenhado aqui: asterisco branco de 12 raios numa caixa laranja arredondada.
void spark(int cx, int cy, int r) {
  constexpr uint16_t ORANGE_CLAUDE = 0xDBAA;  // #D97757
  fb().fillSmoothRoundRect(cx - r, cy - r, 2 * r, 2 * r, r * 9 / 20, ORANGE_CLAUDE);
  const float s = r * 0.66f;  // os raios ocupam o meio da caixa
  for (int i = 0; i < 12; i++) {
    const float a = i * PI / 6 + 0.13f, len = s * (i % 2 ? 0.72f : 1.f);
    gfx::wideLine(cx + cosf(a) * s * 0.18f, cy + sinf(a) * s * 0.18f, cx + cosf(a) * len, cy + sinf(a) * len,
                  max(1.f, s * 0.11f), 0xFFFF);
  }
}

// O Claude Desktop com uma sessão só aparece na lista como a própria sessão: o toque só consegue abrir o
// app, então não há o que escolher num nível a mais. Devolve o índice em sess.rows, ou -1.
int directSession(int ws) {
  JsonArrayConst w = app::frame["sess"]["ws"][ws];
  int ids[1];
  return (int)(w[6] | 0) == 1 && (int)(w[1] | 0) == 1 && sessionsOf(ws, ids, 1) == 1 ? ids[0] : -1;
}

// Pasta em pixel art, do tamanho do mascote: o ícone de um workspace, na cor do estado mais urgente.
void folder(int x, int y, int px, uint16_t c, uint16_t bg) {
  static const char* const ROWS[] = {".oooo.......", "oooooo......", "oooooooooooo", "o----------o",
                                     "oooooooooooo", "oooooooooooo", "oooooooooooo", "oooooooooooo"};
  const uint16_t flap = blend(c, bg, 0.45f);  // a dobra da frente, mais escura
  for (int r = 0; r < 8; r++)
    for (int k = 0; k < 12; k++)
      if (ROWS[r][k] != '.') fb().fillRect(x + k * px, y + r * px, px, px, ROWS[r][k] == '-' ? flap : c);
}

// Página da lista de workspaces: cada cartão com a pasta na cor do estado mais urgente, o nome e quantas
// sessões estão em cada estado.
void drawWorkspacePage(bool live, int sp) {
  char buf[48];
  JsonArrayConst ws = app::frame["sess"]["ws"];
  const int n = ws.size(), per = PER_PAGE(), pages = pagesOf(-1);
  sp = constrain(sp, 0, pages - 1);
  if (pages > 1) snprintf(buf, sizeof buf, "Sessões · %d · %d/%d", (int)(app::frame["sess"]["n"] | 0), sp + 1, pages);
  else snprintf(buf, sizeof buf, "Sessões · %d", (int)(app::frame["sess"]["n"] | 0));
  header(live, buf, false, false);
  if (n == 0) {
    const int px = u(6);
    mascot::draw(W() / 2 - mascot::W * px / 2, gfx::height() / 2 - u(40), px, mascot::IDLE, BG);
    text("Nenhuma sessão aberta", W() / 2, gfx::height() / 2 + u(40), gfx::LABEL, DIM, middle_center);
  }
  for (int k = 0, i = sp * per; i < min(sp * per + per, n); i++, k++) {
    JsonArrayConst w = ws[i];
    const int total = w[1], work = w[2], block = w[3], turn = w[4];
    const int direct = directSession(i);
    if (direct >= 0) {  // o Claude Desktop com uma sessão só: o cartão já é ela
      sessionCard(direct, k, true);
      continue;
    }
    const mascot::Mood mood = block ? mascot::BLOCK : turn ? mascot::TURN : work ? mascot::WORK : mascot::IDLE;
    const uint16_t bg = moodBg(mood);
    const int x = cardX(k), y = cardY(k), cw = CARD_W(), ch = CARD_H(), pad = u(12);
    const float in = anim::entrance(k);
    card(x, y + (int)((1 - in) * u(16)), cw, ch, u(14), bg, (int)(w[5] | 0));
    if (in < 0.9f) continue;
    const uint16_t fc = mood == mascot::BLOCK ? RED : mood == mascot::TURN ? YELLOW : mood == mascot::WORK ? GREEN : blend(DIM, bg, 0.3f);
    // o ícone: o asterisco no Claude Desktop (1), o mascote do Codex no workspace dele (2), a pasta nos outros
    if ((int)(w[6] | 0) == 1) spark(x + pad + 4 * MASCOT_PX(), y + (COMPACT() ? u(10) : u(14)) + 4 * MASCOT_PX(), 4 * MASCOT_PX() + MASCOT_PX() / 2);
    else if ((int)(w[6] | 0) == 2) mascot::draw(x + pad, y + (COMPACT() ? u(10) : u(14)), MASCOT_PX(), mood, bg, k, true);
    else folder(x + pad, y + (COMPACT() ? u(10) : u(14)), MASCOT_PX(), fc, bg);
    snprintf(buf, sizeof buf, total == 1 ? "1 sessão" : "%d sessões", total);
    // cartão estreito (Guition em pé): "3 sessões" passaria por cima do ícone, então fica só o número
    if (gfx::textWidth(buf, gfx::SMALL) > cw - u(10) - (pad + 12 * MASCOT_PX() + u(8))) snprintf(buf, sizeof buf, "%d", total);
    text(buf, x + cw - u(10), y + (COMPACT() ? u(10) : u(14)), gfx::SMALL, DIM, top_right, bg);
    const int maxW = cw - 2 * pad, t1 = y + TITLE_Y(), t2 = t1 + max(u(19), gfx::lineHeight(gfx::LABEL) - 1);
    String l2, name = wrap2(w[0] | "", gfx::LABEL, maxW, l2);
    if (COMPACT()) name = w[0] | "", l2 = "";
    text(fit(name.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t1, gfx::LABEL, TEXT, top_left, bg);
    if (l2.length()) text(fit(l2.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t2, gfx::LABEL, TEXT, top_left, bg);
    // um ponto colorido com a contagem de cada estado que importa (verde trabalha, vermelho pede aprovação,
    // amarelo é sua vez), as mesmas cores do SmallTV
    const int ly = y + ch - max(u(20), gfx::lineHeight(gfx::SMALL) + 2);
    const struct { int count; uint16_t c; } chips[] = {{block, RED}, {work, GREEN}, {turn, YELLOW}};
    int cx = x + pad;
    if (!work && !block && !turn) text("Todas paradas", cx, ly, gfx::SMALL, DIM, top_left, bg);
    for (auto& c : chips) {
      if (!c.count) continue;
      const int d = max(3, u(4));
      fb().fillSmoothCircle(cx + d, ly + gfx::lineHeight(gfx::SMALL) / 2 - 1, d, c.c);
      snprintf(buf, sizeof buf, "%d", c.count);
      text(buf, cx + 3 * d, ly, gfx::SMALL, c.c, top_left, bg);
      cx += 3 * d + gfx::textWidth(buf, gfx::SMALL) + u(10);
    }
  }
}

// Cartão de uma sessão (índice em sess.rows) na posição k da grade. desk: o asterisco do Claude Desktop no
// lugar do mascote (o cartão do Desktop na lista de workspaces).
void sessionCard(int r, int k, bool desk) {
  char buf[48];
  JsonArrayConst row = app::frame["sess"]["rows"][r];
  const int cw = CARD_W(), ch = CARD_H(), x = cardX(k), y = cardY(k);
  const mascot::Mood mood = moodOf(row[2] | "");
  const uint16_t cardBg = moodBg(mood);
  // tocado agora há pouco: a borda pisca enquanto o Mac traz a sessão para a frente
  const bool asked = focusReqIdx == r && anim::since(focusReqMs, 900) < 1;
  const bool focused = (int)(row[7] | 0) || (asked && (millis() - focusReqMs) / 150 % 2);
  const float in = anim::entrance(k);
  card(x, y + (int)((1 - in) * u(16)), cw, ch, u(14), cardBg, focused);
  if (in < 0.9f) return;
  const int pad = u(12), statusY = y + ch - max(u(20), gfx::lineHeight(gfx::SMALL) + 2);
  const int iconY = y + (COMPACT() ? u(10) : u(14)), px = MASCOT_PX();
  if (desk) spark(x + pad + 4 * px, iconY + 4 * px, 4 * px + px / 2);
  const char* origin = row[1] | "CLI";
  const bool codex = !strcmp(origin, "Codex");
  if (!desk) mascot::draw(x + pad, iconY, px, mood, cardBg, k, codex);
  const int by = y + (COMPACT() ? u(6) : u(12)), bw = gfx::textWidth(origin, gfx::SMALL) + 2 * u(10);
  const uint16_t oc = !strcmp(origin, "Desk") ? ACCENT : codex ? 0x150F : !strcmp(origin, "Multica") ? 0x9C7E : GREEN;
  badge(origin, x + cw - u(10), by, oc, cardBg);
  // os tokens na linha do selo, à esquerda dele, depois do mascote: embaixo do selo encostariam no título
  const int room = cw - u(10) - bw - u(8) - (pad + mascot::W * MASCOT_PX() + u(14));
  if (room >= gfx::textWidth(row[3] | "", gfx::SMALL))  // inteiro ou nada: "135,…" não diz nada
    text(row[3] | "", x + cw - u(10) - bw - u(8), by + u(11), gfx::SMALL, DIM, middle_right, cardBg);
  // título da sessão em até duas linhas, quebrado nas palavras; se a segunda não couber, uma linha só
  const int maxW = cw - u(24), t1 = y + TITLE_Y(), t2 = t1 + max(u(19), gfx::lineHeight(gfx::LABEL) - 1);
  String l2, name = wrap2(row[0] | "", gfx::LABEL, maxW, l2);
  if (l2.length() && (COMPACT() || t2 + gfx::lineHeight(gfx::LABEL) > statusY)) name = row[0] | "", l2 = "";
  text(fit(name.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t1, gfx::LABEL, TEXT, top_left, cardBg);
  if (l2.length()) text(fit(l2.c_str(), gfx::LABEL, maxW).c_str(), x + pad, t2, gfx::LABEL, TEXT, top_left, cardBg);
  const int mins = row[4];
  if (mood == mascot::WORK) strcpy(buf, "Trabalhando");
  else if (mood == mascot::BLOCK) strcpy(buf, "Aprovar");  // o mesmo rótulo do SmallTV
  else if (mood == mascot::TURN) snprintf(buf, sizeof buf, mins ? "Sua vez · %d min" : "Sua vez", mins);
  else if (mins < 0) strcpy(buf, "Parada");
  else if (mins >= 120) snprintf(buf, sizeof buf, "Parada há %dh", mins / 60);
  else snprintf(buf, sizeof buf, "Parada há %d min", mins);
  const uint16_t sc = mood == mascot::WORK ? GREEN : mood == mascot::BLOCK ? RED : mood == mascot::TURN ? YELLOW : DIM;
  text(fit(buf, gfx::SMALL, maxW).c_str(), x + pad, statusY, gfx::SMALL, sc, top_left, cardBg);
}

// Página das sessões de um workspace, por prioridade. O título volta para a lista.
void drawSessionPage(bool live, int ws, int sp) {
  char buf[48];
  JsonArrayConst w = app::frame["sess"]["ws"][ws];
  int ids[48];
  const int n = sessionsOf(ws, ids, 48), per = PER_PAGE(), pages = pagesOf(ws);
  sp = constrain(sp, 0, pages - 1);
  if (pages > 1) snprintf(buf, sizeof buf, "‹ %s · %d/%d", (const char*)(w[0] | "?"), sp + 1, pages);
  else snprintf(buf, sizeof buf, "‹ %s", (const char*)(w[0] | "?"));
  header(live, buf, false, false);
  for (int k = 0, idx = sp * per; idx < min(sp * per + per, n); idx++, k++) sessionCard(ids[idx], k);
}

void drawSessions(bool live, int page, int ws, int sp) {
  gfx::clear();
  shownWs = ws, shownSessPage = sp;
  if (ws < 0) drawWorkspacePage(live, sp);
  else drawSessionPage(live, ws, sp);
  dots(page);
}

int workspaceAt(int x, int y, int sp) {
  const int k = cardAt(x, y), i = sp * PER_PAGE() + k;
  return k >= 0 && i < (int)app::frame["sess"]["ws"].size() ? i : -1;
}
bool backAt(int, int y) { return y < GRID_Y() - u(4); }

// Pede ao Mac para trazer a sessão (índice em sess.rows) para a frente: o painel do herdr ou o Claude Desktop.
bool focusRow(int r) {
  const char* sid = app::frame["sess"]["rows"][r][6] | "";
  if (!*sid) return false;
  char cmd[32];
  snprintf(cmd, sizeof cmd, "ACTION focus %s", sid);
  hostlink::action(cmd);
  focusReqIdx = r, focusReqMs = millis();
  return true;
}

bool sessionTap(int x, int y, int ws, int sp) {
  int ids[48];
  const int n = sessionsOf(ws, ids, 48), k = cardAt(x, y), i = sp * PER_PAGE() + k;
  return k >= 0 && i < n && focusRow(ids[i]);
}

int modelAt(int x, int y) {
  for (int i = 0; i < (int)app::frame["mods"].size(); i++) {
    const Cell c = modelCell(i);
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + (L() ? u(118) : modelPitch() - u(6))) return i;
  }
  return -1;
}

// Detalhe de um modelo: tokens e custo de hoje, de onde vêm (entrada, saída, cache) e quais projetos usaram.
void drawModelDetail(bool live, int page, int idx) {
  JsonArrayConst m = app::frame["mods"][idx];
  if (m.isNull()) return drawModels(live, page);
  char buf[64];
  gfx::clear();
  snprintf(buf, sizeof buf, "‹ %s", (const char*)(m[0] | "?"));
  header(live, buf, false, SUB_WARN);
  const int up = lift();
  // em pé, tudo numa coluna; deitada, composição à esquerda e projetos à direita
  const bool land = L();
  const int gap = u(24), lw = land ? (W() - 2 * M() - gap) / 2 : W() - 2 * M(), rx = land ? M() + lw + gap : M();
  text(m[1], M(), (u(68) - up), gfx::NUM, TEXT);
  snprintf(buf, sizeof buf, "%d%%", (int)m[2]);
  text(buf, M() + lw, (u(72) - up), gfx::TITLE, ACCENT, top_right);
  if (settings::v.hideCost) strcpy(buf, "Tokens hoje");
  else if (*(m[7] | "")) snprintf(buf, sizeof buf, "Tokens hoje · %s na API", (const char*)m[7]);
  else strcpy(buf, "Tokens hoje");  // modelo sem preço na tabela (os do Codex)
  text(fit(buf, gfx::SMALL, lw).c_str(), M(), (u(68) - up) + below(gfx::NUM, 38), gfx::SMALL, MUTED);

  // composição numa barra empilhada
  const char* names[] = {"Entrada", "Saída", "Cache lido", "Cache escrito"};
  const uint16_t cols[] = {blend(ACCENT, TEXT, 0.5f), ACCENT, GREEN, AMBER};
  float v[4], sum = 0;
  for (int k = 0; k < 4; k++) sum += v[k] = parseTokens(m[3 + k]);
  const int bx = M(), by = (u(140) - up), bw = lw, bh = u(14), r = bh / 2;
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
  for (int k = 0; k < (*(m[6] | "") ? 4 : 3); k++) {  // sem "Cache escrito" nos modelos do Codex
    const int y = (u(168) - up) + k * below(gfx::LABEL, 26), d = max(3, u(5));
    fb().fillSmoothCircle(M() + d, y + gfx::lineHeight(gfx::LABEL) / 2 - 1, d, cols[k]);
    text(names[k], M() + u(18), y, gfx::LABEL, MUTED);
    snprintf(buf, sizeof buf, "%s · %d%%", (const char*)m[3 + k], sum > 0 ? (int)lroundf(100 * v[k] / sum) : 0);
    // coluna estreita (média deitada): sem espaço para o %, fica só o número
    if (u(18) + gfx::textWidth(names[k], gfx::LABEL) + u(10) + gfx::textWidth(buf, gfx::LABEL) > lw) strcpy(buf, m[3 + k] | "");
    text(buf, M() + lw, y, gfx::LABEL, TEXT, top_right);
  }

  const int py0 = land ? (u(68) - up) : (u(168) - up) + 4 * below(gfx::LABEL, 26) + u(12), pw = W() - M() - rx;
  text(gfx::textWidth("Projetos que mais usaram", gfx::SMALL) <= pw ? "Projetos que mais usaram" : "Projetos", rx, py0, gfx::SMALL, DIM);
  JsonArrayConst projs = m[8];
  float top = 1;
  for (JsonArrayConst p : projs) top = max(top, parseTokens(p[1]));
  int i = 0;
  for (JsonArrayConst p : projs) {
    const int y = py0 + below(gfx::SMALL, 22) + i * max(u(44), gfx::lineHeight(gfx::LABEL) + u(14));
    if (y + gfx::lineHeight(gfx::LABEL) + u(10) > gfx::height() - u(24)) break;
    text(fit(p[0], gfx::LABEL, pw - u(70)).c_str(), rx, y, gfx::LABEL, TEXT);
    text(p[1], rx + pw, y, gfx::LABEL, MUTED, top_right);
    gfx::bar(rx, y + below(gfx::LABEL, 24), pw, max(4, u(6)), anim::entrance(i + 1) * parseTokens(p[1]) / top, ACCENT);
    i++;
  }
  dots(page);
}

// Histórico: tokens por dia nos últimos 14 dias, a barra tocada (ou hoje) em destaque.
// deitada o gráfico fica mais largo e mais baixo, e o total e a média dividem uma linha
// em pé, abaixo do número do dia e da linha Claude · Codex (a barra mais alta não pode cobrir essa linha)
int CH_Y() {
  if (L()) return u(128) - lift();
  const int top = u(66) + below(gfx::SMALL, 18) + gfx::lineHeight(gfx::NUM) + u(2) + gfx::lineHeight(gfx::SMALL) + u(10);
  return max(u(150), top) - lift();
}
// o gráfico cresce para cima no espaço da linha que saiu, e encolhe para o rodapé (3 linhas em pé) caber acima dos pontos
int CH_H() {
  const int foot = u(34) + (L() ? gfx::lineHeight(gfx::SMALL) : below(gfx::SMALL, 20) + below(gfx::SMALL, 24) + gfx::lineHeight(gfx::SMALL));
  return min(u(L() ? 112 : 200) + lift(), gfx::height() - u(24) - foot - CH_Y());
}
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
  header(live, "Histórico · 14 dias", false, SUB_WARN);
  JsonArrayConst hist = app::frame["hist"];
  const int n = hist.size();
  if (!n) return dots(page);
  if (sel < 0 || sel >= n) sel = n - 1;
  float peak = 0.1f, sumTok = 0;
  int sumCost = 0;
  for (JsonArrayConst d : hist) peak = max(peak, (float)d[2]), sumTok += (float)d[2], sumCost += (int)d[3];

  JsonArrayConst s = hist[sel];
  snprintf(buf, sizeof buf, "%s %d%s", (const char*)s[0], (int)s[1], sel == n - 1 ? " · hoje" : "");
  buf[0] = toupper(buf[0]);  // o dia vem do Mac em minúsculas ("qua"), como nas barras
  text(buf, M(), (u(66) - lift()), gfx::SMALL, MUTED);
  snprintf(buf, sizeof buf, "%.1fM", (float)s[2]);
  String tok = buf;
  tok.replace('.', ',');
  const int ny = (u(66) - lift()) + below(gfx::SMALL, 18);
  text(tok.c_str(), M(), ny, gfx::NUM, TEXT);
  const bool cost = !settings::v.hideCost && !codexOnly();  // o Codex não tem preço na tabela
  snprintf(buf, sizeof buf, "US$ %d", (int)s[3]);
  if (cost) text(buf, W() - M(), ny, gfx::NUM, ACCENT, top_right);
  if (s.size() > 4) {  // com o Codex o número é a soma; esta linha separa os dois (o custo é só o do Claude)
    snprintf(buf, sizeof buf, "Claude %.1fM · Codex %.1fM", (float)s[2] - (float)s[4], (float)s[4]);
    String split = buf;
    split.replace('.', ',');
    // deitada não há espaço entre o número e as barras: a linha vai à direita do dia
    // deitada a linha divide espaço com o dia: sem largura, fica só o Codex (o Claude é o resto do total)
    if (L() && gfx::textWidth(split.c_str(), gfx::SMALL) > W() / 2 - M()) split = split.substring(split.indexOf("Codex"));
    if (L()) text(fit(split.c_str(), gfx::SMALL, W() / 2 - M()).c_str(), W() - M(), (u(66) - lift()), gfx::SMALL, DIM, top_right);
    else text(fit(split.c_str(), gfx::SMALL, W() - 2 * M()).c_str(), M(), ny + gfx::lineHeight(gfx::NUM) + u(2), gfx::SMALL, DIM);
  }

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
  snprintf(buf, sizeof buf, "Média de US$ %d por dia", sumCost / n);
  const bool oneLine = gfx::textWidth(total.c_str(), gfx::SMALL) + u(16) + gfx::textWidth(buf, gfx::SMALL) <= W() - 2 * M();
  if (cost && L()) {  // deitada só há uma linha: a média entra se couber ao lado do total
    if (oneLine) text(buf, W() - M(), ty, gfx::SMALL, MUTED, top_right);
  } else if (cost) {
    snprintf(buf, sizeof buf, "Média de US$ %d por dia", sumCost / n);
    text(buf, M(), ty + below(gfx::SMALL, 20), gfx::SMALL, MUTED);
    text("Custo equivalente na API", M(), ty + below(gfx::SMALL, 20) + below(gfx::SMALL, 24), gfx::SMALL, DIM);
  }
  dots(page);
}

// Ajustes, como no celular: uma lista de grupos com o valor de agora e "›"; o toque abre o grupo, com
// o nome em cima e as opções num controle segmentado embaixo. Deitada, os controles de até 3 opções
// ficam em duas colunas, para caber em 320 px. Toque no título do grupo volta para a lista.
namespace {
enum Kind : uint8_t { SEG, MASK, CYCLE, RADIO };  // um valor; vários ligados (bits); um entre muitos, com ‹ ›; um por linha
struct Row {
  const char* name;
  uint8_t* value;
  const char* opts[5];
  uint8_t count;
  Kind kind = SEG;
};
// sem a linha de baixo do cabeçalho (ajustes ao vivo), o conteúdo sobe para o espaço dela
int SET_Y() { return max(subLine ? u(L() ? 58 : 62) : u(46), BACK_H() + u(4)); }
// Sobrando altura no grupo, cada linha comum cresce até u(18): o controle mais alto erra menos o toque.
int rowGrow = 0;
// Alvo de toque mínimo: 9 mm na tela física, qualquer que seja a densidade. Os ajustes rolam em vez de encolher
// os controles para caber.
int TAP() { return TAPPX(); }
int SEG_TOP() { return below(gfx::LABEL, 24); }
int SEG_H() { return max(u(38), TAP()) + rowGrow; }
int ROW_H() { return SEG_TOP() + SEG_H() + u(14); }
int CYCLE_H() { return max(u(62), TAP()); }  // o seletor de som, numa linha só: alto para o toque nos ‹ › não errar

// Som do alerta, no controle: 0 desligado, 1 a 3 o volume (settings::web.alerts e .volume).
uint8_t soundLevel = 0;

Row screenRows[] = {
    {"Brilho", &settings::v.bright, {"25%", "50%", "75%", "100%"}, 4},
    // OLED não tem "nunca": parada, ela precisa do descanso para não marcar
    {"Escurecer quando parada", &settings::v.idle, {"5 min", "10 min", "30 min", "Nunca"}, board::PROFILE.oled ? 3 : 4},
    {"Tema", &settings::v.theme, {"Auto", "Claro", "Escuro"}, 3},
    {"Custo em US$", &settings::v.hideCost, {"Mostrar", "Ocultar"}, 2},
    // a última, porque com IMU a tela gira sozinha e a linha some. 4 rotações (ESP32-2432S028R): o cabo
    // pode sair por qualquer lado, e o nome é o lado dele, que é o que a pessoa vê (rotação 1 deixa o cabo à direita)
    board::ROTATIONS == 4 ? Row{"Lado do cabo USB", &settings::v.orient, {"Embaixo", "Direita", "Em cima", "Esquerda"}, 4}
                          : Row{"Posição", &settings::v.orient, {"Vertical", "Horizontal"}, 2},
};
// Com bateria, o brilho tem dois: no USB e na bateria (que economiza). O resto é o mesmo da lista de cima.
Row screenRowsBat[] = {
    {"Brilho no USB", &settings::v.bright, {"25%", "50%", "75%", "100%"}, 4},
    {"Brilho na bateria", &settings::web.batBright, {"25%", "50%", "70%", "100%"}, 4},
    screenRows[1], screenRows[2], screenRows[3], screenRows[4],
};
// Os sons: o volume vale para todos; cada aviso escolhe o seu (os da janela também podem ficar desligados).
Row soundRows[] = {
    {"Volume", &soundLevel, {"Desligado", "Baixo", "Médio", "Alto"}, 4},
    {"Sessão", &settings::web.sound, {}, sound::COUNT, CYCLE},
    {"5h esgotou", &settings::web.soundOut, {}, sound::COUNT + 1, CYCLE},
    {"5h renovou", &settings::web.soundNew, {}, sound::COUNT + 1, CYCLE},
    {"Semana", &settings::web.soundWeek, {}, sound::COUNT + 1, CYCLE},
    {"Bateria fraca", &settings::web.soundBat, {}, sound::COUNT + 1, CYCLE},
    {"Computador sem dados", &settings::web.soundMac, {}, sound::COUNT + 1, CYCLE},
    {"Bluetooth", &settings::web.soundBle, {}, sound::COUNT + 1, CYCLE},
};
// Provedores: o ajuste mora no daemon (vale para o Mac todo) e a placa só pede a troca; o frame diz o atual
uint8_t provSel = 0;
const char* const PROV_MODES[] = {"auto", "claude", "codex", "both"};
Row provRows[] = {{"O que a placa mostra", &provSel, {"Automático", "Só Claude", "Só Codex", "Claude + Codex"}, 4, RADIO}};
// Multica: o ajuste também mora no daemon; os nomes dos workspaces vêm do frame ("mcs")
uint8_t mcOn = 0, mcWs = 0;
uint32_t mcTapMs = 0;  // o toque vale até o frame com a mudança chegar do Mac
Row mcRows[] = {
    {"Página do Multica", &mcOn, {"Mostrar", "Ocultar"}, 2},
    {"Workspace", &mcWs, {}, 1, RADIO},
};
void syncMc() {
  JsonVariantConst m = app::frame["mcs"];
  JsonArrayConst w = m["w"];
  if (!mcTapMs || millis() - mcTapMs > 3000) mcOn = (int)(m["on"] | 0) ? 0 : 1, mcWs = m["i"] | 0;
  mcRows[1].count = constrain((int)w.size(), 1, 5);
  for (int k = 0; k < 5; k++) mcRows[1].opts[k] = k < (int)w.size() ? (const char*)(w[k] | "") : "";
}
Row restRows[] = {
    {"Relógio", &settings::web.restSecs, {"HH:MM", "HH:MM:SS"}, 2},
    {"Telas do descanso", &settings::web.restPages, {"Relógio", "Uso", "Sessões", "Modelos", "14 dias"}, 5, MASK},
};
struct Group {
  const char* name;
  Row* rows;
  uint8_t n;
};
const Group GROUPS[] = {
    board::PROFILE.battery ? Group{"Tela", screenRowsBat, board::PROFILE.imu ? 5 : 6} : Group{"Tela", screenRows, board::PROFILE.imu ? 4 : 5},
    {"Som", soundRows, 8},
    {"Descanso", restRows, 2},
    {"Cartão SD", nullptr, 0},  // tela própria: o estado do cartão e o botão de formatar
    {"Provedores", provRows, 1},
    {"Multica", mcRows, 2},
};
int group = -1;  // -1: a lista

// Os itens da lista, na ordem: grupos que a placa tem, depois Conta e Sobre.
enum Item : int8_t { G_SCREEN, G_SOUND, G_REST, G_SD, G_PROV, G_MC, ACCOUNT, ABOUT };
int items(Item* out) {
  int n = 0;
  out[n++] = G_SCREEN;
  if (board::PROFILE.speaker) out[n++] = G_SOUND;
  if (board::PROFILE.oled) out[n++] = G_REST;
  if (board::PROFILE.sd) out[n++] = G_SD;
  if ((int)(app::frame["cxok"] | 0)) out[n++] = G_PROV;  // só quando o Mac tem o Codex
  if (!app::frame["mcs"].isNull()) out[n++] = G_MC;       // só quando o Mac tem o Multica
  if (!codexOnly()) out[n++] = ACCOUNT;                   // a conta é a org do Claude
  out[n++] = ABOUT;
  return n;
}
int MASK_LINE() { return max(u(46), TAP() + u(6)); }  // uma opção da lista de interruptores
// botões lado a lado só quando cada um tem largura de sobra; senão, uma opção por linha
Kind kindOf(const Row& r) {
  return r.kind == SEG && r.count >= 3 && (W() - 2 * M()) / r.count < TAP() * 3 / 2 ? RADIO : r.kind;
}
// a lista de escolha única ocupa a altura que sobra na tela: opção alta erra menos o toque
int lineOf(const Row& r) {
  return kindOf(r) == RADIO && r.value == &provSel ? max(MASK_LINE(), min(u(100), (gfx::height() - SET_Y() - SEG_TOP() - u(12)) / r.count)) : MASK_LINE();
}
// a lista de interruptores é mais alta que uma linha comum; o seletor de som, sem título em cima, mais baixo
int rowHeight(const Row& r) {
  const Kind k = kindOf(r);
  return k == MASK || k == RADIO ? SEG_TOP() + r.count * lineOf(r) + u(10) : k == CYCLE ? CYCLE_H() + u(10) : ROW_H();
}
// a lista de grupos: linhas de pelo menos 9 mm; com mais grupos que a altura, ela rola
int listRowH(int n) { return max(TAP() + u(8), min(u(72), (gfx::height() - SET_Y() - M()) / max(1, n))); }
int listFirst = 0;  // o primeiro grupo à mostra
int listFits(int n) { return max(1, (gfx::height() - SET_Y() - u(6)) / listRowH(n)); }

// Onde fica a linha i do grupo: o nome em y, o controle em y + SEG_TOP, uma linha embaixo da outra.
Cell cell(const Group& g, int i) {
  int y = SET_Y();
  for (int k = 0; k < i; k++) y += rowHeight(g.rows[k]);
  return Cell{M(), y, W() - 2 * M()};
}

// Distribui a altura livre do grupo entre as linhas comuns (a lista de interruptores fica como está).
void fitRows(const Group& g) {
  rowGrow = 0;
  const Cell last = cell(g, g.n - 1);
  const int bottom = last.y + rowHeight(g.rows[g.n - 1]), free = gfx::height() - u(10) - bottom;
  int lines = 0;
  for (int k = 0; k < g.n; k++) lines += kindOf(g.rows[k]) != MASK && kindOf(g.rows[k]) != RADIO;
  if (lines && free > 0) rowGrow = min(u(18), free / lines);
}

// Grupo maior que a tela (o Som da AMOLED): rola em linhas inteiras, a de cima sempre logo abaixo do título
int groupTop = 0;  // a primeira linha à mostra
bool bootHint(int g) { return g == G_SOUND && board::PROFILE.oled; }  // a AMOLED tem o BOOT livre para silenciar
int groupShift(const Group& g) { return cell(g, groupTop).y - SET_Y(); }
int groupMaxTop(int gi) {
  const Group& g = GROUPS[gi];
  const int end = cell(g, g.n - 1).y + rowHeight(g.rows[g.n - 1]) + (bootHint(gi) ? gfx::lineHeight(gfx::SMALL) + u(10) : 0);
  for (int k = 0; k < g.n; k++)
    if (cell(g, k).x == M() && end - (cell(g, k).y - SET_Y()) <= gfx::height() - u(8)) return k;
  return g.n - 1;
}

void summary(Item it, char* buf, size_t n) {
  const auto& v = settings::v;
  const auto& w = settings::web;
  static const char* BRIGHT[] = {"25%", "50%", "75%", "100%"};
  static const char* THEME[] = {"Auto", "Claro", "Escuro"};
  static const char* VOL[] = {"", "Baixo", "Médio", "Alto"};
  static const char* PAGE[] = {"Relógio", "Uso", "Sessões", "Modelos", "14 dias"};
  switch (it) {
    case G_SCREEN: snprintf(buf, n, "%s · %s", BRIGHT[min<int>(v.bright, 3)], THEME[min<int>(v.theme, 2)]); break;
    case G_SOUND:
      if (w.alerts & 2) snprintf(buf, n, "%s · %s", sound::NAMES[w.sound], VOL[w.volume]);
      else strlcpy(buf, "Desligado", n);
      break;
    case G_REST: {
      buf[0] = 0;
      for (int i = 0; i < 5; i++)
        if (w.restPages >> i & 1) {
          const size_t at = strlen(buf);
          snprintf(buf + at, n - at, "%s%s", at ? ", " : "", PAGE[i]);
          if (at) buf[at + 2] = tolower(buf[at + 2]);  // só a primeira da lista com maiúscula
        }
      break;
    }
    case G_SD:
      if (sd::state() == sd::READY) snprintf(buf, n, "%d dias · %lu MB livres", sd::days(), (unsigned long)sd::freeMB());
      else strlcpy(buf, sd::state() == sd::NONE ? "Sem cartão" : "Não montou", n);
      break;
    case G_PROV: {
      static const char* NAMES[] = {"Automático", "Só Claude", "Só Codex", "Claude + Codex"};
      strlcpy(buf, NAMES[provSel], n);
      break;
    }
    case G_MC: strlcpy(buf, mcOn ? "Oculta" : (const char*)(app::frame["mcs"]["w"][mcWs] | ""), n); break;
    case ACCOUNT: strlcpy(buf, app::frame["org"] | "", n); break;
    default: strlcpy(buf, hostlink::firmware(), n);
  }
}

void drawRow(const Row& r, Cell c, uint16_t track) {
  const Kind kind = kindOf(r);
  const int sh = kind == CYCLE ? CYCLE_H() : SEG_H();
  if (kind == CYCLE) {  // numa linha só: o nome à esquerda, o som à direita entre ‹ ›; os lados trocam
    const gfx::Font F = fontFor(sh);
    fb().fillSmoothRoundRect(c.x, c.y, c.w, sh, min(sh / 2, u(28)), track);
    const int cy = c.y + sh / 2;
    text(r.name, c.x + u(16), cy, F, MUTED, middle_left, track);
    const bool off = *r.value >= sound::COUNT;
    text("›", c.x + c.w - u(16), cy, gfx::TITLE, ACCENT, middle_right, track);
    const int vr = c.x + c.w - u(40);
    text(off ? "Desligado" : sound::NAMES[*r.value], vr, cy, F, off ? DIM : TEXT, middle_right, track);
    text("‹", vr - gfx::textWidth(off ? "Desligado" : sound::NAMES[*r.value], F) - u(14), cy, gfx::TITLE, ACCENT, middle_right, track);
    return;
  }
  const int sy = c.y + SEG_TOP();
  text(r.name, c.x, c.y, gfx::LABEL, MUTED);
  fb().fillSmoothRoundRect(c.x, sy, c.w, sh, min(sh / 2, u(28)), track);
  if (kind == MASK || kind == RADIO) {  // uma opção por linha: o interruptor à direita, ou o círculo da escolha à esquerda
    const int ln = lineOf(r);
    // o círculo e o texto crescem com a linha: numa linha alta (tela densa) eles não ficam perdidos nela
    const int rr = max(u(10), ln / 5), cxr = c.x + max(u(26), rr + u(16));
    const gfx::Font OF = fontFor(ln) == gfx::SMALL ? gfx::LABEL : fontFor(ln);
    fb().fillSmoothRoundRect(c.x, sy, c.w, r.count * ln - u(6), u(12), track);
    for (int k = 0; k < r.count; k++) {
      const int ly = sy + k * ln, mid = ly + (ln - u(6)) / 2;
      if (kind == RADIO) {
        const bool on = *r.value == k;
        fb().fillSmoothCircle(cxr, mid, rr, on ? ACCENT : MUTED);
        fb().fillSmoothCircle(cxr, mid, rr - max(2, rr / 5), track);
        if (on) fb().fillSmoothCircle(cxr, mid, rr / 2, ACCENT);
        text(r.opts[k], cxr + rr + u(14), mid, OF, TEXT, middle_left, track);
        if (k < r.count - 1) fb().drawFastHLine(c.x + u(14), ly + ln - u(3), c.w - u(28), BG);
        continue;
      }
      const bool on = *r.value >> k & 1;
      text(r.opts[k], c.x + u(14), mid, OF, TEXT, middle_left, track);
      const int sh2 = max(u(24), ln * 2 / 5), sw = sh2 * 11 / 6, sx = c.x + c.w - u(14) - sw;
      fb().fillSmoothRoundRect(sx, mid - sh2 / 2, sw, sh2, sh2 / 2, on ? ACCENT : blend(track, TEXT, 0.25f));
      fb().fillSmoothCircle(on ? sx + sw - sh2 / 2 : sx + sh2 / 2, mid, sh2 / 2 - u(3), on ? BG : MUTED);
      if (k < r.count - 1) fb().drawFastHLine(c.x + u(14), ly + MASK_LINE() - u(3), c.w - u(28), BG);
    }
    return;
  }
  for (int k = 0; k < r.count; k++) {
    // bordas calculadas do mesmo jeito para todos: segmentos de largura igual, texto no centro
    const int x0 = c.x + c.w * k / r.count, x1 = c.x + c.w * (k + 1) / r.count;
    const bool on = kind == MASK ? *r.value >> k & 1 : *r.value == k;
    if (on) fb().fillSmoothRoundRect(x0 + 3, sy + 3, x1 - x0 - 6, sh - 6, min((sh - 6) / 2, u(25)), ACCENT);
    text(r.opts[k], (x0 + x1) / 2, sy + sh / 2, fontFor(sh), on ? BG : TEXT, middle_center, on ? ACCENT : track);
  }
}
}  // namespace

void settingsHome() { group = -1, groupTop = 0, listFirst = 0; }
bool settingsScroll(int dir) {
  if (group < 0) {  // a lista de grupos: 2 linhas por gesto
    Item list[8];
    const int n = items(list), to = constrain(listFirst + 2 * dir, 0, max(0, n - listFits(n)));
    if (to == listFirst) return false;
    listFirst = to;
    return true;
  }
  if (group == G_SD) return false;
  const Group& g = GROUPS[group];
  int to = groupTop;
  for (int moved = 0; moved < 3;) {  // 3 linhas por gesto, contando só as que começam na margem
    const int nx = to + dir;
    if (nx < 0 || nx > groupMaxTop(group)) break;
    to = nx;
    if (cell(g, to).x == M()) moved++;
  }
  while (to > 0 && cell(g, to).x != M()) to--;
  if (to == groupTop) return false;
  groupTop = to;
  return true;
}

// Cartão SD: o estado e, quando ele não montou, formatar com dois toques (apaga tudo o que houver nele).
uint32_t sdArmedMs = 0;
int SD_BTN_Y() { return SET_Y() + 3 * below(gfx::LABEL, 26) + u(16); }
void drawSd(bool live, uint16_t track) {
  header(live, "‹ Cartão SD", false, false);
  char buf[48];
  const int x = M(), w = W() - 2 * M(), step = below(gfx::LABEL, 26);
  int y = SET_Y();
  if (sd::state() == sd::READY) {
    snprintf(buf, sizeof buf, "%d dias de histórico guardados", sd::days());
    text(buf, x, y, gfx::LABEL, TEXT), y += step;
    snprintf(buf, sizeof buf, "%lu MB livres", (unsigned long)sd::freeMB());
    text(buf, x, y, gfx::LABEL, MUTED), y += step;
    text("Grava o dia de hoje a cada 10 min", x, y, gfx::SMALL, DIM);
    return;
  }
  text(sd::state() == sd::NONE ? "Nenhum cartão encontrado" : "O cartão não montou", x, y, gfx::LABEL, TEXT), y += step;
  text("Sem formato, corrompido ou fora do leitor", x, y, gfx::SMALL, MUTED), y += step;
  const bool armed = sdArmedMs && millis() - sdArmedMs < 3000;
  const int bh = u(48), by = SD_BTN_Y();
  fb().fillSmoothRoundRect(x, by, w, bh, bh / 2, armed ? RED : track);
  text(armed ? "Toque de novo: apaga tudo no cartão" : "Formatar o cartão", x + w / 2, by + bh / 2, gfx::LABEL,
       armed ? BG : TEXT, middle_center, armed ? RED : track);
}
int sdTap(int y) {
  const int by = SD_BTN_Y();
  if (sd::state() == sd::READY || y < by || y > by + u(48)) return -1;
  if (sdArmedMs && millis() - sdArmedMs < 3000) {
    sdArmedMs = 0;
    gfx::loading(gfx::BOOT, "Cartão SD", "Formatando…", 0.5f);
    sd::format();
  } else {
    sdArmedMs = millis();
  }
  return 0;
}

void drawSettings(bool live) {
  gfx::clear();
  const uint16_t track = blend(BG, TRACK, 0.7f);
  soundLevel = settings::web.alerts & 2 ? settings::web.volume : 0;
  for (int i = 0; i < 4; i++)
    if (!strcmp(app::frame["prov"] | "auto", PROV_MODES[i])) provSel = i;
  syncMc();
  if (group == G_SD) return drawSd(live, track);
  if (group >= 0) {
    const Group& g = GROUPS[group];
    char title[32];
    snprintf(title, sizeof title, "‹ %s", g.name);
    header(live, title, false, false);  // antes do layout: ele decide onde as linhas começam
    fitRows(g);
    const int dy = groupShift(g);
    for (int i = groupTop; i < g.n; i++) {
      Cell c = cell(g, i);
      c.y -= dy;
      drawRow(g.rows[i], c, track);
    }
    if (bootHint(group)) {
      const int y = cell(g, g.n - 1).y + rowHeight(g.rows[g.n - 1]) - dy + u(4);
      text(app::muted() ? "Silenciado pelo BOOT: toque nele de novo" : "BOOT silencia por 1 h (não segure ao ligar)",
           M(), y, gfx::SMALL, app::muted() ? RED : DIM);
    }
    if (const int mt = groupMaxTop(group)) {  // barra de rolagem: o grupo continua para baixo
      const int th = gfx::height() - SET_Y() - u(8), bh = th * 2 / 3;
      fb().fillSmoothRoundRect(W() - u(5), SET_Y() + (th - bh) * groupTop / mt, u(3), bh, u(2), TRACK);
    }
    return;
  }
  header(live, "Ajustes", false, false);
  Item list[8];
  const int n = items(list), rh = listRowH(n), fits = listFits(n);
  listFirst = constrain(listFirst, 0, max(0, n - fits));
  const int w = W() - 2 * M() - (n > fits ? u(10) : 0);
  // linha alta: a fonte sobe um tamanho junto com ela, para o texto acompanhar o alvo
  const bool big = rh - u(8) >= 2 * gfx::lineHeight(gfx::TITLE) - u(4);
  const gfx::Font NF = big ? gfx::TITLE : gfx::LABEL, SF = gfx::LABEL;
  static const char* NAMES[] = {"Tela", "Som", "Descanso", "Cartão SD", "Provedores", "Multica", "Conta", "Sobre"};
  char buf[48];
  for (int i = listFirst; i < min(n, listFirst + fits); i++) {
    const int y = SET_Y() + (i - listFirst) * rh, h = rh - u(8);
    fb().fillSmoothRoundRect(M(), y, w, h, u(10), track);
    text(NAMES[list[i]], M() + u(14), y + h / 2, NF, TEXT, middle_left, track);
    text("›", M() + w - u(12), y + h / 2, gfx::TITLE, MUTED, middle_right, track);
    summary(list[i], buf, sizeof buf);
    const int room = w - u(60) - gfx::textWidth(NAMES[list[i]], NF);
    if (gfx::textWidth(buf, SF) > room) {  // não coube: o resumo curto em vez de cortar com reticências
      if (list[i] == G_REST) snprintf(buf, sizeof buf, "%d telas", __builtin_popcount(settings::web.restPages & 31));
      if (list[i] == G_PROV && provSel == 3) strlcpy(buf, "Os dois", sizeof buf);
    }
    text(fit(buf, SF, room).c_str(), M() + w - u(32), y + h / 2, SF, MUTED, middle_right, track);
  }
  if (n > fits) {  // barra de rolagem: há grupos abaixo ou acima
    const int th = fits * rh - u(8), bh = max(u(16), th * fits / n);
    fb().fillSmoothRoundRect(W() - M() - u(4), SET_Y() + (th - bh) * listFirst / (n - fits), u(4), bh, u(2), TRACK);
  }
}

// Toque nos ajustes: 0 trocou um ajuste (ou abriu/fechou um grupo), 1 abriu o Sobre, 2 abriu a Conta, -1 nada
int settingsTap(int x, int y) {
  if (group < 0) {
    Item list[8];
    const int n = items(list), rh = listRowH(n);
    const int i = listFirst + (y - SET_Y()) / rh;
    if (y < SET_Y() || i >= min(n, listFirst + listFits(n)) || x < M() || x > W() - M()) return -1;
    if (list[i] == ACCOUNT) return 2;
    if (list[i] == ABOUT) return 1;
    group = list[i], groupTop = 0;
    return 0;
  }
  if (y < SET_Y() - u(4)) return void(group = -1), 0;  // o título: volta para a lista
  if (group == G_SD) return sdTap(y);
  syncMc();
  const Group& g = GROUPS[group];
  fitRows(g);
  y += groupShift(g);
  for (int i = groupTop; i < g.n; i++) {
    const Cell c = cell(g, i);
    Row& r = g.rows[i];
    const Kind kind = kindOf(r);
    const int top = kind == CYCLE ? c.y : c.y + SEG_TOP();
    const int bottom = kind == MASK || kind == RADIO ? top + r.count * lineOf(r) : top + SEG_H() + 4;
    if (x < c.x || x >= c.x + c.w || y < top - 6 || y > bottom) continue;
    auto& web = settings::web;
    if (kind == CYCLE) {  // à direita avança; na metade da esquerda (o nome e o ‹) volta
      *r.value = (*r.value + (x - c.x < c.w / 2 ? r.count - 1 : 1)) % r.count;
    } else {
      const int k = kind == MASK || kind == RADIO ? constrain((y - top) / lineOf(r), 0, r.count - 1)
                                   : min<int>((x - c.x) * r.count / c.w, r.count - 1);
      if (kind == MASK) {
        if ((*r.value ^ 1 << k) & 31) *r.value ^= 1 << k;  // ao menos uma tela fica ligada
      } else if (r.value == &provSel) {
        provSel = k;
        char cmd[24];
        snprintf(cmd, sizeof cmd, "ACTION prov %s", PROV_MODES[k]);
        hostlink::action(cmd);
      } else if (r.value == &mcOn || r.value == &mcWs) {
        *r.value = k, mcTapMs = millis();
        char cmd[24];
        snprintf(cmd, sizeof cmd, "ACTION %s %d", r.value == &mcOn ? "mc" : "mcws", r.value == &mcOn ? !k : k);
        hostlink::action(cmd);
      } else if (r.value == &soundLevel) {
        web.alerts = k ? web.alerts | 2 : web.alerts & ~2;
        if (k) web.volume = k;
      } else {
        const bool turned = r.value == &settings::v.orient && *r.value != k;
        *r.value = k;
        if (turned) gfx::setRotation(k), anim::reset();
      }
    }
    settings::save();
    settings::saveWeb();
    if (group == G_SOUND && web.alerts & 2 && *r.value < sound::COUNT) {  // a prévia, como o aviso toca
      const bool session = r.value == &web.sound || r.kind != CYCLE;
      board::play(r.kind == CYCLE ? *r.value : web.sound, settings::volumePct(), session ? 0 : 2000, session ? 2 : 1);
    }
    return 0;
  }
  return -1;
}

// Sobre: o que a placa sabe de si mesma, e reiniciar com toque duplo de confirmação.
// Deitada, os itens vão em 2 colunas, rótulo em cima e valor embaixo; os botões vêm logo depois.
// o Sobre em letra de leitura: o nome do item em LABEL e o valor em TITLE (antes SMALL e LABEL, pequenos demais)
int aboutStep() { return L() || gfx::ui() == gfx::LARGE ? gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::TITLE) + u(8) : below(gfx::TITLE, 30) + u(4); }
int ABOUT_BTN_H() { return max(u(48), TAP()); }
// com bateria, ela é o primeiro item e a página web desce uma posição
int aboutItems() { return board::PROFILE.battery ? 9 : 8; }
int aboutWebItem() { return aboutItems() - 2; }
int aboutRows() { return aboutItems(); }  // uma coluna, que rola: em duas os valores eram cortados
int RESTART_Y() { return L() ? max(u(232), SET_Y() + u(4) + aboutRows() * aboutStep() + u(6)) : max(u(384), SET_Y() + u(4) + aboutRows() * aboutStep() + u(6)); }
// Rolagem em linhas inteiras: o item de cima sempre começa logo abaixo do cabeçalho
int aboutOff = 0;
int aboutMaxOff() { return max(0, (RESTART_Y() + ABOUT_BTN_H() + u(12) - gfx::height() + aboutStep() - 1) / aboutStep()); }
void aboutTop() { aboutOff = 0; }
bool aboutScroll(int dir) {
  const int to = constrain(aboutOff + dir * 3, 0, aboutMaxOff());
  if (to == aboutOff) return false;
  aboutOff = to;
  return true;
}

// Previsão de carga completa pelo ritmo desde que a carga começou; precisa subir 2% para estimar
int chargeEtaMin(const board::PowerState& ps) {
  static uint32_t t0 = 0;
  static int p0 = -1;
  if (!ps.charging || ps.pct < 0) return p0 = -1, -1;
  if (p0 < 0 || ps.pct < p0) t0 = millis(), p0 = ps.pct;
  const int gained = ps.pct - p0;
  return gained < 2 ? -1 : (100 - ps.pct) * (millis() - t0) / gained / 60000;
}

void drawAbout(bool live, bool armed) {
  char buf[64];
  gfx::clear();
  header(live, "Sobre", false, false);
  const uint32_t up = millis() / 60000;
  if (up >= 60) snprintf(buf, sizeof buf, "%luh%02lu", up / 60, up % 60);
  else snprintf(buf, sizeof buf, "%lu min", up);
  String upTxt = buf;
  // livre de quanto: só o livre ("6168 KB") parecia memória acabando
  snprintf(buf, sizeof buf, "%.1f de %.1f MB livres", ESP.getFreePsram() / 1048576.f, ESP.getPsramSize() / 1048576.f);
  String psram = psramFound() ? String(buf) : String("Não tem");
  psram.replace('.', ',');
  // o perfil da placa: o que ela tem de hardware além da tela
  const auto& pf = board::PROFILE;
  String hw = String(pf.touch ? "touch · " : "") + (pf.imu ? "giro · " : "") + (pf.led ? "LED · " : "") + (pf.button ? "botão · " : "");
  hw = hw.length() ? hw.substring(0, hw.length() - 4) : String("Só a tela");
  snprintf(buf, sizeof buf, "%u de %u KB livres", ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);
  String ram = buf;
  if (live) strcpy(buf, "Ao vivo pela USB");
  else if (app::lastFrameMs) snprintf(buf, sizeof buf, "Sem dados há %lu min", (millis() - app::lastFrameMs) / 60000);
  else strcpy(buf, "Esperando o computador");
  String link = buf;
  // a entrada da página web: o endereço, ou o convite para ligar o WiFi (toque no item)
  String web = webui::online() ? String(webui::host()) + ".local" : webui::apOn() ? String("WiFi ") + webui::apName()
                                                                                  : String("Toque para ligar o WiFi");
  board::PowerState ps;
  String bat = "Sem bateria";
  if (board::powerState(ps) && ps.battery) {
    const int eta = chargeEtaMin(ps);
    const int left = power::batteryLeftMin();
    if (!ps.usb && left < 0) snprintf(buf, sizeof buf, "%d%% · medindo a duração", ps.pct);
    else if (!ps.usb && left >= 60) snprintf(buf, sizeof buf, "%d%% · dura ~%dh%02d", ps.pct, left / 60, left % 60);
    else if (!ps.usb) snprintf(buf, sizeof buf, "%d%% · dura ~%d min", ps.pct, max(left, 1));
    else if (!ps.charging) snprintf(buf, sizeof buf, "%d%% · carregada", ps.pct);
    // na tensão constante o % fica parado perto de 95% por até 1 h: o ritmo não serve mais para prever
    else if (ps.finishing) snprintf(buf, sizeof buf, "%d%% · terminando", ps.pct);
    else if (eta < 0) snprintf(buf, sizeof buf, "%d%% · carregando", ps.pct);
    else if (eta >= 60) snprintf(buf, sizeof buf, "%d%% · cheia em ~%dh%02d", ps.pct, eta / 60, eta % 60);
    else snprintf(buf, sizeof buf, "%d%% · cheia em ~%d min", ps.pct, max(eta, 1));
    bat = buf;
  }
  const char* all[][2] = {
      {"Bateria", bat.c_str()},
      {"Placa", board::NICK}, {"Modelo", board::NAME},          {"Firmware", hostlink::firmware()}, {"Ligada há", upTxt.c_str()},
      {"PSRAM", psram.c_str()}, {"RAM interna", ram.c_str()},  {"Página web", web.c_str()},
      {"Conexão", link.c_str()},
  };
  const auto* items = all + (board::PROFILE.battery ? 0 : 1);
  const int n = aboutItems(), rows = aboutRows(), off = aboutOff, top = SET_Y() + u(4);
  // tela grande: rótulo em cima, valor embaixo. Na média em pé os pares não cabem empilhados: um por linha.
  // Deitada: empilhados, em 2 colunas.
  const bool stacked = L() || gfx::ui() == gfx::LARGE;
  const int cols = 1, colW = W() - 2 * M(), step = aboutStep();
  for (int k = 0; k < n; k++) {
    const auto& it = items[k];
    const int row = k % rows - off, x = M() + (k / rows) * (colW + u(16)), y = top + row * step;
    if (row < 0) continue;  // rolou para cima do cabeçalho
    // o item da página web é o único que se toca: na cor de destaque e com "›", como os botões
    const bool link = k == aboutWebItem();
    const String val = link ? String(it[1]) + " ›" : String(it[1]);
    const uint16_t c = link ? ACCENT : TEXT;
    text(it[0], x, y, gfx::LABEL, DIM);
    if (stacked) text(fit(val.c_str(), gfx::TITLE, colW).c_str(), x, y + gfx::lineHeight(gfx::LABEL), gfx::TITLE, c);
    else text(fit(val.c_str(), gfx::TITLE, W() - 2 * M() - u(120)).c_str(), W() - M(), y, gfx::TITLE, c, top_right);
  }
  if (const int mo = aboutMaxOff()) {  // barra de rolagem: a página continua para baixo
    const int th = gfx::height() - top - u(8), bh = th * rows / (rows + mo);
    fb().fillSmoothRoundRect(W() - u(5), top + (th - bh) * off / mo, u(3), bh, u(2), TRACK);
  }
  // touch resistivo: calibrar o toque divide a linha com o reiniciar
  const bool cal = board::PROFILE.calibrate;
  const int bw = cal ? (W() - 2 * M() - u(8)) / 2 : W() - 2 * M(), rx = W() - M() - bw, by = RESTART_Y() - off * step, bh = ABOUT_BTN_H();
  const uint16_t c = armed ? RED : blend(BG, TRACK, 0.7f);
  if (cal) {
    const uint16_t cb = blend(BG, TRACK, 0.7f);
    fb().fillSmoothRoundRect(M(), by, bw, bh, u(12), cb);
    text("Calibrar o toque", M() + bw / 2, by + bh / 2, fontFor(bh) == gfx::SMALL ? gfx::LABEL : fontFor(bh), TEXT, middle_center, cb);
  }
  fb().fillSmoothRoundRect(rx, by, bw, bh, u(12), c);
  const char* label = armed ? (cal ? "Toque de novo" : "Toque de novo para reiniciar") : "Reiniciar a placa";
  text(label, rx + bw / 2, by + bh / 2, fontFor(bh) == gfx::SMALL ? gfx::LABEL : fontFor(bh), armed ? BG : TEXT, middle_center, c);
  // a dica só entra se couber embaixo dos botões (na média deitada não cabe)
  if (by + bh + gfx::lineHeight(gfx::SMALL) < gfx::height() - u(8))
    text("Toque fora para voltar aos ajustes", W() / 2, gfx::height() - u(22), gfx::SMALL, DIM, middle_center);
}

int aboutButtonAt(int x, int y) {
  // o item "página web": a mesma conta de posição do drawAbout, na página rolada
  y += aboutOff * aboutStep();
  const int cols = 1, colW = W() - 2 * M(), k = aboutWebItem();
  const int ix = M() + (k / aboutRows()) * (colW + u(16)), iy = SET_Y() + u(4) + k % aboutRows() * aboutStep();
  if (x >= ix && x < ix + colW && y >= iy - u(4) && y < iy + aboutStep()) return 3;
  if (y < RESTART_Y() || y >= RESTART_Y() + ABOUT_BTN_H()) return 0;
  return board::PROFILE.calibrate && x < W() / 2 ? 2 : 1;
}
bool gearAt(int x, int y) { return x >= W() - max(u(64), TAPPX()) && y < max(u(52), TAPPX() * 2 / 3); }
bool orgTitleAt(int x, int y) { return x < W() / 2 && y < max(u(30), TAPPX() * 2 / 3); }
// a pílula "esgotado · trocar" é baixa: o toque vale numa faixa de 9 mm centrada nela
bool suggestionAt(int x, int y) {
  const int pad = max(0, (TAPPX() - sugH) / 2);
  return sugW && x >= sugX && x < sugX + sugW && y >= sugY - pad && y < sugY + sugH + pad;
}
void setGearActive(bool on) { gearActive = on; }

// Conta: a org atual, as contas que o cswap conhece e o login de uma org nova. A troca roda no Mac
// (cswap ou o login do Claude Code no navegador); a placa só pede e acompanha.
// Trocar de org. Em pé, tudo numa coluna; deitada, a org atual, o status e "entrar em outra org" à esquerda e a
// lista à direita. Cada org é uma linha inteira tocável de 9 mm ou mais (antes era um botão "Trocar" de 2 mm na
// AMOLED); o primeiro toque arma e o segundo, em 5 s, confirma, porque a troca vale para todas as sessões do Mac.
struct AccCols {
  int lw, rx, rw;  // largura da coluna esquerda; início e largura da lista
};
// uma coluna em pé e deitada: em duas, a lista ficava estreita e cortava os nomes
AccCols accCols() { return {W() - 2 * M(), M(), W() - 2 * M()}; }
int armedAcct = -2;  // número da conta armada; 0 = "entrar em outra org"; -2 nada
uint32_t armedMs = 0;
bool armed(int num) { return armedAcct == num && millis() - armedMs < 5000; }
int accRowY = 0, accRowH = 0, accRows = 0, accNewY = 0, accNewH = 0;  // o último desenho, para o toque

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
  header(live, "‹ Trocar de org", false, false);
  const AccCols ac = accCols();
  const int lw = ac.lw, rx = ac.rx, rw = ac.rw;
  const uint16_t card = blend(BG, TRACK, 0.6f);
  int y = SET_Y();  // a org atual é a linha "Em uso" da lista: sem bloco repetido em cima

  JsonArrayConst act = app::frame["act"];
  const char* st = act[0] | "";
  const char* target = act[1] | "";
  if (*st) {  // em que pé está a troca: uma linha, ou os três passos do login no navegador
    const bool login = !strcmp(st, "login");
    const int lh = gfx::lineHeight(gfx::LABEL) + u(8), h = (login ? 3 : 1) * lh + u(16);
    fb().fillSmoothRoundRect(M(), y, lw, h, u(12), card);
    int ly = y + u(8) + lh / 2;
    auto line = [&](int icon, const char* t, uint16_t c) {
      stepIcon(M() + u(20), ly, icon);
      text(fit(t, gfx::LABEL, lw - u(48)).c_str(), M() + u(38), ly, gfx::LABEL, c, middle_left, card);
      ly += lh;
    };
    if (!strcmp(st, "switching")) snprintf(buf, sizeof buf, "Trocando para %s…", target), line(1, buf, TEXT);
    else if (login) {
      line(0, "Login aberto no navegador do computador", TEXT);
      snprintf(buf, sizeof buf, "Escolha a org %s e autorize", *target ? target : "desejada");
      line(1, buf, AMBER);
      line(2, "A placa confirma a troca", DIM);
    } else if (!strcmp(st, "done")) snprintf(buf, sizeof buf, "Pronto: agora na org %s", (const char*)(app::frame["org"] | "?")), line(0, buf, GREEN);
    else snprintf(buf, sizeof buf, "Não deu certo: %s", (const char*)(act[2] | "erro")), line(2, buf, RED);
    y += h + u(12);
  }

  // "entrar em outra org": embaixo da coluna da esquerda deitada; em pé, no fim da página
  accNewH = max(u(52), TAP());
  JsonArrayConst accts = app::frame["accts"];
  // sem título em cima da lista quando há orgs: a linha "Em uso" já diz o que é, e a altura vai para as linhas
  const int rowsTop = accts.size() ? y + u(4) : y + gfx::lineHeight(gfx::LABEL) + u(4);
  accRowH = max(TAP() + u(6), 2 * gfx::lineHeight(gfx::LABEL) + u(20));
  const int listBottom = gfx::height() - u(8) - accNewH - u(6);
  accRows = max(1, (listBottom - rowsTop) / accRowH);
  accRowY = rowsTop;
  if (accts.size() == 0) {
    text("Orgs salvas no cswap", rx, rowsTop - gfx::lineHeight(gfx::LABEL) - u(2), gfx::LABEL, DIM);
    text("Nenhuma: rode cswap add no computador", rx, rowsTop + u(6), gfx::LABEL, MUTED);
  }
  const gfx::Font NF = accRowH >= 2 * gfx::lineHeight(gfx::TITLE) + u(16) ? gfx::TITLE : gfx::LABEL;
  int i = 0;
  for (JsonArrayConst a : accts) {
    if (i == accRows) break;
    const int ry = rowsTop + i * accRowH, h = accRowH - u(8), num = a[0];
    const bool active = (int)a[2], relogin = (int)a[3], arm = armed(num);
    const uint16_t rowBg = arm ? blend(BG, RED, 0.18f) : active ? blend(BG, GREEN, 0.12f) : card;
    fb().fillSmoothRoundRect(rx, ry, rw, h, u(12), rowBg);
    const char* tag = active ? "Em uso" : arm ? "Confirmar ›" : relogin ? "Login expirado" : "Trocar ›";
    const uint16_t tc = active ? GREEN : arm ? RED : relogin ? AMBER : ACCENT;
    const int tw = gfx::textWidth(tag, gfx::LABEL);
    // nome e detalhe empilhados no meio da linha, que é alta nas telas densas
    const int mid = ry + h / 2;
    text(tag, rx + rw - u(14), mid - u(2), gfx::LABEL, tc, bottom_right, rowBg);
    text(fit(a[1], NF, rw - tw - u(40)).c_str(), rx + u(14), mid - u(2), NF, TEXT, bottom_left, rowBg);
    if (arm) strcpy(buf, "Toque de novo: troca todas as sessões do computador");
    else if ((int)a[4] >= 0) snprintf(buf, sizeof buf, "5h %d%% · semana %d%% · %s", (int)a[4], (int)a[5], ago(a[6]).c_str());
    else strcpy(buf, active ? "Buscando o %…" : "Sem % recente");
    text(fit(buf, gfx::LABEL, rw - u(28)).c_str(), rx + u(14), mid + u(4), gfx::LABEL, arm ? RED : DIM, top_left, rowBg);
    i++;
  }
  accNewY = min(listBottom, rowsTop + max(1, i) * accRowH) + u(4);
  const bool armNew = armed(0);
  const uint16_t nb = armNew ? blend(BG, RED, 0.18f) : card;
  fb().fillSmoothRoundRect(M(), accNewY, lw, accNewH, min(accNewH / 2, u(26)), nb);
  const char* newTxt = armNew ? "Toque de novo: o login troca todas as sessões" : "Entrar em outra org";
  text(fit(newTxt, gfx::LABEL, lw - u(20)).c_str(), M() + lw / 2, accNewY + accNewH / 2, gfx::LABEL, armNew ? RED : TEXT, middle_center, nb);
}

// Toque na Conta: arma ou confirma uma troca (true) ou devolve false para fechar o painel (o título "‹" fecha).
bool accountTap(int x, int y) {
  auto confirm = [](int num, const char* action) {
    if (armed(num)) {
      hostlink::action(action);
      armedAcct = -2;
    } else {
      armedAcct = num, armedMs = millis();
    }
  };
  if (y < SET_Y() - u(4)) return false;
  const AccCols ac = accCols();
  if (y >= accNewY && y < accNewY + accNewH && x >= M() && x < M() + ac.lw) {
    confirm(0, "ACTION login");
    return true;
  }
  JsonArrayConst accts = app::frame["accts"];
  const int i = (y - accRowY) / max(1, accRowH);
  if (x < ac.rx || y < accRowY || i >= min(accRows, (int)accts.size())) return true;  // fora de alvo: fica
  if ((int)accts[i][2]) return true;  // a org em uso
  char cmd[32];
  snprintf(cmd, sizeof cmd, "ACTION switch %d", (int)accts[i][0]);
  confirm(accts[i][0], cmd);
  return true;
}

// Multica: a página dos agentes do workspace, os projetos, as issues de um projeto e o detalhe de um cartão. O
// bloco "mc" do frame: w workspace, r rodando, c [rodando, na fila, em revisão, falhou], a os cartões [agente,
// provedor, issue, título, tipo (0 rodando, 1 na fila, 2 em revisão, 3 falhou), minutos, tokens, projeto,
// prioridade, tentativa, PRs, modelo, erro], p [projeto, estado, feitas, total, em revisão] e, com um projeto
// aberto, pi [índice, [[issue, título, status (0 revisão … 5 feita), quem, minutos]]].
namespace {
int mcAgentY = 0, mcAgentH = 0, mcAgentN = 0, mcProjY = 0, mcTileY = 0, mcTileH = 0;  // o último desenho, para o toque
int mcShown[16];     // índices em mc.a dos cartões à mostra
int mcAgentTop = 0;  // o primeiro cartão à mostra: com mais cartões que a altura, a lista rola na vertical
int mcFiltered = 0;  // quantos cartões o filtro de agora tem
int mcRowY = 0, mcRowH = 0, mcRowN = 0, mcRowIdx[10];
int mcRowTop = 0, mcRowAll = 0;  // a primeira issue à mostra e quantas o projeto tem: a lista rola na vertical
int mcTapped = -1;   // a issue tocada: a borda pisca enquanto o Mac abre ela
uint32_t mcTappedMs = 0;
int mcProjTop = 0, mcProjH = 0, mcProjN = 0, mcProjFirst = 0, mcProjAll = 0;  // a lista de projetos rola como as outras
int PROJ_H() { return max(max(u(58), gfx::lineHeight(gfx::LABEL) + u(26)), TAPPX()); }
const char* const MC_TILES[] = {"Rodando", "Na fila", "Revisão", "Falhou"};
const char* const MC_EMPTY[] = {"Nenhum agente rodando", "Nada na fila", "Nenhuma issue sua em revisão",
                                "Nenhuma run sua falhou em 24 h"};
// as cores do tema mudam com o claro e o escuro: lidas na hora, não guardadas
uint16_t mcColor(int k) { return k == 0 ? GREEN : k == 2 ? YELLOW : k == 3 ? RED : MUTED; }
// os status das issues de um projeto, na ordem do daemon
const char* const MC_STATUS[] = {"Revisão", "Bloqueada", "Fazendo", "A fazer", "Backlog", "Feita"};
uint16_t mcStatusColor(int st) { return st == 0 ? YELLOW : st == 1 ? RED : st == 2 ? GREEN : st == 3 ? MUTED : DIM; }

uint16_t providerColor(const char* p) { return !strcmp(p, "Codex") ? 0x150F : !strcmp(p, "Claude") ? ACCENT : MUTED; }

void ago(char* buf, size_t n, int mins) {
  if (mins >= 1440) snprintf(buf, n, "há %d d", mins / 1440);
  else if (mins >= 60) snprintf(buf, n, "há %d h", mins / 60);
  else if (mins >= 0) snprintf(buf, n, "há %d min", mins);
  else *buf = 0;
}

void mcCard(JsonArrayConst a, int k, int y, int h) {
  char buf[72], when[16];
  const int kind = a[4] | 0;
  const uint16_t bg = kind == 3 ? blend(BG, RED, 0.14f) : blend(BG, TRACK, kind == 1 ? 0.35f : 0.6f);
  const int x = M(), w = W() - 2 * M(), pad = u(12), px = max(2, min(u(5), (h - u(16)) / 8));
  const float in = anim::entrance(k);
  card(x, y + (int)((1 - in) * u(16)), w, h, u(12), bg, false);
  if (in < 0.9f) return;
  const char* prov = a[1] | "";
  const mascot::Mood mood = kind == 0 ? mascot::WORK : kind == 2 ? mascot::TURN : kind == 3 ? mascot::BLOCK : mascot::IDLE;
  mascot::draw(x + pad, y + (h - 8 * px) / 2, px, mood, bg, k, !strcmp(prov, "Codex"));
  const int tx = x + pad + mascot::W * px + u(12), maxW = x + w - pad - tx;
  const int bw = *prov ? gfx::textWidth(prov, gfx::SMALL) + 2 * u(10) : 0;
  // as três linhas formam um bloco centrado na altura do cartão (o cartão estica para ocupar a sobra da tela)
  const int block = 2 * gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(8);
  const int top = y + max(u(8), (h - block) / 2);
  if (*prov) badge(prov, x + w - u(10), top, providerColor(prov), bg);
  // baixo demais para três linhas (CYD deitada): a issue na primeira, ao lado do selo, e o estado embaixo
  const bool slim = h < 2 * gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(14);
  const int l1 = top, l3 = min(y + h - u(8), top + block) - gfx::lineHeight(gfx::SMALL);
  const int l2 = (l1 + gfx::lineHeight(gfx::LABEL) + l3) / 2 - gfx::lineHeight(gfx::LABEL) / 2;
  snprintf(buf, sizeof buf, "%s · %s", (const char*)(a[2] | ""), (const char*)(a[3] | ""));
  // sem espaço para "TP-42 · título", fica o título: o número da issue está no detalhe
  if (gfx::textWidth(buf, gfx::LABEL) > (slim ? maxW - bw - u(8) : maxW)) strlcpy(buf, a[3] | "", sizeof buf);
  if (slim) {
    text(fit(buf, gfx::LABEL, maxW - bw - u(8)).c_str(), tx, l1, gfx::LABEL, kind == 1 ? MUTED : TEXT, top_left, bg);
  } else {
    text(fit(a[0] | "", gfx::LABEL, maxW - bw - u(8)).c_str(), tx, l1, gfx::LABEL, TEXT, top_left, bg);
    text(fit(buf, gfx::LABEL, maxW).c_str(), tx, l2, gfx::LABEL, kind == 1 ? MUTED : TEXT, top_left, bg);
  }
  ago(when, sizeof when, a[5] | -1);
  if (kind == 0) {
    snprintf(buf, sizeof buf, "Rodando %s · %s tokens", when, (const char*)(a[6] | "--"));
    if (gfx::textWidth(buf, gfx::SMALL) > maxW) snprintf(buf, sizeof buf, "Rodando %s", when);  // os tokens estão no detalhe
  }
  else if (kind == 1) strcpy(buf, "Na fila");
  else if (kind == 2) snprintf(buf, sizeof buf, "Em revisão %s", when);
  else snprintf(buf, sizeof buf, *(a[12] | "") ? "Falhou %s · %s" : "Falhou %s", when, (const char*)(a[12] | ""));
  text(fit(buf, gfx::SMALL, maxW).c_str(), tx, l3, gfx::SMALL, mcColor(kind), top_left, bg);
}

void mcProjects(bool live, JsonVariantConst mc) {
  char buf[48];
  JsonArrayConst ps = mc["p"];
  const int n = ps.size();
  snprintf(buf, sizeof buf, "‹ Projetos · %d", n);
  header(live, buf, false, false);
  const uint16_t track = blend(BG, TRACK, 0.6f);
  const int x = M(), w = W() - 2 * M(), gap = u(10);
  mcProjTop = SET_Y();
  // altura fixa: com mais projetos que a tela, a lista rola em vez de espremer as linhas
  const int h = max(max(u(76), gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(34)), TAPPX());
  const int fits = max(1, (gfx::height() - mcProjTop - u(30) + gap) / (h + gap));
  mcProjH = h + gap, mcProjAll = n, mcProjFirst = constrain(mcProjFirst, 0, max(0, n - fits));
  mcProjN = min(n - mcProjFirst, fits);
  if (!n) text("Nenhum projeto com issues", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
  if (n > fits) {
    const int lh = fits * (h + gap) - gap, bh = max(u(16), lh * fits / n);
    fb().fillSmoothRoundRect(W() - u(5), mcProjTop + (lh - bh) * mcProjFirst / (n - fits), u(3), bh, u(2), TRACK);
  }
  for (int k = 0; k < mcProjN; k++) {
    JsonArrayConst p = ps[mcProjFirst + k];
    const int y = mcProjTop + k * (h + gap), done = p[2] | 0, total = max(1, (int)(p[3] | 1)), review = p[4] | 0;
    fb().fillSmoothRoundRect(x, y, w, h, u(10), track);
    const char* st = p[1] | "";
    const uint16_t sc = !strcmp(st, "Em andamento") ? GREEN : !strcmp(st, "Concluído") ? MUTED : DIM;
    // nome, barra e contagem num bloco centrado na linha: em linha alta, nada fica grudado nas bordas
    const int sw = gfx::textWidth(st, gfx::SMALL), bh = max(4, u(6)), gapv = u(6);
    const int blockH = gfx::lineHeight(gfx::LABEL) + gapv + bh + gapv + gfx::lineHeight(gfx::SMALL), top = y + (h - blockH) / 2;
    text(st, x + w - u(14), top, gfx::SMALL, sc, top_right, track);
    text(fit(p[0] | "", gfx::LABEL, w - u(42) - sw).c_str(), x + u(14), top, gfx::LABEL, TEXT, top_left, track);
    const int by = top + gfx::lineHeight(gfx::LABEL) + gapv, bw = w - u(28);
    fb().fillSmoothRoundRect(x + u(14), by, bw, bh, bh / 2, blend(track, TEXT, 0.12f));
    if (done) fb().fillSmoothRoundRect(x + u(14), by, max(bh, bw * done / total), bh, bh / 2, done >= total ? GREEN : ACCENT);
    snprintf(buf, sizeof buf, "%d de %d issues", done, (int)(p[3] | 0));
    const int my = by + bh + gapv;
    text(buf, x + u(14), my, gfx::SMALL, MUTED, top_left, track);
    if (review) {
      snprintf(buf, sizeof buf, review == 1 ? "1 em revisão" : "%d em revisão", review);
      text(buf, x + w - u(14), my, gfx::SMALL, YELLOW, top_right, track);
    }
  }
  if (n) text("Toque num projeto para ver as issues", W() / 2, gfx::height() - u(14), gfx::SMALL, DIM, middle_center);
}

// As issues de um projeto: as que pedem atenção primeiro (revisão, bloqueada, fazendo…). O daemon só manda as do
// projeto aberto; até elas chegarem, a tela espera. Tocar numa abre ela no Mac.
void mcProjectIssues(bool live, JsonVariantConst mc, int proj) {
  char buf[72], when[16];
  snprintf(buf, sizeof buf, "‹ %s", (const char*)(mc["p"][proj][0] | ""));
  header(live, buf, false, false);
  JsonArrayConst pi = mc["pi"];
  mcRowN = 0;
  if ((int)(pi[0] | -1) != proj) return text("Carregando…", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
  const uint16_t track = blend(BG, TRACK, 0.6f);
  const int x = M(), w = W() - 2 * M(), gap = u(8), foot = gfx::height() - u(30);
  mcRowY = SET_Y(), mcRowH = max(max(u(58), gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(18)), TAPPX()) + gap;
  JsonArrayConst rows = pi[1];
  const int fits = max(1, (foot - mcRowY + gap) / mcRowH);
  mcRowAll = rows.size(), mcRowTop = constrain(mcRowTop, 0, max(0, mcRowAll - fits));
  for (int idx = mcRowTop; idx < mcRowAll; idx++) {
    JsonArrayConst l = rows[idx];
    if (mcRowN == 10 || mcRowN == fits) break;
    const int y = mcRowY + mcRowN * mcRowH, h = mcRowH - gap, st = constrain((int)(l[2] | 4), 0, 5);
    mcRowIdx[mcRowN++] = idx;
    const bool lit = idx == mcTapped && millis() - mcTappedMs < 900 && (millis() - mcTappedMs) / 150 % 2 == 0;
    card(x, y, w, h, u(10), track, lit);
    const int sw = gfx::textWidth(MC_STATUS[st], gfx::SMALL);
    const int mid = y + h / 2;  // título e detalhe empilhados no meio da linha
    // título comprido: o estado desce para a linha de baixo e o título usa a largura toda
    const bool down = gfx::textWidth(l[1] | "", gfx::LABEL) > w - u(40) - sw;
    text(MC_STATUS[st], x + w - u(14), down ? mid + u(4) : mid - u(2), gfx::SMALL, mcStatusColor(st), down ? top_right : bottom_right, track);
    text(fit(l[1] | "", gfx::LABEL, down ? w - u(28) : w - u(40) - sw).c_str(), x + u(14), mid - u(2), gfx::LABEL, TEXT, bottom_left, track);
    ago(when, sizeof when, l[4] | -1);
    const char* who = l[3] | "";
    snprintf(buf, sizeof buf, "%s%s%s%s%s", (const char*)(l[0] | ""), *who ? " · " : "", who, *when ? " · " : "", when);
    text(fit(buf, gfx::SMALL, w - u(28) - (down ? sw + u(12) : 0)).c_str(), x + u(14), mid + u(4), gfx::SMALL, MUTED, top_left, track);
  }
  if (mcRowAll > fits) {  // barra de rolagem
    const int lh = fits * mcRowH - gap, bh = max(u(16), lh * fits / mcRowAll);
    fb().fillSmoothRoundRect(W() - u(5), mcRowY + (lh - bh) * mcRowTop / (mcRowAll - fits), u(3), bh, u(2), TRACK);
  }
  if (!mcRowN) text("Nenhuma issue aberta", W() / 2, gfx::height() / 2, gfx::LABEL, DIM, middle_center);
  else text("Toque numa issue para abrir no computador", W() / 2, gfx::height() - u(14), gfx::SMALL, DIM, middle_center);
}

void mcIssue(bool live, JsonVariantConst mc, const char* issue) {
  JsonArrayConst a;
  for (JsonArrayConst r : mc["a"].as<JsonArrayConst>())
    if (!strcmp(r[2] | "", issue)) a = r;
  char buf[64], when[16];
  snprintf(buf, sizeof buf, "‹ %s", issue);
  header(live, buf, false, false);
  const int x = M(), maxW = W() - 2 * M(), kind = a[4] | 0;
  int y = SET_Y() + u(8);
  String l2, l1 = wrap2(a[3] | "", gfx::TITLE, maxW, l2);
  text(fit(l1.c_str(), gfx::TITLE, maxW).c_str(), x, y, gfx::TITLE, TEXT);
  y += gfx::lineHeight(gfx::TITLE);
  if (l2.length()) text(fit(l2.c_str(), gfx::TITLE, maxW).c_str(), x, y, gfx::TITLE, TEXT), y += gfx::lineHeight(gfx::TITLE);
  const char* proj = a[7] | "";
  const char* prio = a[8] | "";
  if (*prio) snprintf(buf, sizeof buf, *proj ? "%s · prioridade %s" : "%sprioridade %s", proj, prio);
  else strlcpy(buf, proj, sizeof buf);
  if (*buf) text(fit(buf, gfx::SMALL, maxW).c_str(), x, y + u(4), gfx::SMALL, MUTED), y += gfx::lineHeight(gfx::SMALL) + u(4);
  static const char* PILLS[] = {"Rodando", "Na fila", "Em revisão", "Falhou"};
  const uint16_t pc = mcColor(kind), pbg = blend(BG, pc, 0.22f);
  const char* pill = PILLS[constrain(kind, 0, 3)];
  const int ph = u(24), pw = gfx::textWidth(pill, gfx::SMALL) + 2 * u(12);
  y += u(12);
  fb().fillSmoothRoundRect(x, y, pw, ph, ph / 2, pbg);
  text(pill, x + pw / 2, y + ph / 2, gfx::SMALL, pc, middle_center, pbg);
  y += ph + u(18);
  ago(when, sizeof when, a[5] | -1);
  const char* labels[4];
  char v[4][56];
  int n = 0;
  labels[n] = kind == 2 ? "Com" : "Agente";
  snprintf(v[n++], sizeof v[0], *(a[1] | "") ? "%s · %s" : "%s", (const char*)(a[0] | ""), (const char*)(a[1] | ""));
  if (kind == 2) {
    labels[n] = "Em revisão", snprintf(v[n++], sizeof v[0], "%s", when);
  } else {
    static const char* VERB[] = {"rodando", "na fila", "", "falhou"};
    labels[n] = "Run", snprintf(v[n++], sizeof v[0], "Tentativa %d · %s %s", (int)(a[9] | 1), VERB[kind], when);
  }
  if (kind == 0) {
    labels[n] = "Tokens da run";
    snprintf(v[n++], sizeof v[0], *(a[11] | "") ? "%s · %s" : "%s", (const char*)(a[6] | "--"), (const char*)(a[11] | ""));
    const int prs = a[10] | 0;
    labels[n] = "PRs ligados";
    if (prs) snprintf(v[n++], sizeof v[0], prs == 1 ? "1 aberto" : "%d abertos", prs);
    else strcpy(v[n++], "Nenhum");
  }
  if (kind == 3 && *(a[12] | "")) labels[n] = "Erro", strlcpy(v[n++], a[12] | "", sizeof v[0]);
  const int step = gfx::lineHeight(gfx::SMALL) + gfx::lineHeight(gfx::LABEL) + u(10);
  for (int i = 0; i < n && y + step < gfx::height() - u(30); i++, y += step) {
    text(labels[i], x, y, gfx::SMALL, DIM);
    text(fit(v[i], gfx::LABEL, maxW).c_str(), x, y + gfx::lineHeight(gfx::SMALL), gfx::LABEL, kind == 3 && i == n - 1 ? RED : TEXT);
  }
  text("Toque para voltar", W() / 2, gfx::height() - u(14), gfx::SMALL, DIM, middle_center);
}
}  // namespace

void drawMulti(bool live, int view, const char* issue, int sel) {
  gfx::clear();
  JsonVariantConst mc = app::frame["mc"];
  if (view == 1) return mcProjects(live, mc);
  if (view == 2) return mcIssue(live, mc, issue);
  if (view == 3) return mcProjectIssues(live, mc, sel);
  char title[48];
  snprintf(title, sizeof title, "Multica · %s", (const char*)(mc["w"] | ""));
  const bool wsCut = header(live, title, false, SUB_WARN);
  int y = SET_Y();
  if (wsCut) text(mc["w"] | "", M(), u(10) + below(gfx::LABEL, 22), gfx::SMALL, MUTED);  // saiu do título: vai na linha de baixo
  // os números são filtros da lista de baixo: o escolhido fica com a borda na cor dele
  const int gap = u(8), tw = (W() - 2 * M() - 3 * gap) / 4;
  const int th = gfx::lineHeight(gfx::NUM) + gfx::lineHeight(gfx::SMALL) + u(16);
  mcTileY = y, mcTileH = th, sel = constrain(sel, 0, 3);
  for (int k = 0; k < 4; k++) {
    const int x = M() + k * (tw + gap), count = mc["c"][k] | 0;
    const bool on = k == sel;
    const uint16_t c = mcColor(k), bg = on ? blend(BG, c, 0.16f) : blend(BG, TRACK, 0.6f);
    if (on) fb().fillSmoothRoundRect(x, y, tw, th, u(10), c);
    fb().fillSmoothRoundRect(x + (on ? 2 : 0), y + (on ? 2 : 0), tw - (on ? 4 : 0), th - (on ? 4 : 0), u(10), bg);
    char n[8];
    snprintf(n, sizeof n, "%d", count);
    text(n, x + tw / 2, y + u(8), gfx::NUM, count ? c : DIM, top_center, bg);
    text(MC_TILES[k], x + tw / 2, y + th - u(8), gfx::SMALL, on ? TEXT : MUTED, bottom_center, bg);
  }
  y += th + u(12);
  mcProjY = gfx::height() - u(30) - PROJ_H();
  JsonArrayConst cards = mc["a"];
  mcFiltered = 0;
  int ids[16];
  for (int i = 0; i < (int)cards.size() && mcFiltered < 16; i++)
    if ((int)(cards[i][4] | -1) == sel) ids[mcFiltered++] = i;
  int ah = max(max(u(66), 2 * gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(16)), TAPPX());
  const int agap = u(8), room = mcProjY - u(8) - y;
  if (room < ah) ah = max(room, gfx::lineHeight(gfx::LABEL) + gfx::lineHeight(gfx::SMALL) + u(14));  // um cartão baixo
  const int fits = max(1, (room + agap) / (ah + agap));
  // estica para ocupar a sobra até Projetos, até 1,5x: poucos cartões não viram blocos enormes
  ah = constrain((room + agap) / constrain(mcFiltered, 1, fits) - agap, ah, ah * 3 / 2);
  mcAgentY = y, mcAgentH = ah + agap;
  mcAgentTop = constrain(mcAgentTop, 0, max(0, mcFiltered - fits));
  mcAgentN = min(mcFiltered - mcAgentTop, fits);
  if (!mcFiltered) text(MC_EMPTY[sel], W() / 2, (y + mcProjY) / 2, gfx::LABEL, DIM, middle_center);
  for (int k = 0; k < mcAgentN; k++) {
    mcShown[k] = ids[mcAgentTop + k];
    mcCard(cards[mcShown[k]], k, y + k * (ah + agap), ah);
  }
  if (mcFiltered > fits && fits) {  // barra de rolagem: há cartões acima ou abaixo
    const int lh = fits * (ah + agap) - agap, bh = max(u(16), lh * fits / mcFiltered);
    fb().fillSmoothRoundRect(W() - u(5), y + (lh - bh) * mcAgentTop / (mcFiltered - fits), u(3), bh, u(2), TRACK);
  }
  const uint16_t track = blend(BG, TRACK, 0.6f);
  const int ph = PROJ_H(), w = W() - 2 * M();
  fb().fillSmoothRoundRect(M(), mcProjY, w, ph, u(10), track);
  char buf[32];
  snprintf(buf, sizeof buf, "Projetos · %d", (int)mc["p"].size());
  text(buf, M() + u(14), mcProjY + ph / 2, fontFor(ph) == gfx::SMALL ? gfx::LABEL : fontFor(ph), TEXT, middle_left, track);
  text("›", M() + w - u(12), mcProjY + ph / 2, gfx::TITLE, MUTED, middle_right, track);
  shownWs = -1;
  dots(1);
}

int multiTap(int x, int y) {
  if (x < M() || x >= W() - M()) return -1;
  if (y >= mcTileY && y < mcTileY + mcTileH) return -10 - min(3, (x - M()) * 4 / (W() - 2 * M()));
  if (y >= mcProjY && y < mcProjY + PROJ_H()) return -2;
  const int k = (y - mcAgentY) / max(1, mcAgentH);
  return y >= mcAgentY && k < mcAgentN ? mcShown[k] : -1;
}

bool multiScroll(int dir, int view) {
  if (view == 1) {
    const int to = constrain(mcProjFirst + dir, 0, max(0, mcProjAll - mcProjN));
    if (to == mcProjFirst) return false;
    mcProjFirst = to;
    return true;
  }
  if (view == 3) {
    const int to = constrain(mcRowTop + dir, 0, max(0, mcRowAll - mcRowN));
    if (to == mcRowTop) return false;
    mcRowTop = to;
    return true;
  }
  const int to = constrain(mcAgentTop + dir, 0, max(0, mcFiltered - mcAgentN));
  if (to == mcAgentTop) return false;
  mcAgentTop = to;
  return true;
}
void multiFilter() { mcAgentTop = 0, mcRowTop = 0, mcProjFirst = 0; }

int mcProjTap(int x, int y) {
  if (y < SET_Y() - u(4)) return -2;
  const int k = (y - mcProjTop) / max(1, mcProjH);
  return y >= mcProjTop && k < mcProjN && x >= M() && x < W() - M() ? mcProjFirst + k : -1;
}

int mcListTap(int x, int y) {
  if (y < SET_Y() - u(4)) return -2;
  const int k = (y - mcRowY) / max(1, mcRowH);
  if (y < mcRowY || k >= mcRowN || x < M() || x >= W() - M()) return -1;
  mcTapped = mcRowIdx[k], mcTappedMs = millis();
  return mcTapped;
}

// Uma pergunta do primeiro uso: o título, a explicação e as opções em cartões altos (alvo de 9 mm ou mais).
int choiceY(int k) { return u(110) + k * (max(u(70), TAP()) + u(12)); }
void drawChoice(const char* title, const char* sub, const char* const* names, const char* const* hints, int n) {
  gfx::clear();
  const int cx = W() / 2, bw = W() - 2 * M(), bh = max(u(70), TAP());
  text(title, cx, u(40), gfx::TITLE, TEXT, middle_center);
  text(fit(sub, gfx::LABEL, bw).c_str(), cx, u(40) + below(gfx::TITLE, 32), gfx::LABEL, MUTED, middle_center);
  for (int k = 0; k < n; k++) {
    const uint16_t bg = blend(BG, TRACK, 0.6f);
    fb().fillSmoothRoundRect(M(), choiceY(k), bw, bh, u(14), bg);
    const bool two = hints[k] && *hints[k];
    const int mid = choiceY(k) + bh / 2;
    text(names[k], M() + u(18), two ? mid - gfx::lineHeight(gfx::LABEL) / 2 - u(2) : mid, gfx::TITLE, TEXT, middle_left, bg);
    if (two) text(fit(hints[k], gfx::LABEL, bw - u(36)).c_str(), M() + u(18), mid + gfx::lineHeight(gfx::TITLE) / 2 + u(2), gfx::LABEL, DIM, middle_left, bg);
  }
  text("Dá para mudar depois em Ajustes", cx, gfx::height() - u(20), gfx::SMALL, DIM, middle_center);
}
int choiceTap(int x, int y, int n) {
  const int bh = max(u(70), TAP());
  for (int k = 0; k < n; k++)
    if (y >= choiceY(k) && y < choiceY(k) + bh && x >= M() && x < W() - M()) return k + 1;
  return 0;
}
void drawMcAsk() {
  static const char* const N[] = {"Mostrar", "Agora não"};
  static const char* const H[] = {"Seus agentes, issues e projetos", "Liga depois em Ajustes → Multica"};
  drawChoice("Mostrar o Multica?", "Achei o Multica neste computador", N, H, 2);
}
int mcAskTap(int x, int y) { return choiceTap(x, y, 2); }

void drawLoginAsk() {
  gfx::clear();
  const int cx = W() / 2, bw = W() - 2 * M(), bh = max(u(56), TAP());
  text("Falta o login do Claude", cx, u(40), gfx::TITLE, TEXT, middle_center);
  text(fit("Sem ele, a placa não mostra o % de uso", gfx::LABEL, bw).c_str(), cx, u(40) + below(gfx::TITLE, 32), gfx::LABEL, MUTED, middle_center);
  // a caixa cresce com o texto: o comando e a frase em até duas linhas, sem cortar
  const int by = u(110), tx = M() + u(16), tw = bw - u(32);
  String l2, l1 = wrap2("e entre na sua conta. A placa segue sozinha.", gfx::SMALL, tw, l2);
  const int cmdY = by + u(14) + below(gfx::LABEL, 30), noteY = cmdY + gfx::lineHeight(gfx::TITLE) + u(6);
  const int boxH = noteY - by + gfx::lineHeight(gfx::SMALL) * (l2.length() ? 2 : 1) + u(14);
  const uint16_t bg = blend(BG, TRACK, 0.6f);
  fb().fillSmoothRoundRect(M(), by, bw, boxH, u(14), bg);
  text("No terminal, rode:", tx, by + u(14), gfx::LABEL, TEXT, top_left, bg);
  text("claude", tx, cmdY, gfx::TITLE, GREEN, top_left, bg);
  text(l1.c_str(), tx, noteY, gfx::SMALL, DIM, top_left, bg);
  if (l2.length()) text(fit(l2.c_str(), gfx::SMALL, tw).c_str(), tx, noteY + gfx::lineHeight(gfx::SMALL), gfx::SMALL, DIM, top_left, bg);
  const int sy = gfx::height() - u(24) - bh;
  fb().fillSmoothRoundRect(M(), sy, bw, bh, min(bh / 2, u(26)), bg);
  text("Pular: só tokens, sem %", cx, sy + bh / 2, gfx::LABEL, TEXT, middle_center, bg);
}
bool loginSkipAt(int x, int y) {
  const int bh = max(u(56), TAP()), sy = gfx::height() - u(24) - bh;
  return y >= sy && y < sy + bh && x >= M() && x < W() - M();
}

// Firmware novo a caminho: o daemon grava em poucos segundos. O toque adia uma hora.
void drawFwUp() {
  JsonArrayConst f = app::frame["fwup"];
  // a placa conta sozinha a partir do primeiro aviso: com os segundos de cada frame, a tela lenta pulava números
  static String seen;
  static uint32_t fromMs = 0;
  static int fromSecs = 0;
  if (seen != (const char*)(f[0] | "")) seen = f[0] | "", fromMs = millis(), fromSecs = f[1] | 0;
  const int left = max(0, fromSecs - (int)((millis() - fromMs) / 1000));
  gfx::clear();
  const int cx = W() / 2, h = gfx::height();
  char buf[48];
  text("Atualizando o firmware", cx, h * 30 / 100, gfx::TITLE, TEXT, middle_center);
  snprintf(buf, sizeof buf, "%s → %s", hostlink::firmware(), (const char*)(f[0] | ""));
  text(fit(buf, gfx::LABEL, W() - 2 * M()).c_str(), cx, h * 30 / 100 + below(gfx::TITLE, 34), gfx::LABEL, MUTED, middle_center);
  snprintf(buf, sizeof buf, left ? "Começa em %d s" : "Começando…", left);
  text(buf, cx, h / 2 + u(10), gfx::NUM, ACCENT, middle_center);
  text("Não desligue a placa", cx, h * 72 / 100, gfx::LABEL, TEXT, middle_center);
  text("Toque para adiar 1 h", cx, h * 72 / 100 + below(gfx::LABEL, 28), gfx::SMALL, DIM, middle_center);
}

// Versão nova no GitHub: atualiza o programa do Mac e, em seguida, o firmware das placas.
int updBtnY(int k) { return gfx::height() - u(30) - (2 - k) * (max(u(52), TAP()) + u(12)); }
void drawUpdate(int secsLeft) {
  gfx::clear();
  const int cx = W() / 2, bh = max(u(52), TAP()), bw = W() - 2 * M();
  char buf[48];
  text("Versão nova", cx, u(40), gfx::TITLE, TEXT, middle_center);
  // quem atualiza é o programa do computador: a versão de partida é a dele, não a do firmware desta placa
  snprintf(buf, sizeof buf, "%s → %s", (const char*)(app::frame["agv"] | hostlink::firmware()), (const char*)(app::frame["upd"] | ""));
  text(fit(buf, gfx::LABEL, bw).c_str(), cx, u(40) + below(gfx::TITLE, 34), gfx::LABEL, ACCENT, middle_center);
  String l2, l1 = wrap2("Atualiza o computador e as placas, em cerca de 1 min.", gfx::LABEL, bw, l2);
  const int ty = u(40) + below(gfx::TITLE, 34) + below(gfx::LABEL, 40);
  text(l1.c_str(), cx, ty, gfx::LABEL, MUTED, middle_center);
  if (l2.length()) text(fit(l2.c_str(), gfx::LABEL, bw).c_str(), cx, ty + below(gfx::LABEL, 24), gfx::LABEL, MUTED, middle_center);
  char later[24];
  snprintf(later, sizeof later, "Depois (%d s)", secsLeft);
  const char* labels[] = {"Atualizar agora", later};
  for (int k = 0; k < 2; k++) {
    const uint16_t c = k ? blend(BG, TRACK, 0.7f) : ACCENT;
    fb().fillSmoothRoundRect(M(), updBtnY(k), bw, bh, min(bh / 2, u(26)), c);
    text(labels[k], cx, updBtnY(k) + bh / 2, fontFor(bh) == gfx::SMALL ? gfx::LABEL : fontFor(bh), k ? TEXT : BG, middle_center, c);
  }
}
// O código que libera a página web no navegador: grande, com o tempo que falta. Some sozinho depois de usado.
void drawPairCode(int code, int secs) {
  gfx::clear();
  const int cx = W() / 2, cy = gfx::height() / 2;
  char buf[24];
  text("Código da página web", cx, cy - u(70), gfx::TITLE, TEXT, middle_center);
  snprintf(buf, sizeof buf, "%d %d %d %d", code / 1000, code / 100 % 10, code / 10 % 10, code % 10);
  text(buf, cx, cy, gfx::NUM, ACCENT, middle_center);
  title2("Digite no navegador para mudar os ajustes", cx, cy + u(46), W() - 2 * M(), MUTED, gfx::LABEL);
  snprintf(buf, sizeof buf, "Some em %d s", secs);
  text(buf, cx, gfx::height() - u(20), gfx::SMALL, DIM, middle_center);
}

// Primeiro uso numa placa sem acelerômetro: qual lado fica embaixo. A placa não sabe para que lado a pessoa olha,
// então cada borda tem uma faixa escrita para ser lida quando aquela borda está embaixo: só uma aparece em pé, e é
// nela que se toca. Com duas posições (em pé ou deitada), dois desenhos da placa no lugar das faixas.
int ORIENT_BAND() { return max(u(44), TAPPX()); }

void drawOrient(bool confirm, int secsLeft) {
  gfx::clear();
  const int w = W(), h = gfx::height(), b = ORIENT_BAND(), cx = w / 2, cy = h / 2;
  if (confirm) {
    text("Ficou certo?", cx, cy - u(56), gfx::TITLE, TEXT, middle_center);
    const int bh = max(u(44), TAPPX()), bw = min(w - 2 * M(), u(260));
    fb().fillSmoothRoundRect(cx - bw / 2, cy - u(24), bw, bh, bh / 2, ACCENT);
    text("Sim, está certo", cx, cy - u(24) + bh / 2, gfx::LABEL, BG, middle_center, ACCENT);
    const uint16_t sec = blend(BG, TRACK, 0.7f);
    fb().fillSmoothRoundRect(cx - bw / 2, cy - u(24) + bh + u(10), bw, bh, bh / 2, sec);
    text("Escolher de novo", cx, cy - u(14) + bh * 3 / 2, gfx::LABEL, TEXT, middle_center, sec);
    char buf[40];
    snprintf(buf, sizeof buf, "Fica assim em %d s", max(secsLeft, 0));
    text(buf, cx, h - u(20), gfx::SMALL, DIM, middle_center);
    return;
  }
  const uint16_t band = blend(BG, ACCENT, 0.18f);
  if (board::ROTATIONS == 4) {
    // as faixas das bordas curtas vão de ponta a ponta; as outras cabem entre elas, com vão: os cantos não se tocam
    const int e = u(6), g = max(u(12), 8), wide = w > h;
    const int hx = wide ? e + b + g : e, hw = w - 2 * hx, vy = wide ? e : e + b + g, vh = h - 2 * vy;
    fb().fillSmoothRoundRect(hx, h - b - e, hw, b, u(10), band);  // embaixo
    fb().fillSmoothRoundRect(hx, e, hw, b, u(10), band);          // em cima
    fb().fillSmoothRoundRect(e, vy, b, vh, u(10), band);          // esquerda
    fb().fillSmoothRoundRect(w - b - e, vy, b, vh, u(10), band);  // direita
    const char* t = "Este lado fica embaixo";
    gfx::textTurned(t, cx, h - u(6) - b / 2, 0, gfx::LABEL, ACCENT, band);
    gfx::textTurned(t, cx, u(6) + b / 2, 2, gfx::LABEL, ACCENT, band);
    gfx::textTurned(t, u(6) + b / 2, cy, 1, gfx::SMALL, ACCENT, band);
    gfx::textTurned(t, w - u(6) - b / 2, cy, 3, gfx::SMALL, ACCENT, band);
    // a instrução no meio, no sentido da faixa de baixo (a posição de fábrica, a mais provável)
    const int lh = gfx::lineHeight(gfx::SMALL);
    text("Toque na faixa que ficou", cx, cy - lh / 2 - u(2), gfx::SMALL, DIM, middle_center);
    text("embaixo, virada para você", cx, cy + lh / 2 + u(2), gfx::SMALL, DIM, middle_center);
    return;
  }
  text("Como ela vai ficar?", cx, u(40), gfx::TITLE, TEXT, middle_center);
  const int pw = min((w - 3 * M()) / 2, u(130)), ph = pw * 4 / 3, top = cy - ph / 2;
  for (int k = 0; k < 2; k++) {
    const int x = cx + (k ? M() / 2 : -M() / 2 - pw), on = settings::v.orient == k;
    fb().fillSmoothRoundRect(x, top, pw, ph, u(14), on ? blend(BG, ACCENT, 0.18f) : blend(BG, TRACK, 0.7f));
    const int fw = k ? pw * 2 / 3 : pw / 2, fh = k ? pw / 2 : pw * 2 / 3;  // a placa em pé ou deitada
    fb().fillSmoothRoundRect(x + (pw - fw) / 2, top + u(16), fw, fh, u(4), on ? ACCENT : MUTED);
    text(k ? "Deitada" : "Em pé", x + pw / 2, top + ph - u(22), gfx::LABEL, TEXT, middle_center);
  }
  text("Toque no desenho que parece com a sua mesa", cx, h - u(24), gfx::SMALL, MUTED, middle_center);
}

// Na escolha: a borda tocada (0 embaixo, 1 direita, 2 em cima, 3 esquerda) ou a posição (0 em pé, 1 deitada);
// na confirmação: 1 sim, 2 escolher de novo. -1: nada.
int orientTap(int x, int y, bool confirm) {
  const int w = W(), h = gfx::height(), b = ORIENT_BAND() + u(6), cx = w / 2, cy = gfx::height() / 2;
  if (confirm) {
    const int bh = max(u(44), TAPPX());
    if (y >= cy - u(24) && y < cy - u(24) + bh) return 1;
    if (y >= cy - u(14) + bh && y < cy - u(14) + 2 * bh) return 2;
    return -1;
  }
  if (board::ROTATIONS == 4) {
    if (w > h && x < b) return 3;  // deitada, as laterais vão de ponta a ponta: valem nos cantos
    if (w > h && x >= w - b) return 1;
    if (y >= h - b) return 0;
    if (y < b) return 2;
    if (x < b) return 3;
    if (x >= w - b) return 1;
    return -1;
  }
  const int pw = min((w - 3 * M()) / 2, u(130)), ph = pw * 4 / 3, top = cy - ph / 2;
  if (y < top || y >= top + ph) return -1;
  return x < cx ? 0 : 1;
}

// Depois do "Atualizar agora": o que já foi e o que falta, até a contagem do firmware assumir a tela. O primeiro
// passo fica pronto quando o daemon que fala com a placa já é a versão nova (agv no frame).
void drawUpdating(const char* target) {
  gfx::clear();
  const int cx = W() / 2, bw = W() - 2 * M();
  text("Atualizando", cx, u(40), gfx::TITLE, TEXT, middle_center);
  title2("Não desligue a placa nem o computador", cx, u(40) + below(gfx::TITLE, 26), bw, MUTED, gfx::LABEL);
  const bool agentDone = !strcmp(app::frame["agv"] | "", target);
  const char* steps[] = {"Programa no computador", "Firmware desta placa"};
  int y = gfx::height() / 2 - u(10);
  for (int k = 0; k < 2; k++, y += max(u(56), TAPPX())) {
    const int st = k == 0 ? (agentDone ? 0 : 1) : (agentDone ? 1 : 2);
    stepIcon(M() + u(24), y, st);
    text(steps[k], M() + u(48), y, gfx::LABEL, st == 2 ? DIM : TEXT, middle_left);
  }
}

int updateTap(int x, int y) {
  const int bh = max(u(52), TAP());
  for (int k = 0; k < 2; k++)
    if (y >= updBtnY(k) && y < updBtnY(k) + bh && x >= M() && x < W() - M()) return k + 1;
  return 0;
}

// O aviso do Multica em tela cheia, como o de uma sessão: issue que foi para revisão (amarelo) ou run que falhou
// (vermelho). O botão abre a issue no navegador do Mac; tocar fora fecha.
void drawMcAlert(uint32_t ageMs) {
  JsonVariantConst a = app::frame["mca"];
  const bool fail = !strcmp(a["k"] | "", "fail");
  const uint16_t c = fail ? RED : YELLOW, bg = blend(BG, c, 0.12f);
  gfx::clear(bg);
  const int cx = W() / 2, h = gfx::height(), maxW = W() - 2 * M();
  text(fail ? "Run falhou" : "Pede sua revisão", cx, u(28), gfx::TITLE, c, middle_center, bg);
  const int px = max(3, u(7)), my = u(52);
  mascot::draw(cx - mascot::W * px / 2, my, px, fail ? mascot::BLOCK : mascot::TURN, bg);
  int y = my + 8 * px + u(16);
  String l2, l1 = wrap2(a["t"] | "", gfx::NUM, maxW, l2);
  text(fit(l1.c_str(), gfx::NUM, maxW).c_str(), cx, y, gfx::NUM, TEXT, top_center, bg);
  y += gfx::lineHeight(gfx::NUM);
  if (l2.length()) text(fit(l2.c_str(), gfx::NUM, maxW).c_str(), cx, y, gfx::NUM, TEXT, top_center, bg), y += gfx::lineHeight(gfx::NUM);
  y += u(4);
  text(fit(a["m"] | "", gfx::SMALL, maxW).c_str(), cx, y, gfx::SMALL, MUTED, top_center, bg);
  y += gfx::lineHeight(gfx::SMALL) + u(12);
  if (*(a["x"] | "")) text(fit(a["x"] | "", gfx::LABEL, maxW).c_str(), cx, y, gfx::LABEL, TEXT, top_center, bg);
  const int by = ATTN_BTN_Y(), bh = ATTN_BTN_H(), bw2 = maxW;
  fb().fillSmoothRoundRect(cx - bw2 / 2, by, bw2, bh, min(bh / 2, u(26)), c);
  text("Abrir no Multica", cx, by + bh / 2, fontFor(bh) == gfx::SMALL ? gfx::LABEL : fontFor(bh), BG, middle_center, c);
  const int left = maxW * (60000 - min<uint32_t>(ageMs, 60000)) / 60000;
  fb().fillRoundRect(M(), h - u(22), maxW, u(4), u(2), blend(bg, c, 0.25f));
  if (left > 0) fb().fillRoundRect(M(), h - u(22), left, u(4), u(2), c);
}

}  // namespace screens::tall
