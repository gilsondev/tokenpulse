#pragma once
// Quem pode mudar a placa pela página web: o navegador que digitou o código mostrado na tela dela (cookie tp=<token>),
// ou o daemon que recebeu o token no primeiro contato. Ver a página continua livre; mudar ajuste, reiniciar,
// esquecer o WiFi, preparar, enviar firmware e capturar a tela pedem o token. O token é da placa, fica gravado e
// troca no "Preparar para entrega".
#include <Arduino.h>
#if defined(ESP8266)
#define TP_RANDOM() (*(volatile uint32_t*)0x3FF20E44)  // RANDOM_REG32: o gerador de hardware
#else
#include <esp_random.h>
#define TP_RANDOM() esp_random()
#endif

namespace webauth {

constexpr uint32_t CODE_MS = 120000;  // o código fica na tela por 2 min
inline uint16_t code = 0;
inline uint32_t codeMs = 0;
inline uint8_t misses = 0;

inline bool codeActive() { return code && millis() - codeMs < CODE_MS; }
inline int codeSecsLeft() { return codeActive() ? (CODE_MS - (millis() - codeMs)) / 1000 : 0; }

inline void newToken(char* t, size_t n) {
  for (size_t i = 0; i + 2 < n && i < 32; i += 2) snprintf(t + i, 3, "%02x", (unsigned)(TP_RANDOM() & 0xFF));
}

}  // namespace webauth
