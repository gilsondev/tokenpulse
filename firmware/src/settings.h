#pragma once
// Ajustes do usuário, gravados na NVS: sobrevivem a reinício e a OTA.
#include <Arduino.h>

namespace settings {

struct Values {
  uint8_t bright = 3;     // 25/50/75/100%
  uint8_t night = 1;      // sem uso: o modo noite (22h–7h) é fixo; o campo fica para não mudar o layout gravado
  uint8_t idle = 1;       // escurecer depois de 5/10/30 min, 3 nunca
  uint8_t theme = 0;      // 0 segue o macOS, 1 claro, 2 escuro
  uint8_t orient = 0;     // rotação da tela: 0 em pé, 1 deitada, 2 e 3 os mesmos virados 180° (board::ROTATIONS)
  uint8_t hideCost = 0;   // 1: esconde os valores em US$
};

extern Values v;

// Ajustes que só a página web muda, numa chave própria da NVS: os Values gravados continuam valendo.
struct Web {
  uint8_t interval = 0;  // s em cada tela antes de passar sozinha para a próxima; 0 só troca pelo toque
  uint8_t pages = 7;     // telas na volta automática: 1 sessões, 2 modelos, 4 histórico (a principal sempre)
  uint8_t alerts = 3;    // sessão nova pedindo aprovação: 1 abre as sessões e pulsa o brilho, 2 toca um som
  uint8_t wifi = 0;      // 1: a pessoa configurou o WiFi pela placa; ela entra na rede ao ligar
  char host[32] = "";    // nome na rede (<host>.local); vazio: tokenpulse-<codinome>
  uint8_t sound = 6;     // som da sessão esperando você, tocado 2 vezes (sound.h; o Vidro)
  uint8_t volume = 2;    // 1 baixo, 2 médio, 3 alto
  uint8_t restSecs = 1;  // relógio do descanso (OLED): 0 HH:MM, 1 HH:MM:SS
  uint8_t restPages = 7; // telas do descanso: 1 relógio, 2 uso, 4 sessões, 8 modelos, 16 histórico
  uint8_t soundOut = 9;  // som quando a janela de 5h esgota, ~2 s (sound.h; sound::COUNT: desligado; os Três tons)
  uint8_t soundNew = 0;  // som quando a janela renova e o uso volta a ~0%, ~2 s (a Pergunta 1)
  uint8_t soundWeek = 10; // som quando o limite da semana esgota, ~2 s (o Pep)
  uint8_t soundBat = 7;   // bateria abaixo de 15% fora do USB (o Gongo)
  uint8_t soundMac = 3;   // 1 min sem dados do Mac (a Pergunta 4)
  uint8_t soundBle = 5;   // Bluetooth conectou (uma vez) ou caiu (duas) (a Confirmação 2)
  uint8_t paired = 0;     // 1: um daemon já falou com a placa. Sem isso, a primeira tela é o QR da instalação
  uint8_t batBright = 2;  // brilho na bateria: 25/50/70/100% (só placa com bateria; o bright vale no USB)
  uint8_t orientSet = 0;  // 1: a pessoa já disse qual lado fica embaixo; 0: o primeiro uso pergunta
  char token[33] = "";    // a chave da página web (web/auth.h); vazia: gera no boot. Troca no preparo
  // campo novo entra sempre no fim: o gravado por um firmware antigo é menor e o resto fica no padrão
};
extern Web web;
void saveWeb();
bool validHost(const String& h);  // letras minúsculas, dígitos e hífen, até 31

// Calibração do touch resistivo: a leitura crua nas bordas da tela (x = 0 e x = largura, y = 0 e
// y = altura, no retrato nativo) e se os eixos crus andam trocados em relação aos da tela. Fica fora de
// Values: recalibrar não mexe nos ajustes, e os ajustes não perdem a calibração.
struct TouchCal {
  int16_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  uint8_t swap = 0, ok = 0;
};
extern TouchCal cal;
void saveCal();

void begin();  // lê da NVS
void save();   // grava na NVS

uint8_t brightness();       // 0-255 a partir do ajuste (no USB)
uint8_t batteryBrightness(); // 0-255 na bateria: 25/50/70/100%
uint32_t idleMs();          // 0: nunca escurece
uint8_t volumePct();        // volume do alto-falante, 0-100

}  // namespace settings
