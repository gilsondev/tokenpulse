#pragma once
// Dados sem fio: as mesmas linhas da serial por Bluetooth LE, num serviço no padrão "UART" da Nordic (uma
// característica recebe, outra avisa). O daemon só usa quando a placa não está na USB, por exemplo na bateria.
// OTA e screenshot continuam só pela USB: são grandes demais para o BLE. O link é criptografado (LE Secure
// Connections, pareamento sem confirmação): a placa ignora o que chega antes da criptografia.
#include <Arduino.h>

namespace ble {

void begin(const char* name);   // anuncia com este nome (TokenPulse <placa> <final do chip>)
bool poll(String& line);        // próxima linha completa que chegou pelo BLE
void send(const char* line);    // manda uma linha (com '\n') ao Mac, se houver alguém conectado
bool connected();
const char* name();             // o nome anunciado, que o daemon guarda para achar a placa ("" sem BLE)
void forgetBonds();             // apaga os pareamentos guardados: na entrega, o computador antigo deixa de conectar

}  // namespace ble
