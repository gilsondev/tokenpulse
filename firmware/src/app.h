#pragma once
// Estado compartilhado entre o protocolo serial, as telas e o loop principal.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace app {

inline JsonDocument frame;       // último frame do daemon, como chegou
inline uint32_t lastFrameMs = 0;  // 0 enquanto o Mac ainda não mandou nada

inline bool hasData() { return lastFrameMs != 0; }
inline bool live() { return hasData() && millis() - lastFrameMs <= 5000; }
// o % de 5h e da semana vem da API ou da statusline; marcado como velho depois de 5 min
inline int dataAge() { return frame["age"] | 0; }
inline bool stale() { return dataAge() > 300; }

}  // namespace app
