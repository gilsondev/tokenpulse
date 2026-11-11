#pragma once
// Dados sem fio: as mesmas linhas da serial por Bluetooth LE, num serviço no padrão "UART" da Nordic (uma
// característica recebe, outra avisa). O daemon só usa quando a placa não está na USB, por exemplo na bateria.
// OTA e screenshot continuam só pela USB: são grandes demais para o BLE.
#include <Arduino.h>

namespace ble {

void begin(const char* name);   // anuncia com este nome (o mesmo do WiFi da placa: tokenpulse-XXXXXX)
bool poll(String& line);        // próxima linha completa que chegou pelo BLE
void send(const char* line);    // manda uma linha (com '\n') ao Mac, se houver alguém conectado
bool connected();

}  // namespace ble
