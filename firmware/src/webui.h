#pragma once
// Página web das placas USB. Os dados continuam chegando pelo cabo; o WiFi serve só para a configuração.
// Ele fica desligado até a pessoa pedir (Ajustes → Sobre → página web): aí a placa abre o WiFi aberto
// tokenpulse-XXXXXX com o portal de configuração. Depois de configurado, a placa entra nessa rede ao ligar e a
// página fica em http://<nome>.local.
//   GET  /            ajustes (no WiFi da placa e ainda sem rede: a configuração do WiFi)
//   GET  /wifi        configuração do WiFi        GET /update  atualização guiada
//   GET  /api/state   estado e ajustes             POST /api/config   muda os ajustes
#include <Arduino.h>

namespace webui {

void begin();   // no boot: entra na rede salva, se houver; senão não liga nada
void loop();
void openAp();  // abre o WiFi de configuração da placa (pedido na tela)
bool online();  // na rede da casa
bool apOn();
const char* apName();
const char* host();  // nome .local
String address();    // IP na rede, ou o do WiFi da placa

}  // namespace webui
