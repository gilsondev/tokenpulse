#pragma once
// Relógio de cartões que viram (flip retrô): HH:MM ou HH:MM:SS, todos os cartões do mesmo tamanho.
// Cada dígito que muda vira em duas metades, com sombra no cartão que cai. Desenha direto no canvas
// inteiro: só para placas com PSRAM, onde o quadro não é desenhado em faixas.
#include <stdint.h>

namespace flip {

void size(int& w, int& h, bool seconds);              // tamanho do relógio, com ou sem os segundos
void draw(int x, int y, int32_t secs, bool seconds);  // secs: segundos do dia

}  // namespace flip
