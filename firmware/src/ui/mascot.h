#pragma once
// Mascote em pixel art desenhado aqui (não é o do Claude Code): um bichinho na cor do Claude que
// mostra o estado da sessão. Trabalhando anda, na sua vez dá pulinhos, bloqueado treme, parado dorme.
#include <Arduino.h>

namespace mascot {

enum Mood : uint8_t { WORK, TURN, IDLE, BLOCK };  // BLOCK: esperando você aprovar algo

constexpr int W = 12;  // em pixels do sprite; na tela cada um vira um bloco de `px` × `px`

// (x, y): canto superior esquerdo; `seed` desencontra a animação de mascotes vizinhos
void draw(int x, int y, int px, Mood mood, uint16_t bg, uint32_t seed = 0);

}  // namespace mascot
