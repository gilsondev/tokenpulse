#pragma once
// Ajustes do usuário, gravados na NVS: sobrevivem a reinício e a OTA.
#include <Arduino.h>

namespace settings {

struct Values {
  uint8_t bright = 3;     // 25/50/75/100%
  uint8_t night = 1;      // sem uso: o modo noite (22h–7h) é fixo; o campo fica para não mudar o layout gravado
  uint8_t idle = 1;       // escurecer depois de 5/10/30 min, 3 nunca
  uint8_t theme = 0;      // 0 segue o macOS, 1 claro, 2 escuro
  uint8_t orient = 0;     // 0 vertical, 1 horizontal (ocupa o lugar do antigo giro de 180°)
  uint8_t hideCost = 0;   // 1: esconde os valores em US$
};

extern Values v;

// Calibração do touch resistivo: a leitura crua nas bordas da tela (x = 0 e x = largura, y = 0 e
// y = altura, no retrato nativo) e se os eixos crus andam trocados em relação aos da tela. Fica fora de
// Values: recalibrar não mexe nos ajustes, e os ajustes não perdem a calibração.
struct TouchCal {
  int16_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  uint8_t swap = 0, ok = 0;
};
extern TouchCal cal;
void saveCal();

void begin();  // lê da NVS
void save();   // grava na NVS

uint8_t brightness();       // 0-255 a partir do ajuste
uint32_t idleMs();          // 0: nunca escurece

}  // namespace settings
