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

Genau drei Touch-Schaltflächen, von links nach rechts:

- **Autoscan**: Suche ein-/ausschalten. Beim Einschalten des Core2 läuft sie
  automatisch. Ohne eindeutigen Treffer wird alle drei Sekunden erneut gesucht.
  Sobald genau ein Profil antwortet, wird es ausgewählt und der Autoscan
  **automatisch deaktiviert**. Ein erneuter Druck startet eine neue Suche und
  lädt dabei auch die SD-Profile neu. Es gibt keinen separaten SD-Knopf mehr.
- **Start/Stop Auswertung**: Sensor prüfen, initialisieren und Messung starten
  bzw. stoppen. Die Suche bleibt nach Stop ausgeschaltet. Messfehler stoppen die Auswertung
  nicht: beim nächsten Intervall wird erneut gemessen.
  Ein Sensorwechsel erfordert einen neuen Autoscan. Eine neue Suche beendet
  eine laufende Auswertung zuerst.
- **Sensor durchblättern**: alle geladenen Profile nacheinander anzeigen.
  Laufende Auswertung und Autoscan werden beendet. Start prüft den Anschluss
  erneut, bevor dieses Profil initialisiert wird.

Bei mehreren Treffern erfolgt keine automatische Auswahl. Mit Sensor
manuell wählen oder Autoscan ausschalten. Ohne Treffer wird kein beliebiges
Profil als erkannter Sensor angezeigt. Vor Kartenwechsel Auswertung stoppen;
anschließend Autoscan neu einschalten, um die Karte erneut einzulesen.

Name, Sensortyp, Gruppe und Ausgabekanäle kommen aus der JSON. Messergebnisse
erscheinen auf dem Display und im seriellen Monitor als
`Millisekunden,Sensortyp,Variable,Wert,Einheit`. Es gibt noch keine SD-Aufzeichnung
oder statistische Analyse. Bei einem Messfehler wird kein gültiger Messwert angezeigt. Die Auswertung
bleibt aktiv und versucht beim nächsten Intervall erneut zu messen; eine gültige
Messung ersetzt die Fehlermeldung automatisch. Fehler beim Stoppen erscheinen auf Serial.
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
automatisches Abschalten nach eindeutigem Treffer, expliziten Neustart,
Mehrdeutigkeit, Sensorwechsel/Abziehen und millis-Überlauf.
Simulationen ersetzen keine Hardwaretests. Alle vier neuen Protokollprofile
müssen noch an den realen Sensoren geprüft werden; insbesondere Versorgung,
Breakout-Varianten, Kalibrierung, Neustart, die drei Touch-Tasten, Start/Stopp
und Sensorabziehen.

Quellen und Lizenzhinweise der abgeleiteten Initialisierungsdaten und Formeln:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Sensorbibliotheken sind keine
Build-Abhängigkeiten mehr.

## VL6180 meldet Messfehler

Die alte Meldung „Ungueltiger Range-Status“ besagt nur, dass der Sensor einen
Status ungleich 0 liefert. Das aktualisierte `sd/sensors/vl6180.json` zeigt
stattdessen den konkreten Sensorcode. **Nur diese Datei auf der SD ersetzen**,
keinen neuen Firmware-Upload durchführen. Danach Autoscan neu starten, damit die
SD-Profile eingelesen werden, und Start drücken.

Für einen ersten Test eine matte, helle Fläche etwa 20–50 mm vor die optische
Sensoröffnung halten. Fenster auf Schutzfolie, Verschmutzung oder Verdeckung
prüfen; direktes Sonnenlicht vermeiden. Nur einen Sensor auf I2C-Adresse 0x29
anschließen. Der VL6180 ist für kurze Abstände gedacht.

- Status 1–5: interne Systemfehler; Versorgung und Sensorzustand prüfen.
- Status 6: frühe Konvergenz fehlgeschlagen.
- Status 7: keine Konvergenz / kein Ziel erkannt.
- Status 8: Range-Ignore-Prüfung fehlgeschlagen.
- Status 9–10: reservierte Codes.
- Status 11: unzureichendes Signal/Rausch-Verhältnis.
- Status 12/13: Rohwert-Unterlauf/-Überlauf.
- Status 14/15: Abstands-Unterlauf/-Überlauf.

Ein ungültiger Status wird weiterhin nicht als gültiger Abstand ausgegeben.
Die Änderung verbessert die Diagnose; sie bestätigt keine Behebung der Ursache
am realen Sensor. Bei anhaltendem Fehler Statuscode, Breakout-Modell, Zielabstand
und Anschluss angeben, um Initialisierung, Messbedingungen und Hardware gezielt
zu prüfen.
