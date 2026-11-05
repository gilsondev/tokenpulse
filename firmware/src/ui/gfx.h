#pragma once
// Canvas e primitivas de desenho. As telas desenham no canvas e chamam present(), que entrega o
// quadro para board::displayPush. O LovyanGFX aqui só desenha em memória: a tela pode ter outro driver.
#include <LovyanGFX.hpp>

#include "../boards/board.h"
#include "theme.h"

namespace gfx {

void begin();
void setRotation(uint8_t rot);
uint8_t rotation();
bool ready();  // false se não coube o canvas na memória; nada é desenhado
LGFX_Sprite& fb();
inline int width() { return fb().width(); }
inline int height() { return fb().height(); }
inline bool wide() { return width() > height(); }
// Escala da UI em relação à tela de referência de 172x320: 1 nela, 1,5 em 320x480.
// Posições e tamanhos fixos das telas passam por s(); as fontes trocam de tamanho junto.
float scale();
inline int s(int v) { return lroundf(v * scale()); }
bool large();  // tela grande: fontes suaves (Inter) e formas com anti-aliasing
void present();
// Tira do fim da String um caractere UTF-8 inteiro (não só o último byte de um "ã").
inline void dropLastChar(String& t) {
  while (t.length() && ((uint8_t)t[t.length() - 1] & 0xC0) == 0x80) t.remove(t.length() - 1);
  if (t.length()) t.remove(t.length() - 1);
}
const uint16_t* shown();  // o último quadro entregue à tela (RGB565, byte alto primeiro)
void clear(uint16_t c = color::BG);  // mais rápido que fillScreen na PSRAM
// A próxima troca de tela desliza: -1 o conteúdo sai pela esquerda, +1 pela direita, UP sobe e revela a nova.
constexpr int UP = 2;
void slide(int dir);

// Papéis de texto. Em tela grande são Inter com anti-aliasing; nas pequenas, as fontes bitmap.
enum Font : uint8_t { SMALL, LABEL, TITLE, NUM, HERO };
// bg: a cor sob o texto (o texto pronto fica guardado já misturado com ela)
void text(const char* s, int x, int y, Font f, uint16_t c, uint8_t datum = top_left, uint16_t bg = color::BG);
int textWidth(const char* s, Font f);
inline void label(const char* s, int x, int y, uint16_t c, uint8_t datum = top_left) { text(s, x, y, LABEL, c, datum); }
inline void big(const char* s, int x, int y, uint16_t c = color::TEXT, uint8_t datum = top_left) { text(s, x, y, NUM, c, datum); }

// Guarda um retângulo já desenhado do canvas e o devolve enquanto a chave não mudar.
// restore() devolve true se copiou; se não, a tela desenha a região e chama store().
class RegionCache {
 public:
  bool restore(uint32_t key, int x, int y, int w, int h);
  void store(uint32_t key);

 private:
  LGFX_Sprite spr;
  uint32_t key = 0;
  int x = 0, y = 0, w = 0, h = 0;
};
uint32_t hashOf(std::initializer_list<uint32_t> vals);

// Barra de progresso. Em tela grande: gradiente até a ponta, halo depois dela (halo 0 a 1),
// listras no trecho além do ritmo `pace` (0 a 1; -1 sem) e um brilho único em `shine` (0 a 1; -1 sem).
// Em tela pequena: barra lisa com um brilho periódico.
void bar(int x, int y, int w, int h, float frac, uint16_t c, float shine = -1, float pace = -1, float halo = 0);
// Anel com anti-aliasing e pontas arredondadas, de 12 h no sentido horário até frac (0 a 1).
// bg é a cor do fundo sob o anel; track = bg desenha só o arco.
void ring(int cx, int cy, int r, int thick, float frac, uint16_t c, uint16_t track, uint16_t bg = color::BG);
// Halo na ponta do anel, sobre o trilho logo depois dela.
void ringHalo(int cx, int cy, int r, int thick, float frac, uint16_t c, float alpha);
// Ícone num anel de progresso, título e uma linha de status; desenha e já mostra.
enum Icon : uint8_t { BOOT, DOWNLOAD, OK, FAIL };
void loading(Icon icon, const char* title, const char* status, float progress, uint16_t c = color::ACCENT);

}  // namespace gfx
