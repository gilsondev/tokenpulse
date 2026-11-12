#pragma once
#include <stdint.h>

#include "../boards/profile.h"
// Telas para retrato grande (320x480): grade de 16 px, Inter com anti-aliasing, um número herói por tela.
// Telas pequenas usam os desenhos de screens.cpp.

namespace screens::tall {

constexpr int PAGES = 4;  // principal, sessões, modelos, histórico; os ajustes abrem pela engrenagem
inline bool codexMain = false;  // a principal mostrada é a do Codex (bloco "cx" do frame)

void drawWaiting();
void drawShutdown(float p);  // PWR segurado: p de 0 a 1 até a PMU desligar
void drawAttention(int r, uint32_t ageMs);  // a sessão (índice em sess.rows) que passou a esperar você
bool attentionCenter(int x, int y);           // o toque caiu no botão "abrir no Mac" do destaque
void drawSaver();   // OLED parada: só o relógio, em cinza sobre preto, mudando de lugar a cada minuto
void drawInstall();  // QR code da página /comecar, para quem ainda não instalou o daemon
void drawMain(bool live, int page);
void drawSessions(bool live, int page, int ws, int sp);  // ws -1: a lista de workspaces; sp: a página
int pagesOf(int ws);  // páginas da lista de workspaces (ws -1) ou das sessões de um workspace
int workspaceAt(int x, int y, int sp);  // workspace sob o toque, ou -1
bool backAt(int x, int y);  // o título que volta para a lista
int directSession(int ws);  // o Claude Desktop com uma sessão só: a sessão (índice em sess.rows), ou -1
bool focusRow(int r);       // pede ao Mac para trazer a sessão para a frente
void drawModels(bool live, int page);
void drawModelDetail(bool live, int page, int idx);
void drawHistory(bool live, int page, int sel);
void drawSettings(bool live);
void settingsHome();  // os ajustes abrem na lista de grupos
bool settingsScroll(int dir);  // arrastar na vertical dentro de um grupo maior que a tela
void drawAbout(bool live, bool armed);
void aboutTop();             // o Sobre abre do começo
bool aboutScroll(int dir);   // arrastar na vertical: +1 desce; false se já está na ponta
int chargeEtaMin(const board::PowerState& ps);  // minutos até a carga completa, ou -1 sem estimativa
void drawWeb(bool live);  // QR da página web, ou do WiFi da placa para configurá-la
void drawAccount(bool live);

int modelAt(int x, int y);       // modelo sob o toque, ou -1
int historyBarAt(int x, int y);  // dia do histórico sob o toque, ou -1
int settingsTap(int x, int y);   // 0 trocou um ajuste, 1 abriu o Sobre, 2 abriu a Conta, -1 nada
bool accountTap(int x, int y);   // pediu uma troca de conta ao Mac; false: fechar o painel
int aboutButtonAt(int x, int y); // sob o toque: 1 reiniciar, 2 calibrar o toque, 3 página web, 0 nenhum
bool gearAt(int x, int y);       // o toque caiu na engrenagem do cabeçalho
bool orgTitleAt(int x, int y);   // o toque caiu no nome da org (tela principal)
bool suggestionAt(int x, int y); // o toque caiu no botão "esgotado · trocar para…" (tela principal)
bool sessionTap(int x, int y, int ws, int sp);  // pediu ao Mac para trazer a sessão tocada
void setGearActive(bool on);     // engrenagem acesa enquanto os ajustes estão abertos

}  // namespace screens::tall
