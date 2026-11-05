#pragma once
// Guition JC3248W535: ESP32-S3 N16R8, IPS 3,5" 320x480 com AXS15231B por QSPI e touch capacitivo
// no mesmo chip (I2C 0x3B). Sem IMU e sem LED. Pinos do exemplo JC3248W535 da Arduino_GFX 1.4.9.
// https://www.atomic14.com/esp32/boards/guition-jc3248w535/
#include <Arduino.h>
#include <Wire.h>

namespace board {

constexpr const char* NAME = "guition-jc3248w535";
constexpr uint8_t DEFAULT_ROTATION = 0;  // retrato, a orientação nativa do painel
constexpr uint8_t ROT_FOR[4] = {0, 0, 0, 0};

constexpr int PIN_BACKLIGHT = 1;
constexpr int PIN_BUTTON = 0;  // BOOT
constexpr uint8_t TOUCH_ADDR = 0x3B;

void displayBegin();  // guition_jc3248w535.cpp
void displayPush(const uint16_t* px, int w, int h);
void setBrightness(uint8_t v);
// O AXS15231B não gira por hardware: o retrato de cabeça para baixo (rot 2) é feito no displayPush e
// no touch. ponytail: paisagem (rot 1/3) não é suportada; girar 90° pede outro layout de qualquer jeito.
inline bool flipped = false;
void displaySetRotation(uint8_t rot);  // rot 2 só vale com o buffer de faixas (é ele que gira a imagem)
inline int displayWidth() { return 320; }
inline int displayHeight() { return 480; }

inline void begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Wire.begin(4, 8);
  Wire.setClock(400000);
}

inline bool gravity(int16_t&, int16_t&) { return false; }
inline void setLed(uint8_t, uint8_t, uint8_t) {}

// Um dedo na tela, em pixels do retrato nativo. O I2C só é lido a cada 15 ms.
inline bool touch(int16_t& x, int16_t& y) {
  static uint32_t lastMs = 0;
  static bool down = false;
  static int16_t lx = 0, ly = 0;
  if (millis() - lastMs >= 15) {
    lastMs = millis();
    static const uint8_t READ[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 0x08, 0, 0, 0};
    uint8_t buf[8] = {0};
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(READ, sizeof READ);
    down = Wire.endTransmission() == 0 && Wire.requestFrom(TOUCH_ADDR, (uint8_t)8) == 8;
    if (down) {
      Wire.readBytes(buf, 8);
      down = buf[1] > 0 && buf[1] <= 2;  // buf[1] = número de dedos
      lx = (buf[2] & 0x0F) << 8 | buf[3];
      ly = (buf[4] & 0x0F) << 8 | buf[5];
    }
  }
  x = flipped ? 319 - lx : lx, y = flipped ? 479 - ly : ly;
  return down;
}

inline bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }

}  // namespace board
