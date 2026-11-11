#pragma once
// Camada de placa: tudo que muda de um ESP para outro fica num header em boards/.
// A placa é escolhida na compilação por uma flag do env em platformio.ini.
//
// Contrato que cada header de placa cumpre, dentro de `namespace board`:
//
//   constexpr const char* NAME           nome do env no platformio.ini; o daemon usa para achar o .bin do OTA
//   constexpr uint8_t DEFAULT_ROTATION
//   constexpr uint8_t ROTATIONS          2: em pé e deitada; 4: também as duas viradas de 180° (Ajustes → Tela)
//   constexpr uint8_t ROT_FOR[4]         rotação quando a gravidade aponta para +X, -X, +Y, -Y
//   constexpr Profile PROFILE            o hardware que a placa tem (profile.h); a UI decide por ele
//   constexpr uint32_t SERIAL_BAUD       velocidade da serial; só conta em placa com conversor USB-UART
//                                        (CH340, CP210x). Na USB nativa do S3 qualquer valor serve.
//
//   Tela. A UI desenha num canvas em memória e entrega o quadro pronto; a placa usa o driver
//   que quiser (LovyanGFX, Arduino_GFX, esp_lcd, o do fabricante).
//   void displayBegin()
//   void displaySetRotation(uint8_t rot)  0 retrato, 1 deitada (Ajustes → Tela); 2 e 3 só pelo IMU. Depois
//                                         disso displayWidth/Height e touch já valem girados
//   int displayWidth(), displayHeight()
//   void setBrightness(uint8_t v)          0 apaga, 255 brilho máximo
//   void displayPush(const uint16_t* px, int y, int w, int h)   linhas y a y+h-1 em RGB565 big-endian
//                                         (byte alto primeiro, o formato que painéis SPI recebem). Com
//                                         PSRAM chega sempre o quadro inteiro (y = 0); sem ela, faixas.
//
//   constexpr uint8_t GRID_ROWS[2]        linhas de cartões de workspaces e sessões, em pé e deitada; 0: pela
//                                         altura mínima do cartão. Cartão mais baixo que ela usa o layout compacto
//   constexpr bool PUSH_CLEARS            true: o displayPush limpa o quadro na cor board::pushFill depois de
//                                         ler (fica livre enquanto o DMA transmite); o gfx pula a limpeza seguinte
//
//   Periféricos. Placa sem o recurso implementa a função vazia.
//   void begin()                          pinos, I2C e sensores; roda depois do displayBegin()
//   bool gravity(int16_t& x, int16_t& y)  eixos X/Y do acelerômetro; false se não há IMU ou ela não respondeu
//   void setLed(uint8_t r, uint8_t g, uint8_t b)
//   bool buttonPressed()
//   bool restPressed()                    botão de descanso (PWR); as placas sem ele devolvem false
//   bool mutePressed()                    botão de silenciar (BOOT, quando ele não é o botão principal)
//   void powerOff()                       desliga a placa (PMU); as placas sem PMU não fazem nada
//   bool powerState(PowerState& s)        bateria e USB (profile.h); false sem PMU
//   bool rtcRead(uint32_t& t)             hora local guardada no RTC (segundos desde 1970); false sem RTC ou parado
//   void rtcWrite(uint32_t t)             grava a hora local no RTC
//   void play(uint8_t i, uint8_t vol, uint16_t minMs = 0, uint8_t times = 1)
//                                         toca o som i de sound.h no volume vol (0-100), sem bloquear; repete até
//                                         tocar `times` vezes e durar ao menos minMs
//   bool touch(int16_t& x, int16_t& y)   um dedo na tela, em pixels da tela; false sem touch ou sem toque
//   bool touchRaw(int16_t& x, int16_t& y) a leitura crua do touch resistivo, para a calibração
//                                        (PROFILE.calibrate); as outras placas devolvem false
//
// A UI escolhe o layout pela tela já girada e pelo perfil (gfx::ui()): compacta abaixo de 220 px
// no lado menor ou sem touch; média até 300 px; grande daí para cima.
//
// Para uma placa nova: copie o header mais parecido, ajuste e inclua aqui. O env dela precisa
// de duas partições de app para o OTA (min_spiffs.csv em 4 MB, default_16MB.csv em 16 MB).

#if defined(BOARD_WAVESHARE_S3_LCD_147B)
#include "waveshare_s3_lcd_147b.h"
#elif defined(BOARD_GUITION_JC3248W535)
#include "guition_jc3248w535.h"
#elif defined(BOARD_CYD_2432S028R)
#include "cyd_2432s028r.h"
#elif defined(BOARD_WAVESHARE_AMOLED_216)
#include "waveshare_amoled_216.h"
#else
#error "Defina a placa no env do platformio.ini, por exemplo -DBOARD_WAVESHARE_S3_LCD_147B"
#endif
