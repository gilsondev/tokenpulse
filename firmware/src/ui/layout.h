#pragma once
// Posições da tela principal. Dependem da resolução, não da placa: uma placa nova com tela
// de 320x172 reaproveita estes layouts. Para outra resolução, acrescente um par em layoutFor();
// sem ele, o layout de mesma orientação é escalado, o que serve de ponto de partida.
#include <LovyanGFX.hpp>

struct Layout {
  int ringX, ringY, ringR, ringLabelY, resetY, sessX, sessY;
  int statusX, statusY;     // ponto + "ao vivo"
  int clockY;               // igual a statusY quando cabe tudo numa linha (paisagem)
  int weekX, weekY, weekW;  // rótulo; a barra fica 16 px abaixo
  int tokX, tokY, rateX;    // rótulos; os números ficam 16 px abaixo
  int subY;
  int spX, spY, spW, spH;   // base da sparkline em spY
  uint8_t sessDatum;
  int w, h;                 // resolução para a qual o layout foi desenhado
};

constexpr Layout LANDSCAPE_320x172 = {56, 76, 42, 126, 140, 56, 158, 214, 6, 6, 120, 32, 192, 120, 64, 236, 102, 120, 156, 192, 36, middle_center, 320, 172};
constexpr Layout PORTRAIT_172x320 = {86, 106, 48, 162, 178, 164, 34, 8, 34, 6, 8, 194, 156, 8, 226, 96, 266, 8, 302, 156, 20, top_right, 172, 320};

inline Layout scaled(const Layout& b, int w, int h) {
  auto sx = [&](int v) { return v * w / b.w; };
  auto sy = [&](int v) { return v * h / b.h; };
  return {sx(b.ringX), sy(b.ringY), min(sx(b.ringR), sy(b.ringR)), sy(b.ringLabelY), sy(b.resetY), sx(b.sessX), sy(b.sessY),
          sx(b.statusX), sy(b.statusY), sy(b.clockY), sx(b.weekX), sy(b.weekY), sx(b.weekW), sx(b.tokX), sy(b.tokY), sx(b.rateX),
          sy(b.subY), sx(b.spX), sy(b.spY), sx(b.spW), sy(b.spH), b.sessDatum, w, h};
}

inline Layout layoutFor(int w, int h) {
  const Layout& base = w > h ? LANDSCAPE_320x172 : PORTRAIT_172x320;
  return w == base.w && h == base.h ? base : scaled(base, w, h);
}
