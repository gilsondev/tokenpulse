#pragma once
// Cartão SD: guarda o histórico diário além dos 14 dias que o Mac manda. As gravações são raras (o dia de hoje a
// cada 10 min) e seguras: o arquivo novo é escrito ao lado, o atual vira cópia e o novo toma o lugar, então uma
// queda de energia no meio deixa sempre um arquivo inteiro.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace sd {

enum State : uint8_t { NONE, READY, UNREADABLE };  // sem cartão (ou placa sem leitor), pronto, não montou

void begin();
State state();
uint32_t freeMB();
int days();                         // dias guardados
void saveHistory(JsonArrayConst h);  // a cada frame; grava de fato no máximo a cada 10 min
bool format();                      // só quando não montou: cria uma partição FAT e monta

}  // namespace sd
