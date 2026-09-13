# Changelog

Alle nennenswerten Änderungen an Firmware, Hardware und Serverseite des ESP32 BoatHub.

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.1.0/),
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/).

Kategorien: `Hinzugefügt` · `Geändert` · `Veraltet` · `Entfernt` · `Behoben` · `Sicherheit`

Hardware-Änderungen (Schaltung, Pinbelegung, Bauteile) werden mit dem Präfix **[HW]**
gekennzeichnet, damit sie beim Nachbau und bei Revisionen der Platine auffindbar bleiben.

---

## [Unveröffentlicht]

### Hinzugefügt

- Projekt-Repository angelegt: `docs/` mit Roadmap, Changelog, Design-Verzeichnis und
  Quelldokumenten.
- README mit Architekturüberblick, Pinplan, Hardware-Eckdaten und Sicherheitsregeln.
- Roadmap mit den Ausbaustufen 1, 2, 2.5, 2B, 3 und 4 inklusive Abnahmekriterien und der
  Reihenfolge der Bauabende.
- Vorlage und Index für Feature-Designdokumente unter `docs/design/`.
- Projektanleitung v0.1 (13.09.2026) als Quelldokument unter `docs/reference/`.

---

## Vorgeschichte

Stand vor Beginn der Repository-Historie, übernommen aus dem Änderungsprotokoll der
Projektanleitung.

### 0.1 - 13.09.2026

- Erste zusammengefasste Projektanleitung.
- Stufe 1 konkretisiert; SeaTalk1 und NMEA2000 als spätere Ausbauphasen abgegrenzt.
- **[HW]** Batteriespannungsteiler auf 82 kΩ / 10 kΩ festgelegt (Teilerfaktor 9,2) für mehr
  Spannungsreserve gegenüber Bordnetzspitzen.
- **[HW]** Pinplan festgelegt: DS18B20 auf GPIO4/5/6, I2C auf GPIO8/9, SeaTalk auf GPIO15/16
  reserviert, TWAI auf GPIO17/18 reserviert.
