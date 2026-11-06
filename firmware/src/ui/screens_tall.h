#pragma once
// Telas para retrato grande (320x480): grade de 16 px, Inter com anti-aliasing, um número herói por tela.
// Telas pequenas usam os desenhos de screens.cpp.

namespace screens::tall {

constexpr int PAGES = 4;  // principal, sessões, modelos, histórico; os ajustes abrem pela engrenagem

void drawWaiting();
void drawMain(bool live, int page);
void drawSessions(bool live, int page, int ws, int scroll);  // ws -1: lista de workspaces
void drawModels(bool live, int page);
void drawModelDetail(bool live, int page, int idx);
void drawHistory(bool live, int page, int sel);
void drawSettings(bool live);
void drawAbout(bool live, bool armed);
void drawAccount(bool live);

int workspaceAt(int x, int y, int scroll);  // workspace sob o toque, ou -1
int wsPerPage();                 // workspaces por página: 10 em pé, 9 deitada
int modelAt(int x, int y);       // modelo sob o toque, ou -1
int historyBarAt(int x, int y);  // dia do histórico sob o toque, ou -1
int settingsTap(int x, int y);   // 0 trocou um ajuste, 1 abriu o Sobre, 2 abriu a Conta, -1 nada
bool accountTap(int x, int y);   // pediu uma troca de conta ao Mac; false: fechar o painel
int aboutButtonAt(int x, int y); // botão do Sobre sob o toque: 1 reiniciar, 2 calibrar o toque, 0 nenhum
bool gearAt(int x, int y);       // o toque caiu na engrenagem do cabeçalho
bool orgTitleAt(int x, int y);   // o toque caiu no nome da org (tela principal)
bool suggestionAt(int x, int y); // o toque caiu no botão "esgotado · trocar para…" (tela principal)
bool sessionTap(int x, int y, int ws, int scroll);  // pediu ao Mac para trazer a sessão tocada
void setGearActive(bool on);     // engrenagem acesa enquanto os ajustes estão abertos

}  // namespace screens::tall
