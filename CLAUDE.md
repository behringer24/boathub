# ESP32 BoatHub - Arbeitsweise

## Git

- Hauptbranch ist `main`.
- **Commit-Nachrichten enthalten niemals einen `Co-Authored-By:`-Trailer** und keinen Hinweis auf
  ein KI-Werkzeug. Auch nicht in Pull-Request-Beschreibungen.
- Commit-Nachrichten auf Deutsch, Betreffzeile im Imperativ.

## Dokumentation

- Projektsprache ist Deutsch.
- Neue Features bekommen vor der Umsetzung ein Designdokument in `docs/design/`
  (Vorlage: `docs/design/TEMPLATE.md`), eingetragen in die Übersicht in `docs/design/README.md`.
- Statusänderungen werden in `docs/ROADMAP.md` nachgezogen.
- Nennenswerte Änderungen kommen in `docs/CHANGELOG.md`; Hardware-Änderungen mit **[HW]**
  markieren.
- Quelldokumente liegen unter `docs/reference/`.

## Technische Leitplanken

Diese Regeln stammen aus der Projektanleitung und sind nicht verhandelbar:

- Keine WLAN- oder Server-Passwörter im Quellcode. Konfiguration in NVS/Preferences.
- Server-Telemetrie ist Einbahnstraße: **keine Autopilot- oder Steuerbefehle aus dem Internet.**
  Steuerung nur im lokalen Bord-WLAN.
- SeaTalk-TX bleibt deaktiviert, bis RX stabil läuft und die Ausgangsstufe getestet ist. Nach Reset
  oder Verbindungsabbruch ist der Sendeteil passiv.
- Sensor- und Netzwerkfehler sind voneinander entkoppelt; ein defekter Sensor darf das System nicht
  lahmlegen. Watchdog nutzen.
- Reservierte GPIOs (15/16 SeaTalk, 17/18 TWAI, 43/44 Debug-UART) nicht anderweitig belegen.
  Strapping-Pins GPIO0/3/45/46, USB-Pins GPIO19/20 und beim N16R8 GPIO33-37 meiden.
