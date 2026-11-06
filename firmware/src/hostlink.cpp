#include "hostlink.h"
#include "ui/screens.h"

#include <Update.h>

#include "app.h"
#include "boards/board.h"
#include "ui/anim.h"
#include "ui/gfx.h"

namespace hostlink {
namespace {

// O daemon manda {"ota": tamanho, "md5": "..."} e depois o .bin em blocos de 1 KB;
// a placa confirma cada bloco com "OK" e mostra o progresso na tela.
void runOta(size_t size, const char* md5) {
  // o Update é um só na placa: o progresso que a página web registrou nele também seria chamado aqui
  struct Busy {
    Busy() { app::serialOta = true; }
    ~Busy() { app::serialOta = false; }
  } busy;
  static uint8_t chunk[1024];
  char status[48];
  if (!md5 || !Update.begin(size)) return (void)Serial.printf("ERRO ota begin: %s\n", Update.errorString());
  if (!Update.setMD5(md5)) {  // sem md5 válido o flash não teria verificação nenhuma
    Update.abort();
    return (void)Serial.println("ERRO ota md5 inválido");
  }
  Serial.setTimeout(3000);
  Serial.println("OK");
  size_t done = 0;
  for (int block = 0; done < size; block++) {
    size_t n = Serial.readBytes(chunk, min(sizeof chunk, size - done));
    if (n == 0 || Update.write(chunk, n) != n) {
      Update.abort();
      Serial.setTimeout(50);
      gfx::loading(gfx::FAIL, "atualizando firmware", "falhou, mantendo a versão atual", (float)done / size, color::RED);
      return (void)Serial.printf("ERRO ota write em %u B\n", done);
    }
    done += n;
    Serial.println("OK");
    if (block % 8 == 0 || done == size) {  // a tela leva ~22 ms; redesenhar a cada bloco dobraria o tempo
      snprintf(status, sizeof status, "%u de %u KB", done / 1024, size / 1024);
      gfx::loading(gfx::DOWNLOAD, "atualizando firmware", status, (float)done / size);
      board::setLed(0, 0, 10 + 30 * breath(1000));
    }
  }
  Serial.setTimeout(50);
  if (!Update.end(true)) {
    gfx::loading(gfx::FAIL, "atualizando firmware", "checksum não bateu, nada mudou", 1, color::RED);
    return (void)Serial.printf("ERRO ota end: %s\n", Update.errorString());
  }
  gfx::loading(gfx::OK, "firmware atualizado", "reiniciando…", 1, color::GREEN);
  Serial.println("DONE");
  Serial.flush();
  delay(600);
  ESP.restart();
}

uint32_t idMs = 0;

void handle(const String& line) {
  if (line == "id") {
    idMs = millis();
    Serial.printf("hello {\"board\":\"%s\",\"fw\":\"%s\"}\n", board::NAME, firmware());
  } else if (line == "imu") {
    int16_t x, y;
    if (board::gravity(x, y)) Serial.printf("acc=%d,%d rot=%d\n", x, y, gfx::rotation());
    else Serial.println("sem IMU");
  } else if (line == "shot") {
    // o último quadro mostrado, cru: cabeçalho em texto e w × h × 2 bytes RGB565 (byte alto primeiro)
    if (!gfx::shown()) return gfx::captureNext();  // desenhando em faixas: sai no próximo quadro
    const int w = gfx::width(), h = gfx::height();
    Serial.printf("SHOT %d %d %d\n", w, h, w * h * 2);
    const uint8_t* px = reinterpret_cast<const uint8_t*>(gfx::shown());
    for (int i = 0; px && i < w * h * 2; i += 4096) Serial.write(px + i, min(4096, w * h * 2 - i));
    Serial.flush();
  } else if (line.startsWith("page ")) {
    screens::goTo(line.substring(5).toInt());
  } else if (line.startsWith("tap ")) {
    const int sp = line.indexOf(' ', 4);
    screens::tap(line.substring(4, sp).toInt(), line.substring(sp + 1).toInt());
  } else if (line == "install") {  // a tela do QR de instalação por 5 s, para screenshot com o daemon rodando
    screens::installPreview();
  } else if (line == "splash") {  // redesenha a abertura e a segura 3 s: para screenshot da tela de boot
    screens::bootSplash();
    screens::holdFor(3000);
  } else if (line == "swipe H") {
    screens::homeUp();
  } else if (line == "swipe U") {
    screens::scroll(1);
  } else if (line == "swipe D") {
    screens::scroll(-1);
  } else if (line == "swipe L") {
    screens::next();
  } else if (line == "swipe R") {
    screens::prev();
  } else if (line.startsWith("{\"ota\"")) {
    JsonDocument cmd;
    if (!deserializeJson(cmd, line)) runOta(cmd["ota"], cmd["md5"]);
  } else if (line.startsWith("{")) {
    // parse num documento à parte: uma linha cortada não pode apagar o frame que está na tela
    JsonDocument next;
    if (DeserializationError err = deserializeJson(next, line)) {
      Serial.printf("frame inválido (%u B): %s; RAM livre %u, maior bloco %u\n", line.length(), err.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      return;
    }
    app::frame = std::move(next);
    app::lastFrameMs = millis();
    anim::onFrame();
  } else if (line.length()) {
    Serial.printf("comando desconhecido: %s\n", line.c_str());
  }
}

}  // namespace

void begin() {
  Serial.setRxBufferSize(8192);  // o padrão de 256 B é menor que um frame (~4 KB com as sessões)
  Serial.begin(board::SERIAL_BAUD);
  Serial.setTimeout(50);
}

void poll() {
  if (Serial.available()) handle(Serial.readStringUntil('\n'));
}

uint32_t lastIdMs() { return idMs; }
bool needsInstall() { return usbPlugged() && !idMs && !app::hasData() && millis() > 20000; }

#if ARDUINO_USB_CDC_ON_BOOT
bool usbPlugged() { return HWCDC::isPlugged(); }
bool portOpen() { return HWCDC::isConnected(); }
#else
// pelo conversor USB-UART a placa não sabe do cabo nem da porta: ligada já é cabo, e o daemon se apresenta
bool usbPlugged() { return true; }
bool portOpen() { return idMs != 0; }
#endif
#ifndef FW_VERSION
#define FW_VERSION "dev " __DATE__ " " __TIME__  // build local; o CI passa a tag (v1.2.0) em -DFW_VERSION
#endif
const char* firmware() { return FW_VERSION; }

}  // namespace hostlink
