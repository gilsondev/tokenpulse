#pragma once
// Mascote em pixel art desenhado aqui (não é o do Claude Code): um bichinho na cor do Claude que
// mostra o estado da sessão. Trabalhando anda, na sua vez dá pulinhos, bloqueado treme, parado dorme.
#include <Arduino.h>

namespace mascot {

enum Mood : uint8_t { WORK, TURN, IDLE, BLOCK, OFFLINE };  // BLOCK: esperando você aprovar algo; OFFLINE: sem dados do Mac

constexpr int W = 12;  // em pixels do sprite; na tela cada um vira um bloco de `px` × `px`
constexpr int W_OFFLINE = 23;  // com o cabo e a tomada ao lado

// (x, y): canto superior esquerdo; `seed` desencontra a animação de mascotes vizinhos. codex: o bichinho verde
// com o rosto ">_" das sessões do Codex, no lugar do laranja do Claude
void draw(int x, int y, int px, Mood mood, uint16_t bg, uint32_t seed = 0, bool codex = false);

}  // namespace mascot
