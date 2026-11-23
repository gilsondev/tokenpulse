#pragma once
// Página web das placas ESP32. O WiFi fica desligado até a pessoa pedir (Ajustes → Sobre → página web): aí a placa
// abre o WiFi aberto tokenpulse-<codinome> com o portal de configuração. Depois de configurado, a placa entra nessa
// rede ao ligar e a página fica em http://<nome>.local.
// Fora do cabo, os dados também chegam pelo WiFi (wifiData): a placa se anuncia como _tokenpulse._tcp e o daemon
// manda o frame no /frame, com o token que recebeu pela USB.
//   GET  /            ajustes (no WiFi da placa e ainda sem rede: a configuração do WiFi)
//   GET  /wifi        configuração do WiFi        GET /update  atualização guiada
//   GET  /api/state   estado e ajustes             POST /api/config   muda os ajustes
//   GET  /hello       placa e firmware, para o daemon   POST /frame   os dados; a resposta leva a ação pedida na tela
#include <Arduino.h>

namespace webui {

void begin();   // no boot: entra na rede salva, se houver; senão não liga nada
void loop();
void openAp();  // abre o WiFi de configuração da placa (pedido na tela)
void join(const char* ssid, const char* pass);  // o WiFi do computador, mandado pelo daemon no cabo
String joinStatus();  // "Conectando em X", "Senha errada para X"...; vazio se nada foi pedido
bool online();  // na rede da casa
bool wifiData();  // sem cabo, os dados vêm pelo WiFi: placa sem Bluetooth, ou o ajuste Sem fio em WiFi
bool wifiFed();   // chegou um /frame do daemon nos últimos 10 s
void queueAction(const char* line);  // ACTION ... para o daemon, na resposta do próximo /frame
void pause();   // na bateria: desliga o WiFi (o AP de configuração, se aberto, continua)
void resume();  // volta para a rede salva
bool apOn();
const char* apName();
const char* host();  // nome .local
String address();    // IP na rede, ou o do WiFi da placa

}  // namespace webui
