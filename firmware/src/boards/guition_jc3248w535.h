#pragma once
// Guition JC3248W535: ESP32-S3 N16R8, IPS 3,5" 320x480 com AXS15231B por QSPI e touch capacitivo
// no mesmo chip (I2C 0x3B). Sem IMU e sem LED. Pinos do exemplo JC3248W535 da Arduino_GFX 1.4.9.
// https://www.atomic14.com/esp32/boards/guition-jc3248w535/
#include <Arduino.h>
#include <Wire.h>

#include "profile.h"

namespace board {

constexpr const char* NAME = "guition-jc3248w535";
constexpr uint8_t DEFAULT_ROTATION = 0;  // retrato, a orientação nativa do painel
constexpr uint8_t ROTATIONS = 2;  // o quadro deitado é girado no envio; as viradas de 180° não foram feitas
constexpr uint8_t ROT_FOR[4] = {0, 0, 0, 0};
constexpr Profile PROFILE{.touch = true, .calibrate = false, .imu = false, .led = false, .button = true};
constexpr uint32_t SERIAL_BAUD = 115200;

constexpr int PIN_BACKLIGHT = 1;
constexpr int PIN_BUTTON = 0;  // BOOT
constexpr uint8_t TOUCH_ADDR = 0x3B;

void displayBegin();  // guition_jc3248w535.cpp
void displayPush(const uint16_t* px, int y, int w, int h);  // tem PSRAM: sempre o quadro inteiro, y = 0
void setBrightness(uint8_t v);
// O AXS15231B não gira por hardware: deitada (rot 1), o displayPush gira o quadro de 480x320 e o
// toque gira junto. Retrato (rot 0) é o nativo.
inline bool landscape = false;
void displaySetRotation(uint8_t rot);
inline int displayWidth() { return landscape ? 480 : 320; }
inline int displayHeight() { return landscape ? 320 : 480; }

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
  x = landscape ? ly : lx, y = landscape ? 319 - lx : ly;  // deitada: o retrato girado 90° no sentido horário
  return down;
}

inline bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }
inline bool touchRaw(int16_t&, int16_t&) { return false; }  // capacitivo: já sai em pixels

inline void play(uint8_t, uint8_t) {}
inline bool restPressed() { return false; }
inline void powerOff() {}

}  // namespace board
