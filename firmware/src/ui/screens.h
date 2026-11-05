#pragma once
// Telas do monitor. O botão ou o toque alternam entre elas; a principal é a 0.

#include <stdint.h>

namespace screens {

constexpr int COUNT = 3;  // telas pequenas: principal, modelos, projetos (as grandes: screens_tall.h)

void bootSplash();
void holdFor(uint32_t ms);  // não redesenha por `ms` (o que está na tela fica, para screenshot)
void render();             // desenha a tela atual (ou a de espera) e atualiza o LED de status
void next();               // clique, toque ou arrastar para a esquerda
void prev();               // arrastar para a direita
void home();               // clique longo ou toque segurado: volta para a principal
void homeUp();             // arrastar da borda de baixo: volta para a principal subindo a tela
void tap(int x, int y);
void goTo(int page);
void scroll(int dir);      // arrastar na vertical: +1 próximas sessões, -1 anteriores       // vai direto para uma tela (comando remoto)    // toque curto: abre o detalhe do que foi tocado; fora disso, próxima tela

}  // namespace screens
