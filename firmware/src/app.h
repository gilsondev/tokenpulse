#pragma once
// Estado compartilhado entre o protocolo serial, as telas e o loop principal.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace app {

inline JsonDocument frame;       // último frame do daemon, como chegou
inline uint32_t lastFrameMs = 0;  // 0 enquanto o Mac ainda não mandou nada
inline uint32_t approveMs = 0;    // quando surgiu a última sessão pedindo aprovação (0: nunca)
inline int blocks = 0;            // sessões pedindo aprovação agora
inline bool serialOta = false;    // OTA pela USB em andamento: o progresso da página web não desenha

inline bool hasData() { return lastFrameMs != 0; }

// Para quem acabou de receber a placa: a página que ensina a instalar o daemon no Mac. O QR leva ao github.io,
// que não depende do encurtador; o endereço escrito é o curto, para digitar.
constexpr const char* START_URL = "https://gilsondev.github.io/tokenpulse/comecar/";
constexpr const char* START_SHORT = "tokenpulse.s.gy/comecar";
inline bool live() { return hasData() && millis() - lastFrameMs <= 5000; }
// o % de 5h e da semana vem da API ou da statusline; marcado como velho depois de 5 min
inline int dataAge() { return frame["age"] | 0; }
inline bool stale() { return dataAge() > 300; }

}  // namespace app
