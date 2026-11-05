#include "mascot.h"

#include "gfx.h"

namespace mascot {
namespace {
constexpr uint16_t BODY = 0xDBAA;  // #D97757, o laranja do Claude
constexpr uint16_t EYE = 0x18E3;

// o = corpo, e = olho, - = olho fechado, . = vazio. Linhas 0-6 o corpo; a 7 são as pernas.
const char* const BODY_ROWS[] = {"..oooooooo..", ".oooooooooo.", "oooooooooooo", "ooeeooooeeoo",
                                 "ooeeooooeeoo", "oooooooooooo", ".oooooooooo."};
const char* const LEGS[] = {".o.o....o.o.", "..o.o..o.o.."};
}  // namespace

void draw(int x, int y, int px, Mood mood, uint16_t bg, uint32_t seed) {
  auto& fb = gfx::fb();
  const uint32_t t = millis() + seed * 377;
  int dx = 0, dy = 0, legs = 0;
  if (mood == BLOCK && t % 1500 < 360) dx = (t / 60 % 2) ? px / 2 : -px / 2;  // tremidinha
  if (mood == WORK) legs = t / 160 % 2, dy = (t / 160 % 2) ? -px / 2 : 0;  // anda no lugar
  if (mood == TURN && t % 1600 < 220) dy = -px;                           // pulinho
  const bool blink = mood == TURN && t % 2900 < 120;
  const uint16_t body = mood == IDLE ? blend(BODY, bg, 0.55f) : BODY;
  for (int r = 0; r < 8; r++) {
    const char* row = r < 7 ? BODY_ROWS[r] : LEGS[legs];
    for (int c = 0; c < W; c++) {
      char p = row[c];
      if (p == '.') continue;
      uint16_t col = body;
      if (p == 'e') {
        const bool closed = mood == IDLE || blink;
        if (closed) col = r == 4 ? EYE : body;           // olho fechado: só a linha de baixo
        else if (mood == TURN) col = r == 3 ? EYE : body;  // olhando para cima
        else col = EYE;
      }
      fb.fillRect(x + dx + c * px, y + dy + r * px, px, px, col);
    }
  }
  if (mood == IDLE) {  // zzz subindo e sumindo
    const float a = (t % 1800) / 1800.f, fade = floorf(a * 6) / 6;  // 6 tons: o texto pronto fica no cache
    gfx::text("z", x + W * px - px, y - (int)(a * 3 * px), gfx::SMALL, blend(color::DIM, bg, fade), top_left, bg);
  }
  if (mood == TURN) gfx::text("!", x + W * px + px / 2, y - px, gfx::TITLE, color::ACCENT, top_left, bg);
  if (mood == BLOCK) gfx::text("?", x + W * px + px / 2, y - px, gfx::TITLE, color::RED, top_left, bg);
}

}  // namespace mascot
