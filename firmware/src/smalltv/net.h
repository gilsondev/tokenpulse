#pragma once
// WiFi, servidor HTTP e mDNS. O SmallTV não tem USB de dados: tudo chega por aqui.
//   POST /frame    o frame do daemon (o mesmo JSON da serial, enxuto)
//   GET  /hello    {"board", "fw", "ip", "heap", "safe"}: o "id" da serial
//   GET/POST /update   OTA: a página do ESP8266HTTPUpdateServer, campo "firmware"
//   GET  /         estado; sem WiFi configurado, o formulário da rede
// Sem rede salva, ou sem conseguir entrar nela em 30 s, a placa abre o AP aberto tokenpulse-caixeta
// (192.168.4.1) com o mesmo servidor: é o caminho de volta sem abrir a caixa.
#include <Arduino.h>

namespace net {

void begin();
void loop();
bool online();           // conectada ao WiFi da casa
bool apOn();             // o AP de configuração está no ar
const char* apName();
String address();        // IP na rede da casa, ou o do AP
uint32_t lastDaemonMs();  // última vez que o daemon falou (/hello ou /frame); 0 nunca
bool takeFrame();        // true uma vez por frame novo

}  // namespace net
