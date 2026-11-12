#pragma once
#include <ArduinoJson.h>

namespace app {

inline JsonDocument frame;
inline uint32_t lastFrameMs = 0;  // 0 enquanto o Mac ainda não mandou nada
inline bool safe = false;         // modo de recuperação: só WiFi e OTA, nada de tela

#ifndef FW_VERSION
#define FW_VERSION "dev " __DATE__ " " __TIME__  // build local; o CI passa a tag em -DFW_VERSION
#endif
constexpr const char* BOARD = "smalltv-ultra";
constexpr const char* NICK = "Caixeta";  // o codinome: a madeira das caixas, para o cubo
constexpr const char* CODE = "caixeta";
constexpr const char* FIRMWARE = FW_VERSION;

}  // namespace app
