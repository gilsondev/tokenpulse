#include "net.h"

#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "app.h"
#include "config.h"
#include "page.h"
#include "screen.h"

namespace net {
namespace {

constexpr uint32_t JOIN_MS = 30000;  // sem entrar na rede salva nesse tempo, abre o AP

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
char ap[24];
bool ap_on = false, mdns_on = false, fresh = false;
uint32_t startMs = 0, onlineSinceMs = 0, daemonMs = 0, rebootAtMs = 0;
String mdnsHost;  // o nome com que o mDNS está no ar

void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap);
  ap_on = true;
}

const char* hostName() { return config::v.host[0] ? config::v.host : ap; }

void handleState() {
  JsonDocument d;
  d["board"] = app::BOARD;
  d["fw"] = app::FIRMWARE;
  d["host"] = hostName();
  d["ip"] = address();
  if (online()) d["ssid"] = WiFi.SSID(), d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["up"] = millis() / 1000;
  d["frame"] = app::lastFrameMs ? (long)((millis() - app::lastFrameMs) / 1000) : -1;
  d["daemon"] = daemonMs ? (long)((millis() - daemonMs) / 1000) : -1;
  d["ap"] = ap_on;
  d["safe"] = app::safe;
  JsonObject c = d["cfg"].to<JsonObject>();
  const auto& v = config::v;
  c["bright"] = v.bright, c["night"] = v.night, c["theme"] = v.theme, c["rotation"] = v.rotation;
  c["interval"] = v.interval, c["pages"] = v.pages, c["host"] = hostName(), c["hideCost"] = v.hideCost, c["alerts"] = v.alerts;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

// Campos ausentes ficam como estão; fora da faixa, são cortados para a faixa.
void handleConfig() {
  auto& v = config::v;
  auto num = [](const char* k, int lo, int hi, uint8_t& field) {
    if (server.hasArg(k)) field = constrain(server.arg(k).toInt(), lo, hi);
  };
  if (server.hasArg("host")) {
    String h = server.arg("host");
    h.trim();
    h.toLowerCase();
    if (h == ap) h = "";  // o nome padrão não precisa ficar gravado
    if (h.length() && !config::validHost(h))
      return server.send(400, "text/plain", "Nome inválido: use letras minúsculas, números e hífen (até 31).");
    strlcpy(v.host, h.c_str(), sizeof v.host);
  }
  num("bright", 10, 100, v.bright);
  num("night", 0, 1, v.night);
  num("theme", 0, 2, v.theme);
  num("rotation", 0, 3, v.rotation);
  num("interval", 0, 120, v.interval);
  num("pages", 0, 63, v.pages);
  num("alerts", 0, 3, v.alerts);
  num("hideCost", 0, 1, v.hideCost);
  config::save();
  server.send(200, "application/json", String("{\"host\":\"") + hostName() + "\"}");
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleWifi() {
  if (!ap_on) return server.send(403, "text/plain", "só pelo AP de configuração\n");
  server.send(200, "text/plain", "conectando; em ~20 s a tela mostra o IP na rede\n");
  delay(200);
  WiFi.begin(server.arg("ssid"), server.arg("pass"));  // persistente: fica gravada para os próximos boots
  startMs = millis();
}

void handleHello() {
  daemonMs = millis();
  char buf[160];
  snprintf(buf, sizeof buf, "{\"board\":\"%s\",\"fw\":\"%s\",\"ip\":\"%s\",\"heap\":%u,\"safe\":%s}", app::BOARD,
           app::FIRMWARE, WiFi.localIP().toString().c_str(), ESP.getFreeHeap(), app::safe ? "true" : "false");
  server.send(200, "application/json", buf);
}

void handleFrame() {
  daemonMs = millis();
  // parse num documento à parte: um corpo cortado não apaga o frame que está na tela
  JsonDocument next;
  if (DeserializationError err = deserializeJson(next, server.arg("plain"))) {
    return server.send(400, "text/plain", String("frame inválido: ") + err.c_str() + "\n");
  }
  app::frame = std::move(next);
  app::lastFrameMs = millis();
  fresh = true;
  server.send(204);
}

}  // namespace

void begin() {
  snprintf(ap, sizeof ap, "tokenpulse-%06x", ESP.getChipId());
  WiFi.persistent(true);
  WiFi.hostname(hostName());
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  // a rede que o firmware anterior gravou na flash do SDK; sem nenhuma, vai direto para o AP
  if (WiFi.SSID().length()) WiFi.begin();
  else startAp();
  startMs = millis();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_POST, handleConfig);
  // screenshot: 240x240 RGB565 cru. Mostra os seus dados a quem estiver na rede, como a própria tela
  server.on("/shot", HTTP_GET, [] {
    if (app::safe) return server.send(503, "text/plain", "modo de recuperação: sem tela\n");
    server.setContentLength(240 * 240 * 2);
    server.send(200, "application/octet-stream", "");
    if (!screen::shot(server.arg("page"), [](const uint8_t* d, size_t n) { server.sendContent(reinterpret_cast<const char*>(d), n); }))
      server.client().stop();  // tela pedida não existe: corta a resposta, o cliente vê o tamanho errado
  });
  server.on("/api/reboot", HTTP_POST, [] { server.send(204); rebootAtMs = millis() + 500; });
  server.on("/api/forget", HTTP_POST, [] {  // apaga a rede gravada: no próximo boot a placa abre o AP
    server.send(204);
    WiFi.disconnect(true);
    rebootAtMs = millis() + 500;
  });
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/hello", HTTP_GET, handleHello);
  server.on("/frame", HTTP_POST, handleFrame);
  updater.setup(&server, "/update");
  server.begin();
}

void loop() {
  server.handleClient();
  if (online()) {
    if (!onlineSinceMs) onlineSinceMs = millis();
    if (mdns_on && mdnsHost != hostName()) MDNS.end(), mdns_on = false;  // renomeada na página
    if (!mdns_on) {
      mdns_on = MDNS.begin(hostName());
      if (mdns_on) {
        mdnsHost = hostName();
        MDNS.addService("tokenpulse", "tcp", 80);
        MDNS.addServiceTxt("tokenpulse", "tcp", "board", app::BOARD);
      }
    }
    if (ap_on && millis() - onlineSinceMs > 60000) {  // conectou e ficou: o AP não é mais preciso
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      ap_on = false;
    }
  } else {
    onlineSinceMs = 0;
    if (!ap_on && millis() - startMs > JOIN_MS) startAp();
  }
  if (mdns_on) MDNS.update();
  if (rebootAtMs && millis() > rebootAtMs) ESP.restart();
}

bool online() { return WiFi.status() == WL_CONNECTED; }
bool apOn() { return ap_on; }
const char* apName() { return ap; }
String address() { return online() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
uint32_t lastDaemonMs() { return daemonMs; }

bool takeFrame() {
  const bool f = fresh;
  fresh = false;
  return f;
}

}  // namespace net
