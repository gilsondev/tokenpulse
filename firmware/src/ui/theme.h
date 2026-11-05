#pragma once
#include <Arduino.h>

// Cores em RGB565. O tema segue o modo claro/escuro do macOS (o daemon manda "dark" no frame).
// ACCENT é o destaque (gráficos, número que mudou, item selecionado); as outras cores são de estado.
namespace color {
inline uint16_t BG, TRACK, TEXT, MUTED, DIM, GREEN, YELLOW, ORANGE, AMBER, RED, ACCENT, DARK_GREEN;
inline bool dark = false;

inline void apply(bool d) {
  // Paleta do Claude: grafite quente (#1F1E1D) ou marfim (#FAF9F5) de fundo e o terracota do Claude
  // (#D97757) como destaque. As faixas de consumo (verde, âmbar, laranja, vermelho) ficam com tons próprios.
  static constexpr uint16_t DARK[] = {0x18E3, 0x39C6, 0xF7BD, 0xBDB5, 0x840F, 0x758F, 0xE589, 0xEBC6, 0xD507, 0xE2C9, 0xDBAA, 0x1A25};
  static constexpr uint16_t LIGHT[] = {0xFFDE, 0xE71B, 0x18E3, 0x5ACA, 0x8C50, 0x3C6A, 0xBC21, 0xCAC3, 0xAB60, 0xC1C5, 0xC2E7, 0xBF19};
  const uint16_t* t = d ? DARK : LIGHT;
  uint16_t* v[] = {&BG, &TRACK, &TEXT, &MUTED, &DIM, &GREEN, &YELLOW, &ORANGE, &AMBER, &RED, &ACCENT, &DARK_GREEN};
  for (int i = 0; i < 12; i++) *v[i] = t[i];
  dark = d;
}
}  // namespace color

// Faixas de consumo, usadas no anel, nas barras e no LED.
inline int level(int pct) { return pct >= 90 ? 3 : pct >= 80 ? 2 : pct >= 70 ? 1 : 0; }

inline uint16_t levelColor(int pct) {
  const uint16_t byLevel[] = {color::GREEN, color::YELLOW, color::ORANGE, color::RED};
  return byLevel[level(pct)];
}

inline uint16_t blend(uint16_t a, uint16_t b, float t) {  // t=0 -> a, t=1 -> b
  t = constrain(t, 0.f, 1.f);
  auto mix = [&](int shift, int mask) {
    return ((int)(((a >> shift) & mask) * (1 - t) + ((b >> shift) & mask) * t) & mask) << shift;
  };
  return mix(11, 0x1F) | mix(5, 0x3F) | mix(0, 0x1F);
}

constexpr float TAU = 6.2831853f;  // em float: o PI do Arduino é double, que o ESP32 calcula em software

// Respiração suave: 0 -> 1 -> 0 no período dado.
inline float breath(uint32_t periodMs) { return 0.5f - 0.5f * cosf(millis() % periodMs * TAU / periodMs); }
