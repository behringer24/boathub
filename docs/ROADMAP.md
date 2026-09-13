# Roadmap

Ausbaustufen des ESP32 BoatHub. Jede Stufe ist eigenständig nutzbar und wird erst begonnen, wenn
die vorherige stabil läuft. Grundlage: Projektanleitung v0.1 vom 13.09.2026.

**Legende Status:** `offen` · `in Arbeit` · `erledigt` · `blockiert` · `zurückgestellt`

---

## Stufe 1 - Basis-Monitoring

**Status:** in Arbeit
**Ziel:** Temperaturen, Feuchte, Batteriespannung und optional Bilgenpegel erfassen, im Bord-WLAN
anzeigen und über das Marina-WLAN an den eigenen Server senden.

### Arbeitspakete

| # | Paket | Status | Design-Doc |
|---|-------|--------|------------|
| 1.1 | Entwicklungsumgebung, Blink-/Seriell-Test | offen | - |
| 1.2 | DS18B20 Motorraum / Bilge / Kühlschrank (GPIO4/5/6) | offen | geplant |
| 1.3 | SHT31-D Kajüte (I2C 0x44) | offen | geplant |
| 1.4 | ADS1115 x3 (0x48/0x49/0x4A) | offen | geplant |
| 1.5 | 12-V-Versorgung mit Sicherung, Verpol- und Transientenschutz | offen | geplant |
| 1.6 | Batteriespannungsmessung 82k/10k, Kalibrierung | offen | geplant |
| 1.7 | SoftAP BOOT-NETZ + lokale Konfigurations-Weboberfläche | offen | geplant |
| 1.8 | Station-Modus Marina-WLAN, Konfiguration in NVS | offen | geplant |
| 1.9 | Server-Uplink: MQTT/TLS, Telemetrie, Heartbeat, Last-Will | offen | geplant |
| 1.10 | Alarme (Batterie niedrig, Frost, Feuchte, Bilge) | offen | geplant |
| 1.11 | Gehäuse, Einbau, Kabelbeschriftung | offen | - |
| 1.12 | Optional: Bilgenpegel 4-20 mA, kalibriert | offen | geplant |
| 1.13 | Optional: NAPT/NAT, damit Clients über den ESP ins Internet kommen | zurückgestellt | geplant |
| 1.14 | Optional: Wassertanktemperatur (GPIO7) | zurückgestellt | - |

### Abnahmekriterien

- [ ] Jeder DS18B20 wird einzeln erkannt und liefert plausible Werte
- [ ] SHT31 liefert Temperatur und relative Luftfeuchte
- [ ] Alle drei ADS1115 auf 0x48/0x49/0x4A erkannt
- [ ] Batteriespannung stimmt nach Kalibrierung mit dem Multimeter überein
- [ ] DC/DC liefert ohne ESP stabile 5,0 V; 3V3-Pin des ESP ca. 3,3 V
- [ ] BOOT-NETZ erscheint, Pixel verbindet sich mit der lokalen Weboberfläche
- [ ] Marina-WLAN wird verbunden, Reconnect nach Ausfall funktioniert
- [ ] Server zeigt Heartbeat und Messwerte von zuhause aus
- [ ] Box und DC/DC nach 30-60 Minuten Dauerbetrieb thermisch unauffällig
- [ ] Watchdog aktiv, Sensor- und Netzwerkfehler entkoppelt

### Server-Schnittstelle (Ziel)

```
boathub/<boot-id>/telemetry
boathub/<boot-id>/status      Last-Will: "offline"
boathub/<boot-id>/events
```

```json
{
  "ts": "2026-09-13T08:15:00Z",
  "battery_v": 12.73,
  "cabin_temp_c": 8.4,
  "cabin_rh": 72.1,
  "engine_temp_c": 7.9,
  "bilge_temp_c": 6.1,
  "fridge_temp_c": 5.2,
  "bilge_level_cm": 1.3,
  "seatalk_online": false
}
```

Heartbeat ca. jede Minute. Kritische Ereignisse (z. B. steigender Bilgenpegel) werden sofort
gesendet, nicht erst im normalen Telemetrieintervall.

---

## Stufe 2 - SeaTalk1 lesen

**Status:** geplant
**Ziel:** Daten aus dem vorhandenen SeaTalk1-Netz am freien Port des Raymarine S1 mitlesen.
**Voraussetzung:** Stufe 1 läuft stabil im Boot.

### Arbeitspakete

| # | Paket | Status |
|---|-------|--------|
| 2.1 | RX-Pegelanpassung/Isolation auf 3,3 V als eigene Schaltplan-Revision festlegen | offen |
| 2.2 | Bench-Test der RX-Stufe (Oszilloskop/Logikanalysator, 4800 Baud, 9. Bit) | offen |
| 2.3 | Rohe Bytes lesen und protokollieren (GPIO15) | offen |
| 2.4 | Datagramme dekodieren: Tiefe, Logge, Kompasskurs | offen |
| 2.5 | GPS-Position, SOG/COG, Zeit - falls im Bus vorhanden | offen |
| 2.6 | Autopilotstatus und Sollkurs dekodieren | offen |
| 2.7 | Unbekannte Datagramme roh sammeln und auswerten | offen |
| 2.8 | SeaTalk-Werte in Telemetrie und Bord-WLAN integrieren | offen |

### Abnahmekriterien

- [ ] RX-Stufe am Tisch getestet, bevor sie an den S1 kommt
- [ ] Rohdatenstrom über Stunden stabil, ohne Rückwirkung auf den Bus
- [ ] Tiefe, Speed, Kurs und - falls vorhanden - GPS eindeutig identifiziert
- [ ] Feld `seatalk_online` in der Telemetrie korrekt

**Offen:** Die genaue RX/TX-Stufe ist bewusst noch nicht festgelegt. Referenzen: APRemote
(ESP32 + SeaTalk1), Open-Collector-Schaltungen mit 74LS07, Signal K Autopilot.

---

## Stufe 2.5 - Track-Logging und digitales Logbuch

**Status:** geplant
**Ziel:** Fahrten offline aufzeichnen und in der Marina automatisch zum Server synchronisieren.
**Voraussetzung:** GPS-Daten aus Stufe 2 verfügbar.

### Arbeitspakete

| # | Paket | Status |
|---|-------|--------|
| 2.5.1 | Trackpunkt-Format festlegen (Zeit, Lat/Lon, SOG/COG, Heading, Tiefe, AP-Status, Batterie) | offen |
| 2.5.2 | Ringpuffer in LittleFS, blockweises Schreiben aus dem RAM-Puffer | offen |
| 2.5.3 | Fahrterkennung: Start bei GPS + Bewegung über Schwellwert, Ende nach Ruhephase | offen |
| 2.5.4 | Zeitbasis: GPS-Zeit, sonst NTP über Marina-WLAN | offen |
| 2.5.5 | Upload noch nicht übertragener Fahrten, quittiert und wiederaufsetzbar | offen |
| 2.5.6 | Serverseitig: Kartenansicht, Logbucheinträge, GPX-/CSV-Export | offen |

### Abnahmekriterien

- [ ] Intervall 5-10 s, Flash-Schreibvorgänge durch RAM-Puffer reduziert
- [ ] Eine komplette Fahrt ohne Internet aufgezeichnet und danach vollständig hochgeladen
- [ ] Übertragene Tracks werden bei Platzbedarf zuerst gelöscht, aktuelle nie
- [ ] Server zeigt Start, Ziel, Dauer, Distanz, Durchschnitts- und Maximalgeschwindigkeit

---

## Stufe 2B - Autopilot steuern (SeaTalk1 TX)

**Status:** blockiert - erst nach stabilem RX-Betrieb und getesteter Ausgangsstufe
**Ziel:** Lokale Bedienung des Raymarine S1 aus dem Bord-WLAN.

### Arbeitspakete

| # | Paket | Status |
|---|-------|--------|
| 2B.1 | Open-Collector/Open-Drain-Ausgangsstufe entwerfen und am Tisch testen | blockiert |
| 2B.2 | TX hochohmig beim Booten, Reset und im Fehlerfall sicherstellen | blockiert |
| 2B.3 | Kommandos +1 / -1 / +10 / -10 Grad | blockiert |
| 2B.4 | AUTO / STANDBY | blockiert |
| 2B.5 | Freigabelogik: nur lokales Bord-WLAN, nach Neustart gesperrt | blockiert |
| 2B.6 | TRACK / Route - erst nach Navigationstest, mit Bestätigung durch den Benutzer | blockiert |

### Sicherheitsregeln

- Der Bus darf niemals aktiv auf 12 V getrieben werden.
- STANDBY ist immer direkt erreichbar.
- AUTO nur durch explizite Bedienaktion, nie automatisch nach Neustart.
- Der vorhandene Raymarine-Bedienteil bleibt immer erhalten und funktionsfähig.
- **Keine Autopilot-Befehle aus dem Internet oder vom Server.**

---

## Stufe 3 - NMEA2000

**Status:** Zukunft
**Ziel:** TWAI/CAN-Anbindung als Gateway zu moderner Bordelektronik.
**Voraussetzung:** Stufe 1 und SeaTalk stabil. Erst dann wird die CAN-Schnittstelle dimensioniert
und der Backbone geplant.

| # | Paket | Status |
|---|-------|--------|
| 3.1 | Externen CAN-Transceiver auswählen, isolierte Schnittstelle an GPIO17/18 | offen |
| 3.2 | NMEA2000 nach WLAN für Tablet/Server | offen |
| 3.3 | SeaTalk1 nach NMEA2000 für alte Raymarine-Daten | offen |
| 3.4 | Eigene Sensorwerte nach NMEA2000, soweit sinnvolle PGNs existieren | offen |
| 3.5 | Optional: AIS, moderne Sensorik, Orca Core o. ä. | offen |

---

## Stufe 4 - OpenCPN auf Tablet/Pixel

**Status:** Zukunft
**Ziel:** Das Bord-WLAN verteilt Navigationsdaten an OpenCPN. Tablet als Hauptbildschirm, Pixel als
Backup. Der ESP bleibt Gateway, nicht Kartenplotter.

---

## Bauabende

Reihenfolge für den gemeinsamen Aufbau aus der Projektanleitung.

| Abend | Ziel | Fertig wenn... | Status |
|-------|------|----------------|--------|
| 1 | ESP kennenlernen | Serieller Monitor und erster Test laufen | offen |
| 2 | Drei DS18B20 | alle drei Temperaturen einzeln stabil | offen |
| 3 | SHT31 + I2C | Kajütentemperatur und Feuchte sichtbar | offen |
| 4 | ADS1115 | I2C-Adressen und Testspannung messbar | offen |
| 5 | 12-V-Versorgung | 5 V sauber, Schutzteile eingebaut | offen |
| 6 | Batteriemessung | Wert gegen Multimeter kalibriert | offen |
| 7 | Boot-WLAN | Pixel verbindet sich lokal mit Weboberfläche | offen |
| 8 | Marina-WLAN + Server | Messwerte von zuhause sichtbar | offen |
| 9 | Gehäuse/Einbau | Box sicher montiert, Kabel beschriftet | offen |
| 10 | Bilgenpegel optional | 4-20 mA kalibriert | offen |
| 11 | SeaTalk RX | nur lesen, Daten roh loggen | offen |
| 12 | SeaTalk dekodieren | Tiefe/Speed/Kurs/GPS identifiziert | offen |
| 13 | Track-Logger | Fahrt offline speichern und hochladen | offen |
| 14 | SeaTalk TX | Bench-Test, dann lokale Autopilot-Steuerung | offen |
| 15 | NMEA2000 | isolierte CAN-Schnittstelle hinzugefügt | offen |

---

## Offene Entscheidungen

| Thema | Stand |
|-------|-------|
| Zweiter ESP als reines Netzwerk-Gateway | nur Reserve; erst wenn Router/NAT von Bordfunktionen getrennt werden soll (Kopplung per UART) |
| SeaTalk RX/TX-Schaltung | bewusst noch nicht festgelegt, eigene Revision vor dem Anschluss |
| NAPT/NAT in der Firmware | optional; AP+STA allein ist noch kein Router |
| Verlustfreier MOSFET-Verpolschutz | erst bei einer eigenen PCB, Prototyp misst hinter der Schottky-Diode |
| Bilgensonde Edelstahlqualität | bei Salz-/Brackwasser regelmäßig auf Korrosion prüfen, austauschbar montieren |
