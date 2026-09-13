# NNN - Titel des Features

| | |
|---|---|
| **Status** | Entwurf |
| **Stufe** | 1 / 2 / 2.5 / 2B / 3 / 4 |
| **Roadmap-Paket** | z. B. 1.2 |
| **Erstellt** | TT.MM.JJJJ |
| **Zuletzt geändert** | TT.MM.JJJJ |
| **Betrifft Hardware** | ja / nein |

## 1. Ziel

Was soll das Feature leisten, in zwei bis drei Sätzen. Was ist ausdrücklich **nicht** Teil davon.

## 2. Ausgangslage

Was ist schon vorhanden, was wird vorausgesetzt, welche anderen Dokumente hängen damit zusammen.

## 3. Hardware

Nur ausfüllen, wenn gelötet oder verdrahtet wird.

**Bauteile**

| Teil | Menge | Zweck | Bemerkung |
|------|-------|-------|-----------|
| | | | |

**Pinbelegung**

| GPIO | Signal | Richtung | Pegel | Bemerkung |
|------|--------|----------|-------|-----------|
| | | | | |

**Schaltung**

```
ASCII-Skizze oder Verweis auf den Schaltplan
```

**Elektrische Randbedingungen** - Spannungsbereiche, Ströme, Pull-ups, Schutzbeschaltung,
Leitungslängen, Masseführung.

## 4. Software

Aufbau, Module, Zustandsautomat, Timing und Intervalle, benötigte Bibliotheken.

**Datenformat / Schnittstelle**

```json
```

**Konfigurierbare Werte** - was gehört in NVS/Preferences, was sind Konstanten, was sind
Kalibrierwerte.

## 5. Fehlerfälle

| Fall | Erkennung | Reaktion |
|------|-----------|----------|
| Sensor antwortet nicht | | |
| Wert unplausibel | | |
| Verbindung weg | | |

Ein Fehler in diesem Feature darf die übrigen Funktionen nicht mitreißen.

## 6. Sicherheit

Was kann schiefgehen und was schützt davor. Insbesondere:

- Verhalten beim Booten, nach Reset und nach Verbindungsabbruch
- Rückwirkungen auf bestehende Bordsysteme (Autopilot, SeaTalk-Bus, Bordnetz)
- Absicherung und galvanische Trennung
- Zugriffsschutz: was darf nur lokal im Bord-WLAN, was darf über den Server

Bei allem, was auf SeaTalk schreibt oder den Kurs beeinflusst, ist dieser Abschnitt Pflicht.

## 7. Test

**Am Tisch**

- [ ] ...

**Im Boot**

- [ ] ...

**Kalibrierung** - Referenzgerät, Vorgehen, wo der ermittelte Wert abgelegt wird.

## 8. Offene Punkte

| Punkt | Entscheidung bis | Wer |
|-------|------------------|-----|
| | | |

## 9. Referenzen

- Datenblätter, Projekte, Diskussionen
