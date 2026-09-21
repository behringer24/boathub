#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "net.h"
#include "uplink.h"

namespace {

WebServer server(80);
DNSServer dns;

// The portal changes where telemetry goes and what the access point password
// is, and it asks for no credentials of its own. Reachable from the station
// side that would mean anyone on the marina network could point the boat at
// their own broker or lock us out of BOOT-NETZ. So: access point only.
bool fromAccessPoint() {
  const IPAddress peer = server.client().remoteIP();
  const IPAddress ap = WiFi.softAPIP();
  const IPAddress mask = WiFi.softAPSubnetMask();
  for (int i = 0; i < 4; i++) {
    if ((peer[i] & mask[i]) != (ap[i] & mask[i])) return false;
  }
  return true;
}

// Returns false when the request has already been answered with a refusal.
bool allowed() {
  if (fromAccessPoint()) return true;
  Serial.printf("[portal] refused %s - not on the access point\n",
                server.client().remoteIP().toString().c_str());
  server.send(403, "text/plain",
              "Configuration is only available on the BOOT-NETZ access point.\n");
  return false;
}

String esc(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

String field(const char *name, const char *label, const String &value, const char *type = "text",
             const char *hint = "") {
  String h = "<label>";
  h += label;
  h += "<input name=\"";
  h += name;
  h += "\" type=\"";
  h += type;
  h += "\" value=\"";
  h += esc(value);
  h += "\"";
  if (hint[0]) {
    h += " placeholder=\"";
    h += hint;
    h += "\"";
  }
  h += "></label>";
  return h;
}

void handleRoot() {
  if (!allowed()) return;
  const Config &c = config::get();

  String h = F("<!doctype html><meta charset=utf-8>"
               "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
               "<title>BoatHub</title><style>"
               "body{font:16px system-ui,sans-serif;margin:0;padding:16px;background:#f6f6f4;color:#1a1a19}"
               "main{max-width:26rem;margin:0 auto}"
               "h1{font-size:1.3rem;margin:0 0 .25rem}"
               "p.s{color:#5c5c57;margin:0 0 1.25rem}"
               "fieldset{border:1px solid #d9d9d4;border-radius:8px;margin:0 0 1rem;padding:.75rem 1rem 1rem;background:#fff}"
               "legend{padding:0 .35rem;color:#5c5c57;font-size:.85rem}"
               "label{display:block;margin:.6rem 0}"
               "input{width:100%;box-sizing:border-box;margin-top:.25rem;padding:.5rem;"
               "border:1px solid #c9c9c2;border-radius:6px;font:inherit}"
               "button{width:100%;padding:.7rem;border:0;border-radius:6px;background:#1a1a19;"
               "color:#fff;font:inherit;font-weight:600}"
               "</style><main><h1>BoatHub</h1><p class=s>");
  h += net::stateName();
  h += F(" &middot; station ");
  h += esc(net::stationIp());
  h += F(" &middot; broker ");
  h += uplink::connected() ? F("connected") : F("not connected");
  h += F("</p><form method=post action=/save>");

  h += F("<fieldset><legend>Identity</legend>");
  h += field("boat_id", "Boat id (appears in the MQTT topic)", c.boatId);
  h += F("</fieldset><fieldset><legend>Wi-Fi to join</legend>");
  h += field("wifi_ssid", "Network name", c.wifiSsid);
  h += field("wifi_pass", "Password", "", "password", "unchanged");
  h += F("</fieldset><fieldset><legend>MQTT broker</legend>");
  h += field("mqtt_host", "Host or IP address", c.mqttHost, "text", "192.168.1.10");
  h += field("mqtt_port", "Port", String(c.mqttPort), "number");
  // A checkbox rather than a field(): an unticked box sends nothing at all, so
  // the parser reads its absence as false instead of needing a value.
  h += F("<label><input type=checkbox name=mqtt_tls value=1");
  if (c.mqttTls) h += F(" checked");
  h += F("> TLS (port 8883). Needed whenever the broker is reached over the "
         "internet - without it the password below crosses it in the clear.</label>");
  h += field("mqtt_user", "User", c.mqttUser);
  h += field("mqtt_pass", "Password", "", "password", "unchanged");
  h += field("smpl_secs", "Measure every ... seconds", String(c.sampleSecs), "number");
  h += field("pub_secs", "Publish every ... seconds", String(c.pubSecs), "number");
  h += F("</fieldset><fieldset><legend>This access point</legend>");
  h += field("ap_pass", "BOOT-NETZ password", "", "password", "unchanged, min. 8 characters");
  h += F("</fieldset><button>Save and restart</button></form></main>");

  server.send(200, "text/html", h);
}

void handleSave() {
  if (!allowed()) return;
  Config in = config::get();

  if (server.hasArg("boat_id")) in.boatId = server.arg("boat_id");
  if (server.hasArg("wifi_ssid")) in.wifiSsid = server.arg("wifi_ssid");
  if (server.hasArg("mqtt_host")) in.mqttHost = server.arg("mqtt_host");
  if (server.hasArg("mqtt_user")) in.mqttUser = server.arg("mqtt_user");
  if (server.hasArg("mqtt_port")) in.mqttPort = server.arg("mqtt_port").toInt();
  // Unticked boxes are not submitted, so presence is the value.
  in.mqttTls = server.hasArg("mqtt_tls");
  if (server.hasArg("smpl_secs")) in.sampleSecs = server.arg("smpl_secs").toInt();
  if (server.hasArg("pub_secs")) in.pubSecs = server.arg("pub_secs").toInt();

  in.wifiPass = server.arg("wifi_pass");
  in.mqttPass = server.arg("mqtt_pass");
  in.apPass = server.arg("ap_pass");

  // WPA2 will not take anything shorter, and a rejected softAP() would leave
  // the board with no way back in.
  if (in.apPass.length() > 0 && in.apPass.length() < 8) {
    server.send(400, "text/html",
                F("<!doctype html><meta charset=utf-8><p>The access point password needs at least "
                  "8 characters. <a href=/>Back</a>"));
    return;
  }
  if (in.mqttPort == 0) in.mqttPort = in.mqttTls ? 8883 : 1883;
  if (in.sampleSecs == 0) in.sampleSecs = 10;
  if (in.pubSecs == 0) in.pubSecs = 300;
  // A window shorter than a sample would close before anything went into it.
  if (in.pubSecs < in.sampleSecs) in.pubSecs = in.sampleSecs;

  config::save(in);

  server.send(200, "text/html",
              F("<!doctype html><meta charset=utf-8>"
                "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                "<body style=\"font:16px system-ui,sans-serif;padding:16px\">"
                "<p>Saved. The board is restarting."));
  delay(250);  // let the response reach the browser before the reset
  ESP.restart();
}

void handleStatus() {
  if (!allowed()) return;
  const Config &c = config::get();
  String j = "{\"state\":\"";
  j += net::stateName();
  j += "\",\"boat_id\":\"";
  j += c.boatId;
  j += "\",\"station_ip\":\"";
  j += net::stationIp();
  j += "\",\"rssi_dbm\":";
  j += net::rssi();
  j += ",\"broker\":\"";
  j += uplink::statusText();
  j += "\",\"uptime_s\":";
  j += millis() / 1000;
  j += "}";
  server.send(200, "application/json", j);
}

}  // namespace

namespace portal {

void begin() {
  // Answer every name with our own address, so a phone offers the page itself.
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() {
    if (!allowed()) return;
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  Serial.printf("[portal] http://%s\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
}

}  // namespace portal
