# M5 Core2: Sensor-Testsystem mit JSON-Protokoll-Engine

**Alle Sensorabläufe liegen auf der SD-Karte.** Die Firmware enthält keine
VL6180-, VL53L4CD-, MAX31865- oder ST25DV-Treiber. Sie interpretiert allgemeine
Busoperationen, Prüfungen und Formeln aus jeweils einer JSON pro Sensor.

Neue Sensoren benötigen nur eine neue JSON, sofern sich ihr Protokoll durch die
unterstützten Operationen beschreiben lässt. Die aktuelle Engine unterstützt
registerbasierte **I2C- und SPI-Geräte**. UART, analoge Spannungs-/Stromeingänge,
CRC und spezielle Busprotokolle sind noch nicht implementiert. Dafür braucht die
Engine eine entsprechende allgemeine Erweiterung. Unbekannte Operationen werden
abgewiesen; es gibt keine versteckten Treiber-Fallbacks.

## Aktualisierung von der ersten Version

Einmalig ist ein Firmware-Upload nötig, weil die bisherige Version feste Treiber
verwendete. Danach werden unterstützte Sensorprotokolle ausschließlich auf SD
ergänzt oder geändert. **Die alten JSON-Dateien (schema_version 1) ersetzen:**
Die neue Firmware erwartet Format 2 und weist alte Profile zurück.

1. In VS Code `git pull origin main` ausführen.
2. PlatformIO IDE installieren und Projektordner mit `platformio.ini` öffnen.
3. PlatformIO **Build** und mit per USB verbundenem Core2 **Upload** ausführen.
4. Den Inhalt von `sd/sensors` nach `/sensors` auf eine FAT32-SD-Karte kopieren.
5. Karte einlegen, Core2 starten oder **SD** im Display drücken.

CLI: `pio run`, `pio run -t upload`, `pio device monitor -b 115200`.
Keine SD-Dateien über `uploadfs` übertragen.

## Bedienung

- **Weiter**: manuelle Auswahl unter den Scan-Treffern (ohne Treffer unter allen
  Profilen); vorherige Messung und Autoscan werden gestoppt.
- **Start**: Erkennung, Initialisierung und wiederholte Messung des Profils;
  während einer Messung stoppt derselbe Knopf.
- **Auto**: periodischen Autoscan ein-/ausschalten. Er ist nach dem Einschalten
  und nach SD-Neuladen aktiv und prüft im Ruhezustand alle drei Sekunden die
  Profile. Pro Programmdurchlauf wird höchstens ein Profil geprüft. Ein Treffer
  wird ausgewählt; bei mehreren Treffern Auswahl mit Weiter. Start erfolgt manuell.
  Während einer Messung pausiert der Scan; nach Stop oder Messfehler läuft er
  weiter, sofern nicht ausgeschaltet. Sensorwechsel werden so ohne Neustart erkannt.
  Kein Treffer zeigt ausdrücklich „Kein Sensor erkannt“, statt das erste Profil
  als angeschlossenen Sensor auszugeben.
- **SD**: Profile neu einlesen und automatisch prüfen. Vor Kartenwechsel stoppen.

Name, Sensortyp, Gruppe und Ausgabekanäle kommen aus der JSON. Messergebnisse
erscheinen auf dem Display und im seriellen Monitor als
`Millisekunden,Sensortyp,Variable,Wert,Einheit`. Es gibt noch keine SD-Aufzeichnung
oder statistische Analyse. Bei einem Messfehler wird gestoppt und die im Profil
hinterlegte Stoppsequenz versucht. Fehler beim Stoppen erscheinen auf Serial.
Während einer Sequenz kann die Touch-Bedienung bis zu deren Ende verzögert sein;
Polls sind begrenzt, eine gesamte Sequenz auf 10 Sekunden.

## Anschlüsse / Board-Grenzen

Die Pins sind Bestandteil des Core2-Boardadapters, nicht des Sensorprotokolls.
Profile können keine internen Touch-, Display- oder Versorgungspins umbelegen.

| Bus | Anschluss |
|---|---|
| I2C extern, 100 kHz | Port A: SDA GPIO32, SCL GPIO33 |
| SPI | Erweiterungsanschluss: SCK GPIO18, MOSI GPIO23, MISO GPIO38, CS GPIO14 |
| Interne SD-Karte | gemeinsamer SPI-Bus, eigener CS GPIO4 |

SPI-Mode, Taktrate und Registeradressierung werden pro JSON konfiguriert.
MAX31865-MISO muss bei inaktivem CS hochohmig sein. GND gemeinsam anschließen.
Nur passende Breakouts mit 3,3-V-kompatiblen Signalen verwenden. Die
5-V-Versorgung am Grove-Port bedeutet keine 5-V-Verträglichkeit der Datenpins.
Versorgungsspannungen werden nicht automatisch umgeschaltet.

## Enthaltene Protokollprofile

| Datei | Inhalt | Erkennung |
|---|---|---|
| `vl6180.json` | ST-Registerinitialisierung, Einzelmessung, Range-Status, mm | Modell-ID 0xB4 |
| `vl53l4cda.json` | ST-Konfigurationsblock, VHV-Start, oszillatorabhängiges Timing, kontinuierliche Messung, mm | Modell-ID 0xEBAA |
| `max31865.json` | Bias, Einzelmessung, Fehlerprüfung, Widerstand und PT100/PT1000-Formeln inkl. negativem Bereich | nur Plausibilität, keine Chip-ID |
| `st25dv16.json` | zwei 32-Bit-Worte aus EEPROM 0..7, rein lesend | Adresse/Speicherantwort, kein eindeutiger Modellnachweis |

VL6180 und VL53L4CD verwenden beide 0x29: vorerst nur einen gleichzeitig anschließen.
Die automatische Suche beweist bei Geräten ohne Chip-ID nicht den genauen Typ.
Profile mit `identification: "model_id"` melden einen Modell-ID-Treffer; bei
`"response"` (Standard) steht „Profil plausibel; Modell nicht bestätigt“. Die
Angabe muss zur lesenden `detect`-Sequenz passen. Die Suche berücksichtigt nur
Profile auf der SD-Karte und kann Sensoren ohne passendes Profil nicht bestimmen.
Kein Lux-Modus beim VL6180; beim ST25DV16 keine RF-Konfiguration oder Schreibzugriffe.

MAX31865-Konstanten: `rtd_nominal_ohm` = 100 (PT100) oder 1000 (PT1000),
`reference_ohm` = tatsächlich bestückter Referenzwiderstand, `wire_bits` = 16 für
3-Leiter, 0 für 2-/4-Leiter; Hardwarebrücken entsprechend setzen. `filter_bits` = 1
für 50 Hz, 0 für 60 Hz. Beim VL53L4CD ist `timing_budget_ms` von 10 bis 200 zulässig.

## Eigene Profile und KI

Der [Profilvertrag](docs/PROFILE_FORMAT.md) beschreibt alle Operationen.
Das [leere Template](docs/profile-template.json) ist die Ausgangsbasis für neue
Sensoren; die vier SD-Profile zeigen vollständige Abläufe. Keine Sensornamen müssen
in den Firmwarequellen ergänzt werden. Die [KI-Arbeitsanweisung](docs/AI_PROFILE_PROMPT.md)
verlangt belegte Registerdaten und hält offene Angaben fest. Die Firmware lernt
nicht eigenständig; geprüfte Profile und Korrekturen werden versioniert.

## Prüfung

`pio run -j 4` erstellt die Core2-Firmware.
`bash tools/test_engine.sh` prüft denselben C++-Interpreter auf Linux mit C++17,
ArduinoJson aus `.pio/libdeps` und Address/UndefinedBehavior-Sanitizer.

Die Tests simulieren die vier Geräte und prüfen detect/init/measure/stop,
VL53-Timing gegen die ursprüngliche Integerberechnung, PT100/PT1000 bei 0 und
negativer Temperatur, einen ausschließlich durch JSON hinzugefügten fünften
Sensortyp sowie ungültige Profile, Rechenfehler, Busfehler, Poll- und Schleifenlimits.
Autoscan-Tests prüfen periodische Planung, einen Probezugriff je Durchlauf,
Mehrdeutigkeit, Sensorwechsel/Abziehen und millis-Überlauf.
Simulationen ersetzen keine Hardwaretests. Alle vier neuen Protokollprofile
müssen noch an den realen Sensoren geprüft werden; insbesondere Versorgung,
Breakout-Varianten, Kalibrierung, Neustart, Start/Stopp und Sensorabziehen.

Quellen und Lizenzhinweise der abgeleiteten Initialisierungsdaten und Formeln:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Sensorbibliotheken sind keine
Build-Abhängigkeiten mehr.
