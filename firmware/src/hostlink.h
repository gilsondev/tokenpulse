#pragma once
// Protocolo com o daemon do Mac: uma linha por mensagem na serial USB.
//   Mac -> placa   {...frame...}                      dados para as telas, 1 por segundo
//                  id                                 a placa responde: hello {"board": ..., "fw": ...}
//                  {"ota": tamanho, "md5": "..."}     seguido do .bin em blocos de 1 KB
//                  imu                                diagnóstico: leitura crua da gravidade
//                  shot                               a placa manda SHOT w h n e o último quadro cru
//                  page N / tap X Y / swipe L|R|U|D|H    navegação remota, para testar e tirar screenshots
//                  splash                             mostra a abertura por 3 s (screenshot da tela de boot)
//   placa -> Mac   OK / DONE / ERRO ...               respostas do OTA; o resto é log

#include <Arduino.h>

namespace hostlink {

void action(const char* line);  // pede uma ação ao Mac (ACTION ...): pela serial e, se houver, pelo BLE

void begin();
void poll();  // lê e trata o que chegou na serial; chamado a cada volta do loop
uint32_t lastIdMs();  // quando o daemon perguntou "id" pela última vez (0: nunca)
// Cabo ok, nenhum daemon se apresentou e nenhum dado desde o boot, há 20 s: quase sempre é quem acabou de
// receber a placa. A tela de espera troca as etapas pelo QR code da instalação.
bool needsInstall();
bool usbPlugged();    // o Mac reconheceu a placa (sem USB nativa: sempre true)
bool portOpen();      // alguém abriu a porta serial
const char* firmware();  // data e hora da compilação

}  // namespace hostlink
