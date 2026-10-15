#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "barometer.h"
#include "config.h"
#include "imu.h"
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

// `extra` is raw attributes, for the odd field that needs one - a number with
// decimals, say, which a browser otherwise refuses as "not a valid value".
String field(const char *name, const char *label, const String &value, const char *type = "text",
             const char *hint = "", const char *extra = "") {
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
  if (extra[0]) {
    h += " ";
    h += extra;
  }
  h += "></label>";
  return h;
}

// The six ways a module axis can point, in the order of the axis codes in
// config.h.
const char *AXES[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

String axisChoice(const char *name, const char *label, uint16_t value) {
  String h = "<label>";
  h += label;
  h += "<select name=\"";
  h += name;
  h += "\">";
  for (uint16_t i = 0; i < 6; i++) {
    h += "<option value=\"";
    h += i;
    h += "\"";
    if (i == value) h += " selected";
    h += ">";
    h += AXES[i];
    h += "</option>";
  }
  h += "</select></label>";
  return h;
}

// What the motion sensor is doing, in one line above its settings.
String imuSummary() {
  String h = "<p class=s>";
  h += imu::statusText();
  const imu::Reading r = imu::latest();
  if (r.valid && r.level) {
    char buf[64];
    snprintf(buf, sizeof(buf), " &middot; heel %+.1f&deg; pitch %+.1f&deg;", r.heelDeg, r.pitchDeg);
    h += buf;
  }
  h += "</p>";
  return h;
}

// The same for the barometer: what it reads now, at a glance.
String barometerSummary() {
  String h = "<p class=s>";
  h += barometer::statusText();
  const barometer::Reading r = barometer::latest();
  if (r.valid) {
    char buf[96];
    snprintf(buf, sizeof(buf), " &middot; %.1f hPa &middot; bay %.1f&deg;C %.0f%%", r.pressureHpa,
             r.tempC, r.rh);
    h += buf;
  }
  h += "</p>";
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
               "input,select{width:100%;box-sizing:border-box;margin-top:.25rem;padding:.5rem;"
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
  h += F("</fieldset><fieldset><legend>Motion sensor</legend>");
  h += imuSummary();
  h += axisChoice("imu_fwd", "Module axis pointing forward", c.imuForward);
  h += axisChoice("imu_up", "Module axis pointing up", c.imuUp);
  h += field("imu_tau", "Filter time constant, seconds", String(c.imuTauDs / 10.0f, 1), "number",
             "", "step=0.1");
  h += field("imu_knock", "Knock: a jump of ... g between samples (steps of 0.125)",
             String(c.imuKnockMg / 1000.0f, 3), "number", "", "step=0.125");
  h += field("imu_hold", "Hold-off after a knock, seconds", String(c.imuHoldoffSecs), "number");
  h += F("</fieldset><fieldset><legend>Barometer</legend>");
  h += barometerSummary();
  h += field("bme_p_off", "Pressure offset, Pa (station minus board, both at sea level)",
             String(c.bmePressureOffsetPa), "number", "", "step=1");
  h += F("<label><input type=checkbox name=bme_gas value=1");
  if (c.bmeGas) h += F(" checked");
  h += F("> Gas heater and gas measurement. Leave it off in the battery bay: it heats a plate "
         "to 320&nbsp;&deg;C where a failing charger can vent hydrogen.</label>");
  h += F("</fieldset><fieldset><legend>This access point</legend>");
  h += field("ap_pass", "BOOT-NETZ password", "", "password", "unchanged, min. 8 characters");
  h += F("</fieldset><button>Save and restart</button></form>");

  // Its own form: an action on the running board, not a setting, so it neither
  // takes the rest of the page with it nor restarts anything.
  h += F("<form method=post action=/level><fieldset><legend>Zero reference</legend>"
         "<p class=s>Records the boat's present attitude as zero for heel and pitch. "
         "Only with the boat at rest and loaded as usual, and again whenever the box is "
         "remounted.</p><button>Level now</button></fieldset></form></main>");

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
  if (server.hasArg("imu_fwd")) in.imuForward = server.arg("imu_fwd").toInt();
  if (server.hasArg("imu_up")) in.imuUp = server.arg("imu_up").toInt();
  if (server.hasArg("imu_tau")) in.imuTauDs = lroundf(server.arg("imu_tau").toFloat() * 10.0f);
  if (server.hasArg("imu_knock")) in.imuKnockMg = lroundf(server.arg("imu_knock").toFloat() * 1000.0f);
  if (server.hasArg("imu_hold")) in.imuHoldoffSecs = server.arg("imu_hold").toInt();
  in.bmeGas = server.hasArg("bme_gas");
  long pressureOffset = in.bmePressureOffsetPa;
  if (server.hasArg("bme_p_off")) pressureOffset = server.arg("bme_p_off").toInt();

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
  // Forward and up along the same axis leave the third undefined - and the
  // board would otherwise fall back to a default nobody chose.
  if (in.imuForward > 5 || in.imuUp > 5 || (in.imuForward >> 1) == (in.imuUp >> 1)) {
    server.send(400, "text/html",
                F("<!doctype html><meta charset=utf-8><p>Forward and up have to be two different "
                  "axes of the module. <a href=/>Back</a>"));
    return;
  }
  // The offset corrects the sensor, whose absolute error is under a
  // hectopascal and drifts by one a year. Twenty is not a sensor error: it is
  // a station's sea-level figure compared with the board's station pressure,
  // which differ by 0.12 hPa for every metre of height - see A-010 section 11.
  if (pressureOffset < -2000 || pressureOffset > 2000) {
    server.send(400, "text/html",
                F("<!doctype html><meta charset=utf-8><p>A pressure offset beyond 2000 Pa (20 hPa) "
                  "is not a sensor error. Reduce the board's reading to sea level before comparing "
                  "it with the station's. <a href=/>Back</a>"));
    return;
  }
  in.bmePressureOffsetPa = (int16_t)pressureOffset;
  if (in.mqttPort == 0) in.mqttPort = in.mqttTls ? 8883 : 1883;
  if (in.sampleSecs == 0) in.sampleSecs = 10;
  if (in.pubSecs == 0) in.pubSecs = 300;
  // A window shorter than a sample would close before anything went into it.
  if (in.pubSecs < in.sampleSecs) in.pubSecs = in.sampleSecs;
  // Half a second follows the waves, a minute stops following the boat. The
  // knock range is what the chip can be set to; see A-009.
  if (in.imuTauDs < 5) in.imuTauDs = 5;
  if (in.imuTauDs > 600) in.imuTauDs = 600;
  if (in.imuKnockMg < 125) in.imuKnockMg = 125;
  if (in.imuKnockMg > 7875) in.imuKnockMg = 7875;
  if (in.imuHoldoffSecs == 0) in.imuHoldoffSecs = 1;

  config::save(in);

  server.send(200, "text/html",
              F("<!doctype html><meta charset=utf-8>"
                "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                "<body style=\"font:16px system-ui,sans-serif;padding:16px\">"
                "<p>Saved. The board is restarting."));
  delay(250);  // let the response reach the browser before the reset
  ESP.restart();
}

// "Level now". Answered with a page rather than a redirect, so that a refusal
// says why instead of quietly showing the form again.
void handleLevel() {
  if (!allowed()) return;

  String h = F("<!doctype html><meta charset=utf-8>"
               "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
               "<body style=\"font:16px system-ui,sans-serif;padding:16px\"><p>");
  if (imu::levelNow()) {
    h += F("Levelled. Heel and pitch are measured from the boat's present attitude.");
  } else {
    const char *why = imu::statusText();
    h += F("Not levelled: the motion sensor is ");
    h += why;
    h += F(".");
    if (strncmp(why, "settling", 8) == 0) {
      h += F(" Its readings settle within half a minute of a start - try again then.");
    }
  }
  h += F(" <a href=/>Back</a>");
  server.send(200, "text/html", h);
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
  j += "\",\"imu\":\"";
  j += imu::statusText();
  j += "\",\"barometer\":\"";
  j += barometer::statusText();
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
  server.on("/level", HTTP_POST, handleLevel);
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
