#pragma once
// Ajustes da página de configuração, gravados na EEPROM emulada (um setor perto do fim da flash). A área de
// arquivos fica de fora: ela ainda guarda as imagens do firmware de fábrica, caso ele volte.
#include <Arduino.h>

namespace config {

struct Values {
  uint8_t bright = 100;  // % do brilho de dia, 10 a 100
  uint8_t night = 1;     // 1: escurece das 22h às 7h
  uint8_t theme = 0;     // 0 segue o macOS, 1 claro, 2 escuro
  uint8_t rotation = 0;  // 0 a 3, em passos de 90°
  uint8_t interval = 15; // s em cada tela antes de passar para a próxima; 0 fica só na principal
  uint8_t pages = 63;    // telas na volta: 1 modelos, 2 projetos, 4 sessões, 8 detalhe de cada modelo, 16 histórico, 32 orgs
  char host[32] = "";    // nome na rede (<host>.local); vazio: tokenpulse-<codinome>
  uint8_t hideCost = 0;  // 1: esconde o custo em US$. Campo novo vai no fim: o gravado antes continua válido
  uint8_t alerts = 3;    // 1: sessão pedindo aprovação segura a tela de sessões e pulsa; 2: aviso ao cruzar 70/80/90%
};

extern Values v;
extern uint32_t version;  // sobe a cada save(): a tela e o mDNS reaplicam quando muda

void begin();
void save();
bool validHost(const String& h);  // letras minúsculas, dígitos e hífen, até 31

}  // namespace config
