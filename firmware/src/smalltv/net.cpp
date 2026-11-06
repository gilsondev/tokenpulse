#include "net.h"

#include <DNSServer.h>
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
DNSServer dns;  // portal cativo: no AP, todo nome responde 192.168.4.1 e o celular abre a página sozinho
char ap[24];
bool ap_on = false, mdns_on = false, fresh = false;
uint32_t startMs = 0, onlineSinceMs = 0, daemonMs = 0, rebootAtMs = 0;
String mdnsHost;  // o nome com que o mDNS está no ar
String scanJson;  // última busca de redes, pronta para a página: [[ssid, rssi, aberta], ...]
String joinSsid;  // a rede escolhida na página de configuração do WiFi
uint32_t joinMs = 0;

void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap);
  dns.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);  // a lista já fica pronta quando a pessoa abrir a página
  ap_on = true;
}

const char* hostName() { return config::v.host[0] ? config::v.host : ap; }

void handleState() {
  JsonDocument d;
  d["board"] = app::BOARD;
  d["fw"] = app::FIRMWARE;
  d["agent"] = app::frame["agv"] | "";  // versão do tokend, vinda no frame
  d["host"] = hostName();
  d["ip"] = address();
  if (online()) d["ssid"] = WiFi.SSID(), d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["ota"] = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;  // o maior .bin que o /update aceita
  d["up"] = millis() / 1000;
  d["frame"] = app::lastFrameMs ? (long)((millis() - app::lastFrameMs) / 1000) : -1;
  d["daemon"] = daemonMs ? (long)((millis() - daemonMs) / 1000) : -1;
  d["ap"] = ap_on;
  d["apName"] = ap;
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

// No AP e ainda sem rede, a raiz é a configuração do WiFi; os ajustes ficam em /config.
void handleRoot() { server.send_P(200, "text/html", ap_on && !online() ? WIFI_PAGE : PAGE); }

// Redes que a placa enxerga, sem repetir SSID (fica o sinal mais forte), da mais forte para a mais fraca.
// A busca roda em segundo plano: a página pergunta de novo até vir "done".
void handleScan() {
  if (server.hasArg("again")) scanJson = "", WiFi.scanDelete(), WiFi.scanNetworks(true);
  if (scanJson.length()) return server.send(200, "application/json", scanJson);
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true), n = WIFI_SCAN_RUNNING;
  if (n == WIFI_SCAN_RUNNING) return server.send(200, "application/json", "{\"state\":\"scanning\"}");
  int order[32], m = 0;
  for (int i = 0; i < n && m < 32; i++) {
    if (WiFi.SSID(i).isEmpty()) continue;  // rede oculta: só pelo "minha rede não aparece"
    int k = 0;
    while (k < m && WiFi.SSID(order[k]) != WiFi.SSID(i)) k++;
    if (k < m) {
      if (WiFi.RSSI(i) > WiFi.RSSI(order[k])) order[k] = i;
      continue;
    }
    order[m++] = i;
  }
  std::sort(order, order + m, [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  JsonDocument d;
  d["state"] = "done";
  JsonArray nets = d["nets"].to<JsonArray>();
  for (int k = 0; k < m; k++) {
    JsonArray r = nets.add<JsonArray>();
    r.add(WiFi.SSID(order[k])), r.add(WiFi.RSSI(order[k])), r.add(WiFi.encryptionType(order[k]) == ENC_TYPE_NONE);
  }
  serializeJson(d, scanJson);
  WiFi.scanDelete();
  server.send(200, "application/json", scanJson);
}

void handleWifi() {
  if (!ap_on) return server.send(403, "text/plain", "só pelo WiFi de configuração da placa\n");
  joinSsid = server.arg("ssid");
  if (joinSsid.isEmpty()) return server.send(400, "text/plain", "escolha uma rede\n");
  server.send(202, "application/json", "{\"ok\":true}");
  delay(100);
  WiFi.begin(joinSsid, server.arg("pass"));  // persistente: fica gravada para os próximos boots
  joinMs = startMs = millis();
}

// Como vai a conexão pedida na página: connecting, connected (com o endereço) ou failed (com o motivo).
void handleWifiState() {
  JsonDocument d;
  const wl_status_t st = WiFi.status();
  const uint32_t t = millis() - joinMs;
  d["ssid"] = joinSsid;
  if (!joinMs) d["state"] = "idle";
  else if (st == WL_CONNECTED) {
    d["state"] = "connected", d["ip"] = WiFi.localIP().toString(), d["host"] = hostName();
  } else if (st == WL_WRONG_PASSWORD) d["state"] = "failed", d["why"] = "senha";
  else if (st == WL_NO_SSID_AVAIL && t > 8000) d["state"] = "failed", d["why"] = "rede";
  else if (t > 25000) d["state"] = "failed", d["why"] = "tempo";
  else d["state"] = "connecting", d["secs"] = t / 1000;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
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
  server.on("/config", HTTP_GET, [] { server.send_P(200, "text/html", PAGE); });
  server.on("/wifi", HTTP_GET, [] { server.send_P(200, "text/html", WIFI_PAGE); });
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_GET, handleWifiState);
  // portal cativo: o teste de internet do celular (captive.apple.com, /generate_204…) cai aqui e é
  // mandado para a página; fora do AP, endereço desconhecido é 404
  server.onNotFound([] {
    if (!ap_on) return server.send(404, "text/plain", "não existe\n");
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
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
  // entrega para outra pessoa: ajustes de fábrica e sem a rede daqui. No próximo boot a placa abre o AP, e
  // depois do WiFi dela a tela mostra o QR code da instalação no Mac
  server.on("/api/prepare", HTTP_POST, [] {
    server.send(204);
    config::v = config::Values();
    config::save();
    WiFi.disconnect(true);
    rebootAtMs = millis() + 500;
  });
  server.on("/api/forget", HTTP_POST, [] {  // apaga a rede gravada: no próximo boot a placa abre o AP
    server.send(204);
    WiFi.disconnect(true);
    rebootAtMs = millis() + 500;
  });
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/hello", HTTP_GET, handleHello);
  server.on("/frame", HTTP_POST, handleFrame);
  server.on("/favicon.ico", HTTP_GET, [] {  // pedido pelos navegadores por conta própria
    server.sendHeader("Location", "/logo.svg");
    server.send(301, "text/plain", "");
  });
  server.on("/logo.svg", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "image/svg+xml", LOGO);
  });
  server.on("/style.css", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "max-age=3600");
    server.send_P(200, "text/css", STYLE);
  });
  // a página guiada vem antes do updater: o ESP8266WebServer usa a primeira rota que bate, e o POST
  // /update continua sendo o dele (o mesmo que o tokend usa)
  server.on("/update", HTTP_GET, [] { server.send_P(200, "text/html", UPDATE_PAGE); });
  updater.setup(&server, "/update");
  // o total vem do Content-Length do envio: o Updater só sabe o espaço livre, não o tamanho do arquivo
  server.collectHeaders("Content-Length");
  if (!app::safe) Update.onProgress([](size_t done, size_t) { screen::otaProgress(done, server.header("Content-Length").toInt()); });
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
      dns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      ap_on = false;
    }
  } else {
    onlineSinceMs = 0;
    if (!ap_on && millis() - startMs > JOIN_MS) startAp();
  }
  if (mdns_on) MDNS.update();
  if (ap_on) dns.processNextRequest();
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
