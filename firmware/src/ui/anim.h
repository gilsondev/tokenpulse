#pragma once
// Estado da animação: o frame traz o alvo, a tela mostra o valor que chega até ele numa mola.
#include <Arduino.h>

namespace anim {

extern float p5, pw, grow;  // grow: 0 a 1 ao entrar na tela (barras e sparkline crescendo)
extern uint32_t tokensMs;   // chegou token novo: o número brilha
extern uint32_t alertMs;    // passou de 70%, 80% ou 90%: pulso e troca de cor
extern uint16_t alertFrom;  // cor da faixa anterior, para a troca de cor
extern uint32_t pwMs;       // o % da semana mudou (ou a tela abriu): um brilho passa na barra

void onFrame();  // chamado a cada frame novo do daemon
void step();     // chamado a cada quadro desenhado
void reset();    // ao trocar de tela ou girar, tudo cresce de novo

// fração de 0 a 1 do tempo decorrido desde `ms`, numa animação de `dur` ms; 1 se nunca começou
inline float since(uint32_t ms, uint32_t dur) { return ms ? (millis() - ms) / (float)dur : 1; }

// Entrada da linha i de uma lista: mola rápida (k=800, ζ=0,6), 60 ms depois da linha anterior.
float entrance(int i);

// Halo da ponta: respira devagar (4 s) e sobe num pulso de 0,8 s quando o consumo muda de faixa.
float halo();

}  // namespace anim
