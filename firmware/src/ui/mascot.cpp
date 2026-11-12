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
constexpr uint16_t CODEX_BODY = 0x150F;  // #10A37F
// e = o ">" do prompt, c = o cursor "_", que pisca enquanto a sessão trabalha
const char* const CODEX_ROWS[] = {"..oooooooo..", ".oooooooooo.", "ooeooooooooo", "oooeoooooooo",
                                  "ooeoocccccoo", "oooooooooooo", ".oooooooooo."};
}  // namespace

void draw(int x, int y, int px, Mood mood, uint16_t bg, uint32_t seed, bool codex) {
  auto& fb = gfx::fb();
  const uint32_t t = millis() + seed * 377;
  int dx = 0, dy = 0, legs = 0;
  if (mood == BLOCK && t % 1500 < 360) dx = (t / 60 % 2) ? px / 2 : -px / 2;  // tremidinha
  if (mood == WORK) legs = t / 160 % 2, dy = (t / 160 % 2) ? -px / 2 : 0;  // anda no lugar
  if (mood == TURN && t % 1600 < 220) dy = -px;                           // pulinho
  if (mood == OFFLINE) dy = (t / 700 % 2) ? -px / 2 : 0;  // respira devagar
  const bool blink = mood == TURN && t % 2900 < 120;
  const uint16_t body = mood == IDLE ? blend(codex ? CODEX_BODY : BODY, bg, 0.55f) : codex ? CODEX_BODY : BODY;
  for (int r = 0; r < 8; r++) {
    const char* row = r < 7 ? (codex ? CODEX_ROWS : BODY_ROWS)[r] : LEGS[legs];
    for (int c = 0; c < W; c++) {
      char p = row[c];
      if (p == '.') continue;
      uint16_t col = body;
      if (codex) {
        if (p == 'c') col = mood == IDLE || (mood == WORK && t / 400 % 2) ? body : EYE;
        else if (p == 'e') col = EYE;
      } else if (p == 'e' && mood == OFFLINE) {  // olha para o cabo, à direita: só a metade direita do olho
        col = c == 3 || c == 9 ? EYE : body;
      } else if (p == 'e') {
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
  if (mood == OFFLINE) {
    // o cabo sai do lado do corpo; o plugue vai até a tomada e volta, sem nunca encaixar
    constexpr uint16_t CABLE = 0x7BEF, PLUG = 0xBDF7, SOCKET = 0x632C;
    const float a = (t % 2400) / 2400.f, reach = a < 0.5f ? a * 2 : 2 - a * 2;  // 0 → 1 → 0
    const int gap = lroundf(3 * (1 - reach * reach)), cy = y + dy + 5 * px;
    int cx = x + dx + W * px;
    fb.fillRect(cx, cy, (3 - gap + 3) * px, px, CABLE), cx += (6 - gap) * px;  // o fio estica
    fb.fillRect(cx, cy - px, 2 * px, 3 * px, PLUG), cx += 2 * px;               // o plugue
    fb.fillRect(cx, cy - px, px, px / 2 + 1, PLUG);                               // os dois pinos
    fb.fillRect(cx, cy + px + px / 2, px, px / 2 + 1, PLUG);
    const int sx = x + W * px + 11 * px;  // a tomada, parada
    fb.fillRect(sx, cy - 2 * px, 2 * px, 5 * px, SOCKET);
    if (reach > 0.92f && t / 90 % 2) {  // faísca quando quase encosta
      const int fx = sx - px, fy = cy;
      fb.fillRect(fx, fy - px, px / 2 + 1, px * 3, color::YELLOW);
      fb.fillRect(fx - px, fy + px / 2 - px / 4, px * 3 - px / 2, px / 2 + 1, color::YELLOW);
    }
  }
  if (mood == TURN) gfx::text("!", x + W * px + px / 2, y - px, gfx::TITLE, color::YELLOW, top_left, bg);
  if (mood == BLOCK) gfx::text("?", x + W * px + px / 2, y - px, gfx::TITLE, color::RED, top_left, bg);
}

}  // namespace mascot
