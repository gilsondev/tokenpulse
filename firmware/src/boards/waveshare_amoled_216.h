#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-2.16: ESP32-S3R8, AMOLED 480x480 com CO5300 por QSPI, touch CST9220,
// QMI8658, PMU AXP2101 com bateria, RTC PCF85063, codec ES8311 com alto-falante e slot TF.
// Todos os chips I2C dividem o barramento SDA 15 / SCL 14. Pinos da wiki da Waveshare:
// https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16
#include <Arduino.h>
#include <Wire.h>

#include "../drivers/qmi8658.h"
#include "profile.h"

namespace board {

constexpr const char* NAME = "waveshare-amoled-216";
constexpr const char* NICK = "Jabuticaba";  // o codinome: uma árvore que lembra a placa
constexpr const char* CODE = "jabuticaba";  // o codinome no endereço: tokenpulse-jabuticaba.local
constexpr const char* BLE_LABEL = "AMOLED";  // no nome Bluetooth: "TokenPulse AMOLED XXXXXX"
constexpr uint8_t DEFAULT_ROTATION = 0;
constexpr uint8_t ROTATIONS = 4;  // tela quadrada: as quatro posições custam o mesmo
constexpr uint8_t ROT_FOR[4] = {0, 2, 3, 1};  // medido segurando a placa nas 4 posições
constexpr Profile PROFILE{.touch = true, .calibrate = false, .imu = true, .led = false, .button = true, .speaker = true, .oled = true, .sd = true, .battery = true, .pxPerMm = 12.3f};
constexpr uint8_t GRID_ROWS[2] = {0, 0};
constexpr uint32_t SERIAL_BAUD = 115200;

constexpr int PIN_SDA = 15, PIN_SCL = 14;
constexpr int SD_PINS[4] = {2, 1, 3, 41};  // CLK, CMD, D0, D3 (o CS do modo SPI)
constexpr int PIN_BUTTON = 18;  // KEY3, o botão do usuário
constexpr int PIN_BOOT = 0;     // silencia o som por 1 h; segurado ao ligar, entra no modo de gravação
constexpr int PIN_PWR = 16;     // o PWR da PMU, invertido por um transistor: nível alto é apertado
constexpr uint8_t PMU_ADDR = 0x34;

// A tela é quadrada e o CO5300 só espelha: as rotações são feitas no envio do quadro, e o toque gira junto.
// Os cantos do vidro são redondos: a interface ocupa o quadrado do meio e a borda preta sobra para eles e para
// o deslocamento contra burn-in.
constexpr int PANEL = 480, PAD = 28, SIDE = PANEL - 2 * PAD;
inline uint8_t rotation = 0;
void displayBegin();  // waveshare_amoled_216.cpp
void displayPush(const uint16_t* px, int y, int w, int h);
// O envio limpa o quadro na cor pushFill (-1: não limpa) enquanto a tela recebe, e o próximo desenho pula a limpeza.
constexpr bool PUSH_CLEARS = true;
inline volatile int32_t pushFill = -1;
void setBrightness(uint8_t v);
inline void displaySetRotation(uint8_t rot) { rotation = rot & 3; }
inline int displayWidth() { return SIDE; }
inline int displayHeight() { return SIDE; }

bool touchNative(int16_t& x, int16_t& y);  // waveshare_amoled_216.cpp
void audioBegin();
void play(uint8_t i, uint8_t vol, uint16_t minMs = 0, uint8_t times = 1);
void pmuBegin();
bool powerState(PowerState& s);
void pmuReport();
bool rtcRead(uint32_t& t);
void rtcWrite(uint32_t t);

inline Qmi8658 imu;

inline void pmuWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

inline void begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_PWR, INPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  // AXP2101: segurar o PWR por 4 s desliga a placa pela própria PMU, mesmo com o firmware travado (os mesmos
  // registradores do claude-desktop-buddy-esp32). O firmware desliga antes, aos 2 s (main.cpp)
  pmuWrite(0x27, 0x10);
  pmuWrite(0x22, 0b110);
  pmuBegin();
  imu.begin(Wire, PIN_SDA, PIN_SCL);
  audioBegin();
}

inline bool gravity(int16_t& x, int16_t& y) {
  int16_t z;
  return imu.accel(x, y, z);
}

inline void setLed(uint8_t, uint8_t, uint8_t) {}
inline bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }
inline bool restPressed() { return digitalRead(PIN_PWR) == HIGH; }
inline bool mutePressed() { return digitalRead(PIN_BOOT) == LOW; }
// AXP2101: o bit 0 do registrador 0x10 desliga na hora (o mesmo que o shutdown() da XPowersLib)
inline void powerOff() {
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(0x10);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(PMU_ADDR, (uint8_t)1) == 1) pmuWrite(0x10, Wire.read() | 1);
}

inline bool touch(int16_t& x, int16_t& y) {
  int16_t tx, ty;
  if (!touchNative(tx, ty)) return false;
  constexpr int N = SIDE - 1;
  tx = constrain(tx - PAD, 0, N), ty = constrain(ty - PAD, 0, N);
  switch (rotation) {
    case 1: x = ty, y = N - tx; break;
    case 2: x = N - tx, y = N - ty; break;
    case 3: x = N - ty, y = tx; break;
    default: x = tx, y = ty;
  }
  return true;
}
inline bool touchRaw(int16_t&, int16_t&) { return false; }

}  // namespace board
