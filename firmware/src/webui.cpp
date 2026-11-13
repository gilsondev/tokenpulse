#include "webui.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "boards/board.h"
#include "hostlink.h"
#include "settings.h"
#include "sound.h"
#include "ui/anim.h"
#include "ui/gfx.h"
#include "web/assets.h"
#include "web/usb_page.h"

namespace webui {
namespace {

WebServer server(80);
HTTPUpdateServer updater;
DNSServer dns;  // portal cativo: no WiFi da placa, todo nome responde 192.168.4.1
char ap[24];
bool started = false, ap_on = false, mdns_on = false;
uint32_t onlineSinceMs = 0, rebootAtMs = 0, joinMs = 0;
String scanJson, joinSsid, mdnsHost;
uint8_t lastReason = 0;  // motivo da última desconexão do WiFi: senha errada, rede não achada…

void startServer();

void startAp() {
  const bool mode = WiFi.mode(WIFI_AP_STA);
  const bool up = WiFi.softAP(ap);
  Serial.printf("wifi: AP %s modo=%d softAP=%d ip=%s heap=%u\n", ap, mode, up, WiFi.softAPIP().toString().c_str(), ESP.getFreeHeap());
  dns.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);  // a lista já fica pronta quando a pessoa abrir a página
  ap_on = true;
  startServer();
}

void handleState() {
  JsonDocument d;
  d["board"] = board::NAME;
  d["battery"] = board::PROFILE.battery;
  d["nick"] = board::NICK;
  d["fw"] = hostlink::firmware();
  d["agent"] = app::frame["agv"] | "";
  d["host"] = host();
  d["ip"] = address();
  if (online()) d["ssid"] = WiFi.SSID(), d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["ota"] = ESP.getFreeSketchSpace();
  d["up"] = millis() / 1000;
  d["frame"] = app::lastFrameMs ? (long)((millis() - app::lastFrameMs) / 1000) : -1;
  d["ap"] = ap_on;
  d["apName"] = ap;
  d["imu"] = board::PROFILE.imu;  // a tela gira sozinha: sem escolha de em pé ou deitada
  d["rotations"] = board::ROTATIONS;
  d["speaker"] = board::PROFILE.speaker;
  d["oled"] = board::PROFILE.oled;
  if (board::PROFILE.speaker)
    for (const char* s : sound::NAMES) d["sounds"].add(s);
  d["cxok"] = app::frame["cxok"] | 0;  // o Mac tem o Codex: a página mostra o ajuste Provedores
  d["prov"] = app::frame["prov"] | "auto";
  if (!app::frame["pask"].isNull()) d["pask"] = app::frame["pask"];  // primeiro uso: a página destaca a escolha
  if (!app::frame["mcs"].isNull()) d["mcs"] = app::frame["mcs"];
  if (!app::frame["upd"].isNull()) d["upd"] = app::frame["upd"];  // versão nova no GitHub: a página oferece atualizar
  JsonObject c = d["cfg"].to<JsonObject>();
  const auto& v = settings::v;
  const auto& w = settings::web;
  c["bright"] = v.bright, c["batBright"] = w.batBright, c["idle"] = v.idle, c["theme"] = v.theme, c["orient"] = v.orient, c["hideCost"] = v.hideCost;
  c["interval"] = w.interval, c["pages"] = w.pages, c["alerts"] = w.alerts, c["sound"] = w.sound, c["volume"] = w.volume, c["restSecs"] = w.restSecs, c["restPages"] = w.restPages, c["soundOut"] = w.soundOut, c["soundNew"] = w.soundNew, c["soundWeek"] = w.soundWeek, c["soundBat"] = w.soundBat, c["soundMac"] = w.soundMac, c["soundBle"] = w.soundBle, c["host"] = host();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

// Campos ausentes ficam como estão; fora da faixa, são cortados para a faixa.
void handleConfig() {
  auto& v = settings::v;
  auto& w = settings::web;
  auto num = [](const char* k, int lo, int hi, uint8_t& field) {
    if (server.hasArg(k)) field = constrain(server.arg(k).toInt(), lo, hi);
  };
  // Provedores mora no daemon: a placa só repassa o pedido, da lista fechada
  if (server.hasArg("prov")) {
    const String m = server.arg("prov");
    if (m != "auto" && m != "claude" && m != "codex" && m != "both") return server.send(400, "text/plain", "Provedor inválido.");
    hostlink::action(("ACTION prov " + m).c_str());
  }
  if (server.hasArg("update")) hostlink::action("ACTION update");
  for (const char* k : {"mc", "mcws"})  // o ajuste Multica também mora no daemon
    if (server.hasArg(k)) hostlink::action((String("ACTION ") + k + " " + constrain(server.arg(k).toInt(), 0, 4)).c_str());
  if (server.hasArg("host")) {
    String h = server.arg("host");
    h.trim();
    h.toLowerCase();
    if (h == ap) h = "";  // o nome padrão não precisa ficar gravado
    if (h.length() && !settings::validHost(h))
      return server.send(400, "text/plain", "Nome inválido: use letras minúsculas, números e hífen (até 31).");
    strlcpy(w.host, h.c_str(), sizeof w.host);
  }
  const uint8_t orient = v.orient;
  num("bright", 0, 3, v.bright);
  num("batBright", 0, 3, w.batBright);
  num("idle", 0, 3, v.idle);
  num("theme", 0, 2, v.theme);
  num("orient", 0, board::ROTATIONS - 1, v.orient);
  num("hideCost", 0, 1, v.hideCost);
  num("interval", 0, 120, w.interval);
  num("pages", 0, 7, w.pages);
  num("alerts", 0, 3, w.alerts);
  num("sound", 0, sound::COUNT - 1, w.sound);
  num("volume", 1, 3, w.volume);
  num("restSecs", 0, 1, w.restSecs);
  num("restPages", 1, 31, w.restPages);
  num("soundOut", 0, sound::COUNT, w.soundOut);
  num("soundNew", 0, sound::COUNT, w.soundNew);
  num("soundWeek", 0, sound::COUNT, w.soundWeek);
  num("soundBat", 0, sound::COUNT, w.soundBat);
  num("soundMac", 0, sound::COUNT, w.soundMac);
  num("soundBle", 0, sound::COUNT, w.soundBle);
  settings::save();
  settings::saveWeb();
  if (v.orient != orient) gfx::setRotation(v.orient), anim::reset();
  server.send(200, "application/json", String("{\"host\":\"") + host() + "\"}");
}

// Redes que a placa enxerga, sem repetir SSID (fica o sinal mais forte), da mais forte para a mais fraca.
void handleScan() {
  if (server.hasArg("again")) scanJson = "", WiFi.scanDelete(), WiFi.scanNetworks(true);
  if (scanJson.length()) return server.send(200, "application/json", scanJson);
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true), n = WIFI_SCAN_RUNNING;
  if (n == WIFI_SCAN_RUNNING) return server.send(200, "application/json", "{\"state\":\"scanning\"}");
  int order[32], m = 0;
  for (int i = 0; i < n && m < 32; i++) {
    if (WiFi.SSID(i).isEmpty()) continue;
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
    r.add(WiFi.SSID(order[k])), r.add(WiFi.RSSI(order[k])), r.add(WiFi.encryptionType(order[k]) == WIFI_AUTH_OPEN);
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
  lastReason = 0;
  WiFi.begin(joinSsid.c_str(), server.arg("pass").c_str());  // o ESP32 grava a rede na NVS
  joinMs = millis();
}

// Como vai a conexão pedida na página: connecting, connected (com o endereço) ou failed (com o motivo).
void handleWifiState() {
  JsonDocument d;
  const uint32_t t = millis() - joinMs;
  d["ssid"] = joinSsid;
  if (!joinMs) d["state"] = "idle";
  else if (online()) {
    d["state"] = "connected", d["ip"] = WiFi.localIP().toString(), d["host"] = host();
  } else if (lastReason == WIFI_REASON_AUTH_FAIL || lastReason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
    d["state"] = "failed", d["why"] = "senha";
  else if (lastReason == WIFI_REASON_NO_AP_FOUND && t > 8000) d["state"] = "failed", d["why"] = "rede";
  else if (t > 25000) d["state"] = "failed", d["why"] = "tempo";
  else d["state"] = "connecting", d["secs"] = t / 1000;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void forgetWifi() {
  settings::web.wifi = 0;
  settings::saveWeb();
  WiFi.disconnect(true, true);  // apaga a rede gravada
}

void startServer() {
  if (started) return;
  started = true;
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", ap_on && !online() ? WIFI_PAGE : USB_PAGE); });
  server.on("/config", HTTP_GET, [] { server.send_P(200, "text/html", USB_PAGE); });
  server.on("/wifi", HTTP_GET, [] { server.send_P(200, "text/html", WIFI_PAGE); });
  server.on("/style.css", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-cache");  // o CSS muda com o firmware: o navegador confere a cada visita
    server.send_P(200, "text/css", STYLE);
  });
  server.on("/favicon.ico", HTTP_GET, [] {  // pedido pelos navegadores por conta própria
    server.sendHeader("Location", "/logo.svg");
    server.send(301, "text/plain", "");
  });
  server.on("/logo.svg", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "image/svg+xml", LOGO);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/play", HTTP_POST, [] {  // prévia do som escolhido
    const int i = server.arg("i").toInt();
    if (i < 0 || i >= sound::COUNT) return server.send(400);
    board::play(i, settings::volumePct());
    server.send(204);
  });
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_GET, handleWifiState);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/api/reboot", HTTP_POST, [] { server.send(204); rebootAtMs = millis() + 500; });
  server.on("/api/forget", HTTP_POST, [] {  // esquece a rede: o WiFi só volta quando pedido na tela
    server.send(204);
    forgetWifi();
    rebootAtMs = millis() + 500;
  });
  // entrega para outra pessoa: ajustes de fábrica e sem a rede daqui. A calibração do toque fica: é da placa
  server.on("/api/prepare", HTTP_POST, [] {
    server.send(204);
    const uint8_t orient = settings::v.orient;  // a posição é do suporte, como a calibração: fica
    settings::v = settings::Values();
    settings::v.orient = orient;
    settings::save();
    settings::web = settings::Web();
    forgetWifi();
    rebootAtMs = millis() + 500;
  });
  // a página guiada vem antes do updater: o WebServer usa a primeira rota que bate, e o POST é o dele
  server.on("/update", HTTP_GET, [] { server.send_P(200, "text/html", UPDATE_PAGE); });
  updater.setup(&server, "/update");
  static const char* keep[] = {"Content-Length"};  // o ESP32 pede a lista, o ESP8266 aceita variádico
  server.collectHeaders(keep, 1);
  Update.onProgress([](size_t done, size_t) {
    if (app::serialOta) return;  // OTA pela USB: a tela de progresso é a do hostlink
    static int lastPct = -1;
    const size_t total = max<size_t>(server.header("Content-Length").toInt(), 1);
    const int pct = done * 100 / total;
    if (pct == lastPct) return;  // a tela só muda de 1 em 1%: o resto do tempo vai para a gravação
    lastPct = pct;
    char status[48];
    snprintf(status, sizeof status, "%u de %u KB", done / 1024, total / 1024);
    gfx::loading(gfx::DOWNLOAD, "atualizando firmware", status, (float)done / total);
  });
  server.onNotFound([] {  // portal cativo: o teste de internet do celular cai na página
    if (!ap_on) return server.send(404, "text/plain", "não existe\n");
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

}  // namespace

void begin() {
  snprintf(ap, sizeof ap, "tokenpulse-%s", board::CODE);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (!settings::web.wifi) return;  // nunca configurado: o WiFi fica desligado
  WiFi.setHostname(host());
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // a rede que a página gravou
  startServer();
}

// Outra placa do mesmo modelo já responde por tokenpulse-<codinome>.local: esta grava o nome com uma cor, fácil
// de digitar e sempre a mesma para este chip (tokenpulse-sumauma-azul). Só na primeira vez, com o nome padrão.
void pickHost() {
  static const char* const COLORS[] = {"azul", "verde", "rosa", "roxo", "cinza", "ouro", "prata", "coral"};
  char tmp[32], name[32];
  snprintf(tmp, sizeof tmp, "tokenpulse-%06x", (uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFFFF);
  if (!MDNS.begin(tmp)) return;
  const uint32_t seed = (uint32_t)(ESP.getEfuseMac() >> 24);
  const IPAddress me = WiFi.localIP();
  const IPAddress owner = MDNS.queryHost(ap, 1500);
  bool taken = (uint32_t)owner && owner != me;
  for (int k = 0; taken && k < 8; k++) {
    snprintf(name, sizeof name, "%s-%s", ap, COLORS[(seed + k) % 8]);
    const IPAddress ip = MDNS.queryHost(name, 1500);
    if (!(uint32_t)ip || ip == me) {
      strlcpy(settings::web.host, name, sizeof settings::web.host);
      settings::saveWeb();
      taken = false;
    }
  }
  MDNS.end();
}

void loop() {
  if (!started) return;
  server.handleClient();
  if (ap_on) dns.processNextRequest();
  if (online()) {
    if (!onlineSinceMs) onlineSinceMs = millis();
    if (!settings::web.wifi) settings::web.wifi = 1, settings::saveWeb();  // conectou pela página: liga ao ligar
    if (mdns_on && mdnsHost != host()) MDNS.end(), mdns_on = false;   // renomeada na página
    if (!mdns_on && !settings::web.host[0]) pickHost();
    if (!mdns_on && (mdns_on = MDNS.begin(host()))) {
      mdnsHost = host();
      MDNS.addService("http", "tcp", 80);  // _tokenpulse._tcp não: o daemon acharia que os dados vão pelo WiFi
    }
    if (ap_on && millis() - onlineSinceMs > 60000) {  // conectou e ficou: o WiFi da placa não é mais preciso
      dns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      ap_on = false;
    }
  } else {
    onlineSinceMs = 0;
  }
  if (rebootAtMs && millis() > rebootAtMs) ESP.restart();
}

// O WiFi que o daemon mandou pelo cabo (o do computador): entra como a página de configuração faria. A senha não
// volta para a serial nem para o log.
void join(const char* ssid, const char* pass) {
  joinSsid = ssid;
  lastReason = 0;
  if (!ap_on) WiFi.mode(WIFI_STA);
  WiFi.setHostname(host());
  WiFi.begin(ssid, pass);  // o ESP32 grava a rede na NVS; ao conectar, o loop liga o WiFi no boot
  joinMs = millis();
  startServer();
}

// Como vai a conexão pedida (pelo cabo ou pela página), numa linha para a tela; vazio se nada foi pedido.
String joinStatus() {
  if (!joinMs) return "";
  if (online()) return "Conectada em " + joinSsid;
  if (lastReason == WIFI_REASON_AUTH_FAIL || lastReason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) return "Senha errada para " + joinSsid;
  const uint32_t t = millis() - joinMs;
  if (lastReason == WIFI_REASON_NO_AP_FOUND && t > 8000) return joinSsid + " não aparece: use uma rede 2,4 GHz";
  if (t > 25000) return "Não conectou em " + joinSsid;
  return "Conectando em " + joinSsid;
}

void openAp() {
  resume();
  if (!ap_on) startAp();
}

bool paused = false;

void pause() {
  if (!started || ap_on || (joinMs && !online() && millis() - joinMs < 30000)) return;  // entrando numa rede: não corta no meio
  if (mdns_on) MDNS.end(), mdns_on = false;
  WiFi.mode(WIFI_OFF);
  paused = true;
}

void resume() {
  if (!paused) return;
  paused = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // a rede salva
}

bool online() { return started && WiFi.status() == WL_CONNECTED; }
bool apOn() { return ap_on; }
const char* apName() { return ap; }
const char* host() { return settings::web.host[0] ? settings::web.host : ap; }
String address() { return online() ? WiFi.localIP().toString() : ap_on ? WiFi.softAPIP().toString() : String(); }

}  // namespace webui
