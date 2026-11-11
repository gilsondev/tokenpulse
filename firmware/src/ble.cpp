#include "ble.h"

#ifdef TOKENPULSE_BLE
#include <NimBLEDevice.h>

namespace ble {
namespace {
constexpr const char* UUID_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* UUID_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // o Mac escreve aqui
constexpr const char* UUID_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // a placa avisa por aqui
constexpr size_t MAX_LINE = 8192;  // o mesmo teto do buffer da serial

NimBLECharacteristic* tx = nullptr;
volatile bool linked = false;
// O NimBLE chama onWrite na tarefa dele: a linha montada vai por uma fila para o loop, que é quem desenha.
QueueHandle_t lines = nullptr;
String partial;

class Rx : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const std::string v = c->getValue();
    for (char ch : v) {
      if (ch != '\n') {
        if (partial.length() < MAX_LINE) partial += ch;
        continue;
      }
      auto* line = new String(std::move(partial));
      partial = String();
      if (xQueueSend(lines, &line, 0) != pdTRUE) delete line;  // o loop está atrasado: fica o frame seguinte
    }
  }
};

class Server : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { linked = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    linked = false;
    partial = String();
    NimBLEDevice::startAdvertising();
  }
};
}  // namespace

void begin(const char* name) {
  lines = xQueueCreate(4, sizeof(String*));
  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new Server());
  NimBLEService* svc = server->createService(UUID_SVC);
  svc->createCharacteristic(UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(new Rx());
  tx = svc->createCharacteristic(UUID_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(name);
  adv->addServiceUUID(UUID_SVC);
  adv->enableScanResponse(true);
  adv->start();
}

bool poll(String& line) {
  String* p = nullptr;
  if (!lines || xQueueReceive(lines, &p, 0) != pdTRUE) return false;
  line = std::move(*p);
  delete p;
  return true;
}

void send(const char* line) {
  if (!linked || !tx) return;
  tx->setValue(reinterpret_cast<const uint8_t*>(line), strlen(line));  // as linhas de ação são curtas
  tx->notify();
}

bool connected() { return linked; }

}  // namespace ble

#else  // placa sem BLE: nada a fazer

namespace ble {
void begin(const char*) {}
bool poll(String&) { return false; }
void send(const char*) {}
bool connected() { return false; }
}  // namespace ble

#endif
