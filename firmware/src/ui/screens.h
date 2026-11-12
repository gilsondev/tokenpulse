#pragma once
// Telas do monitor. O botão ou o toque alternam entre elas; a principal é a 0.

#include <stdint.h>

namespace screens {

constexpr int COUNT = 3;  // telas pequenas: principal, modelos, projetos (as grandes: screens_tall.h)

void bootSplash();
void installPreview();  // mostra a tela do QR de instalação por 5 s, para screenshot
void offlinePreview();
void attentionPreview();
void mcAlertPreview();    // o aviso do Multica que está no frame, para screenshot  // o destaque da primeira sessão que espera você, para screenshot
void shuttingDown(uint32_t heldMs);  // PWR segurado há heldMs (0: soltou): a tela de desligando  // mostra a tela de sem dados por 5 s, para screenshot
void holdFor(uint32_t ms);  // não redesenha por `ms` (o que está na tela fica, para screenshot)
void render();             // desenha a tela atual (ou a de espera) e atualiza o LED de status
void next();               // clique, toque ou arrastar para a esquerda
void prev();               // arrastar para a direita
void home();               // clique longo ou toque segurado: volta para a principal
void pullDown();           // arrastar da borda de cima: abre a lista de ajustes
void homeUp();             // arrastar da borda de baixo: volta para a principal subindo a tela
void tap(int x, int y);    // toque curto: abre o detalhe do que foi tocado; fora disso, próxima tela
void goTo(int page);       // vai direto para uma tela (comando remoto)
void scroll(int dir);      // arrastar na vertical: +1 próximas sessões, -1 anteriores
bool calibrateTouch();     // touch resistivo: 4 alvos, grava na NVS; false se desistiu ou errou os alvos

}  // namespace screens
