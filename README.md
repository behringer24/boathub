# ESP32 BoatHub

Bootsmonitoring, SeaTalk1-Gateway, Track-Logger und NMEA2000-Vorbereitung auf Basis eines
ESP32-S3 N16R8.

Ein dauerhaft eingeschaltetes ESP32-System überwacht das Boot in der Marina, stellt ein eigenes
Bord-WLAN bereit, sendet Messwerte über eine ausgehende TLS-Verbindung an den eigenen
Docker-Host und wird später zum Gateway für SeaTalk1, Autopilot, Track-Logging und NMEA2000
erweitert.

Grundlage ist die Projektanleitung v0.1 vom 13.09.2026
([PDF](docs/reference/ESP32_BoatHub_Projektanleitung.pdf)).

## Status

| Stufe | Ziel | Status |
|-------|------|--------|
| 1 | Basis-Monitoring: Temperaturen, Feuchte, Batterie, optional Bilgenpegel, Boot-WLAN, Marina-WLAN, Server-Uplink | in Arbeit |
| 2 | SeaTalk1 lesen: Logge, Tiefe, Kompass, GPS, Autopilotstatus | geplant |
| 2.5 | GPS-Tracks lokal speichern und als digitales Logbuch synchronisieren | geplant |
| 2B | SeaTalk1 schreiben: lokale Autopilotsteuerung | erst nach RX-Test |
| 3 | NMEA2000 über TWAI/CAN | Zukunft |
| 4 | Android-Tablet/Pixel mit OpenCPN als Plotter | Zukunft |

Details und Abnahmekriterien: [docs/ROADMAP.md](docs/ROADMAP.md)

## Architektur (Zielbild)

```
Sensorik (1-Wire / I2C / 4-20 mA)
        |
     ESP32-S3 N16R8  ──SoftAP──>  BOOT-NETZ (Pixel, Tablet, OpenCPN)
        |   |
        |   └──STA──> Marina-WLAN ──TLS──> eigener Docker-Host
        |                                     ├── Mosquitto (MQTT)
   SeaTalk1 (Stufe 2)                         ├── Backend/API
   NMEA2000 (Stufe 3)                         ├── PostgreSQL + PostGIS
                                              └── Grafana / Web-Dashboard
```

Der ESP baut die Internetverbindung von innen nach außen auf. Im Marina-Netz sind keine
eingehenden Ports nötig.

## Hardware-Eckdaten

- **Zentrale:** 1 x ESP32-S3 DevKitC-1 N16R8 mit externer Antenne
- **Temperatur:** 3 x DS18B20 (Motorraum, Bilgenwasser, Kühlschrank), je eigener 1-Wire-GPIO
- **Klima Kajüte:** SHT31-D (I2C, Adresse 0x44)
- **Analog:** 3 x ADS1115 (0x48 / 0x49 / 0x4A) am gemeinsamen I2C-Bus
- **Batterie:** Spannungsteiler 82 kΩ / 10 kΩ (Faktor 9,2) auf ADS1115 A0, gegen Multimeter kalibriert
- **Bilgenpegel (optional):** hydrostatischer 0-1-m-Sensor, 4-20 mA, 100-Ω-Shunt auf ADS1115 A1
- **Versorgung:** 12 V Bordnetz → 2-A-Sicherung → 1N5822 → TVS 1.5KE20A → DC/DC 9-36 V auf 5 V

### Pinplan

| GPIO | Funktion heute | Später / Hinweis |
|------|----------------|------------------|
| 4 | DS18B20 Motorraum | eigener 1-Wire-Bus |
| 5 | DS18B20 Bilgenwasser | eigener 1-Wire-Bus |
| 6 | DS18B20 Kühlschrank | eigener 1-Wire-Bus |
| 7 | Reserve | optional Wassertank-DS18B20 |
| 8 | I2C SDA | SHT31 + alle ADS1115 |
| 9 | I2C SCL | SHT31 + alle ADS1115 |
| 15 | reserviert | SeaTalk RX (Stufe 2) |
| 16 | reserviert | SeaTalk TX (Stufe 2) |
| 17 | reserviert | TWAI TX (Stufe 3) |
| 18 | reserviert | TWAI RX (Stufe 3) |
| 21 | Reserve | optional lokaler Summer |
| 43/44 | Debug UART | für Service frei lassen |

Vermieden werden die Strapping-Pins GPIO0/3/45/46, die USB-Pins GPIO19/20 und beim N16R8
GPIO33-37 (Octal-PSRAM). Die Beschriftung des gelieferten DevKit-Boards vor dem Löten
gegenprüfen.

## Sicherheitsregeln

- **12 V sind nicht harmlos.** Eine Bootsbatterie liefert sehr hohe Kurzschlussströme. Jede neue
  Zuleitung bekommt nahe an der Quelle eine eigene Sicherung.
- Beim Flashen per USB die externe 5-V-Einspeisung ausschalten. Niemals 5 V auf 3V3 geben.
- **SeaTalk-TX bleibt deaktiviert**, bis RX stabil läuft und die Ausgangsstufe separat am Tisch
  getestet wurde. Nach Reset oder Verbindungsabbruch ist der Sendeteil passiv.
- **Keine Autopilot-Befehle aus dem Internet.** Steuerung ausschließlich im lokalen Bord-WLAN,
  der Server empfängt nur Telemetrie.
- Keine WLAN- oder Server-Passwörter im Quellcode. Konfiguration in NVS/Preferences.

## Repository-Struktur

```
docs/
├── ROADMAP.md          Ausbaustufen, Abnahmekriterien, Bauabende
├── CHANGELOG.md        Änderungsprotokoll des Projekts
├── design/             Planungsdokumente je Feature (+ TEMPLATE.md)
└── reference/          Quelldokumente (Projektanleitung als PDF)
```

## Nächster Schritt

Stufe 1 auf dem Tisch aufbauen: ESP32 per USB, danach DS18B20, SHT31 und ADS1115. Erst wenn die
Sensorik stabil ist, wird die 12-V-Versorgung hinzugefügt.
