# Design-Dokumente

Ein Planungsdokument je Feature. Gedacht zum Denken **vor** dem Löten und Programmieren: Was soll
das Feature können, wie wird es elektrisch und softwareseitig gelöst, wie wird es getestet und was
kann dabei schiefgehen.

## Ablauf

1. Neues Dokument aus [TEMPLATE.md](TEMPLATE.md) kopieren.
2. Dateiname: `NNN-kurzer-name.md`, fortlaufende Nummer, Kleinbuchstaben, Bindestriche.
   Beispiel: `001-ds18b20-temperatursensoren.md`
3. Unten in die Übersicht eintragen.
4. Status pflegen: `Entwurf` → `In Review` → `Angenommen` → (`Umgesetzt` | `Verworfen` |
   `Abgelöst durch NNN`).
5. Angenommene Hardware-Entscheidungen zusätzlich in [../CHANGELOG.md](../CHANGELOG.md) mit
   **[HW]** vermerken und den Status in [../ROADMAP.md](../ROADMAP.md) nachziehen.

Angenommene Dokumente werden nicht stillschweigend umgeschrieben. Ändert sich eine Entscheidung,
bekommt sie ein neues Dokument, das das alte ablöst - so bleibt nachvollziehbar, warum an Bord
etwas so verdrahtet ist, wie es verdrahtet ist.

## Übersicht

| Nr. | Feature | Stufe | Status | Dokument |
|-----|---------|-------|--------|----------|
| - | noch keine Dokumente angelegt | - | - | - |

## Geplante Dokumente

Reihenfolge entlang der Roadmap; wird beim Anlegen in die Übersicht oben verschoben.

**Stufe 1**

- DS18B20-Temperatursensoren an drei getrennten 1-Wire-GPIOs
- SHT31-D Kajütenklima am gemeinsamen I2C-Bus
- ADS1115-Kanalbelegung und Messwertaufbereitung
- 12-V-Versorgung: Sicherung, Verpolschutz, TVS, DC/DC
- Batteriespannungsmessung und Kalibrierverfahren
- Bilgenpegel 4-20 mA (optional)
- WLAN-Betrieb: SoftAP `BOOT-NETZ` + Station Marina, Reconnect-Verhalten
- Konfiguration und Secrets in NVS/Preferences, lokale Weboberfläche
- Server-Uplink: MQTT über TLS, Telemetrieschema, Heartbeat, Last-Will
- Alarm- und Schwellwertlogik
- Fehlerbehandlung und Watchdog: Sensor- und Netzwerkfehler entkoppeln

**Stufe 2 und später**

- SeaTalk1-RX-Stufe: Pegelanpassung und Isolation (Schaltplan-Revision)
- SeaTalk1-Dekodierung: Datagramme, 4800 Baud, 9. Bit
- SeaTalk1-TX-Ausgangsstufe (Open Collector) und Sicherheitsverriegelung
- Autopilot-Bedienung im Bord-WLAN: Freigabelogik und Zustandsautomat
- Track-Logger: Datensatz, LittleFS-Ringpuffer, Fahrterkennung
- Track-Synchronisation und serverseitiges Logbuch
- NMEA2000: CAN-Transceiver, Isolation, PGN-Auswahl
