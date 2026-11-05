#pragma once
// Brilho da tela: cheio enquanto alguém mexe, mais baixo parado, quase apagado à noite.
// O toque numa tela escurecida só acende; o gesto não vira ação.

namespace power {

void update();   // a cada quadro: aproxima o brilho do alvo
bool dimmed();   // a tela está escurecida
void wake();     // toque ou botão: brilho cheio e o relógio de inatividade recomeça

}  // namespace power
