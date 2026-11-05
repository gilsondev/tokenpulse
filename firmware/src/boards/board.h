#pragma once
// Camada de placa: tudo que muda de um ESP para outro fica num header em boards/.
// A placa é escolhida na compilação por uma flag do env em platformio.ini.
//
// Contrato que cada header de placa cumpre, dentro de `namespace board`:
//
//   constexpr const char* NAME           nome do env no platformio.ini; o daemon usa para achar o .bin do OTA
//   constexpr uint8_t DEFAULT_ROTATION
//   constexpr uint8_t ROT_FOR[4]         rotação quando a gravidade aponta para +X, -X, +Y, -Y
//
//   Tela. A UI desenha num canvas em memória e entrega o quadro pronto; a placa usa o driver
//   que quiser (LovyanGFX, Arduino_GFX, esp_lcd, o do fabricante).
//   void displayBegin()
//   void displaySetRotation(uint8_t rot)  0 a 3; depois disso displayWidth/Height já valem girados
//   int displayWidth(), displayHeight()
//   void setBrightness(uint8_t v)          0 apaga, 255 brilho máximo
//   void displayPush(const uint16_t* px, int w, int h)   quadro inteiro em RGB565 big-endian
//                                         (byte alto primeiro, o formato que painéis SPI recebem)
//
//   Periféricos. Placa sem o recurso implementa a função vazia.
//   void begin()                          pinos, I2C e sensores; roda depois do displayBegin()
//   bool gravity(int16_t& x, int16_t& y)  eixos X/Y do acelerômetro; false se não há IMU ou ela não respondeu
//   void setLed(uint8_t r, uint8_t g, uint8_t b)
//   bool buttonPressed()
//   bool touch(int16_t& x, int16_t& y)   um dedo na tela, em pixels da tela; false sem touch ou sem toque
//
// Para uma placa nova: copie o header mais parecido, ajuste e inclua aqui. O env dela precisa
// de duas partições de app para o OTA (default.csv em 4 MB, default_16MB.csv em 16 MB) e,
// para telas acima de ~240x240, de PSRAM: o canvas usa largura × altura × 2 bytes.

#if defined(BOARD_WAVESHARE_S3_LCD_147B)
#include "waveshare_s3_lcd_147b.h"
#elif defined(BOARD_GUITION_JC3248W535)
#include "guition_jc3248w535.h"
#else
#error "Defina a placa no env do platformio.ini, por exemplo -DBOARD_WAVESHARE_S3_LCD_147B"
#endif
