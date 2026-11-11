#include "flipclock.h"

#include <math.h>

#include "fonts/inter_flip.h"
#include "gfx.h"

namespace flip {
namespace {
constexpr uint32_t FLIP_MS = 520;
// cinzas baixos de propósito: a AMOLED marca menos e o relógio não ofusca no escuro
constexpr uint16_t CARD = 0x2945, DIGIT = 0xBDF7, HINGE = 0x0000;
// inter_flip (72 px): o dígito vai da linha 18 à 74 a partir do topo da linha (medido no mkvlw.py)
constexpr int GLYPH_TOP = 18, GLYPH_BOTTOM = 74, ADVANCE = 44;
constexpr int CW = ADVANCE + 12, CH = GLYPH_BOTTOM - GLYPH_TOP + 36;  // o cartão
constexpr int GAP = 5, COLON = 18;

lgfx::PointerWrapper wrap;
lgfx::VLWfont font;
LGFX_Sprite faces[10];
bool faceOk[10];

struct Slot {
  int8_t cur = -1, from = -1;
  uint32_t t0 = 0;
};
Slot slots[6];

LGFX_Sprite* face(int d) {
  static bool fontOk = (wrap.set(inter_flip), font.loadFont(&wrap));
  LGFX_Sprite& s = faces[d];
  if (!faceOk[d] && fontOk) {
    s.setColorDepth(16);
    s.setPsram(true);
    if (!s.createSprite(CW, CH)) return nullptr;
    s.fillSprite(0);
    s.fillSmoothRoundRect(0, 0, CW, CH, 7, CARD);
    s.setFont(&font);
    s.setTextColor(DIGIT, CARD);
    s.setTextDatum(top_center);
    const char str[2] = {char('0' + d), 0};
    s.drawString(str, CW / 2, CH / 2 - (GLYPH_TOP + GLYPH_BOTTOM) / 2);
    s.fillRect(0, CH / 2 - 1, CW, 2, HINGE);  // o vão entre as duas metades
    faceOk[d] = true;
  }
  return faceOk[d] ? &s : nullptr;
}

// Escurece um pixel do canvas (RGB565 com os bytes trocados) pelo fator k de 0 a 1.
inline uint16_t shade(uint16_t px, float k) {
  const uint16_t v = __builtin_bswap16(px);
  const int r = ((v >> 11) & 31) * k, g = ((v >> 5) & 63) * k, b = (v & 31) * k;
  return __builtin_bswap16(r << 11 | g << 5 | b);
}

// Copia a linha `sr` do cartão para a linha `dy` do canvas, escurecida por k.
void row(LGFX_Sprite* s, int sr, int x, int dy, float k) {
  auto* dst = static_cast<uint16_t*>(gfx::fb().getBuffer()) + dy * gfx::width() + x;
  const auto* src = static_cast<const uint16_t*>(s->getBuffer()) + sr * CW;
  if (k >= 0.999f) return (void)memcpy(dst, src, CW * 2);
  for (int i = 0; i < CW; i++) dst[i] = shade(src[i], k);
}

void card(int i, int x, int y, int d) {
  Slot& s = slots[i];
  if (d != s.cur) s.from = s.cur, s.cur = d, s.t0 = millis();
  LGFX_Sprite* to = face(d);
  if (!to) return;
  constexpr int half = CH / 2;
  const float t = s.from < 0 ? 1 : min(1.f, (millis() - s.t0) / (float)FLIP_MS);
  LGFX_Sprite* from = s.from >= 0 ? face(s.from) : nullptr;
  if (t >= 1 || !from) {
    for (int r = 0; r < CH; r++) row(to, r, x, y + r, 1);
    return;
  }
  const float p = t * t * (3 - 2 * t);  // começa devagar, acelera e assenta
  const float c = cosf(p * PI);         // 1 → -1: a metade de cima cai até deitar e vira a de baixo
  // atrás: em cima já o dígito novo; embaixo ainda o velho, na sombra do cartão que cai
  for (int r = 0; r < half; r++) row(to, r, x, y + r, 1);
  const float under = c > 0 ? 0.55f + 0.45f * (1 - c) : 1;
  for (int r = half; r < CH; r++) row(from, r, x, y + r, under);
  // o cartão que vira, achatado pela perspectiva e mais escuro quanto mais deitado
  const float k = 0.35f + 0.65f * fabsf(c);
  const int flapH = lroundf(half * fabsf(c));
  for (int j = 0; j < flapH; j++) {
    if (c > 0) row(from, j * half / flapH, x, y + half - flapH + j, k);  // a metade de cima do velho, caindo
    else row(to, half + j * half / flapH, x, y + half + j, k);           // a de baixo do novo, assentando
  }
}

void dots(int x, int y) {  // os dois pontos entre os grupos
  gfx::fb().fillCircle(x + COLON / 2, y + CH / 2 - CH / 6, 4, DIGIT);
  gfx::fb().fillCircle(x + COLON / 2, y + CH / 2 + CH / 6, 4, DIGIT);
}
}  // namespace

void size(int& w, int& h, bool seconds) {
  w = seconds ? 6 * CW + 3 * GAP + 2 * COLON : 4 * CW + 2 * GAP + COLON;
  h = CH;
}

void draw(int x, int y, int32_t secs, bool seconds) {
  if (secs < 0) return;
  const int digits[6] = {secs / 36000, secs / 3600 % 10, secs / 600 % 6, secs / 60 % 10, secs % 60 / 10, secs % 10};
  const int n = seconds ? 6 : 4;
  for (int i = 0; i < n; i++) {
    card(i, x, y, digits[i]);
    x += CW + (i % 2 == 0 ? GAP : 0);
    if (i == 1 || (i == 3 && seconds)) dots(x, y), x += COLON;
  }
}

}  // namespace flip
