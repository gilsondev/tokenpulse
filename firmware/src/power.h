#pragma once
#include <stdint.h>
// Brilho da tela: cheio enquanto alguém mexe, mais baixo parado, quase apagado à noite.
// O toque numa tela escurecida só acende; o gesto não vira ação.

namespace power {

void update();   // a cada quadro: aproxima o brilho do alvo
bool dimmed();   // a tela está escurecida
bool idle();     // ninguém mexe há idleMs, sem alerta nem aprovação pendente
void wake();     // toque ou botão: brilho cheio e o relógio de inatividade recomeça
void rest();     // entra no descanso agora, como se ninguém mexesse há idleMs
uint32_t lastActiveMs();  // último toque ou botão

}  // namespace power
