# M5 Core2 Sensor-Testsystem – erste Firmware

PlatformIO-Projekt für den klassischen M5Stack Core2 (ESP32), Arduino-Framework.
Die Sensoren verwenden eingebaute Treiber; pro Sensor liegt eine eigene JSON-Datei
auf der SD-Karte. Neue Parameter innerhalb dieser Treiber brauchen keinen neuen
Flash-Vorgang. Neue Treibertypen benötigen eine Firmware-Erweiterung.

## VS Code / Flashen

1. VS Code mit der Erweiterung **PlatformIO IDE** installieren.
2. Diesen Ordner öffnen, PlatformIO **Build** ausführen.
3. Core2 per USB anschließen und **Upload** ausführen.
4. Seriellen Monitor mit 115200 Baud öffnen.

Alternativ: `pio run`, `pio run -t upload`, `pio device monitor -b 115200`.
Die SD-Karte wird separat befüllt; `uploadfs` ist dafür nicht geeignet.

## SD-Karte und Bedienung

FAT32-formatierte Karte: den Ordner `sd/sensors` als `/sensors` auf die Karte
kopieren. Jede Datei mit Endung `.json` ist ein Profil. Die Firmware lädt maximal
24 Profile mit jeweils maximal 8 KiB. Fehlerhafte oder unbekannte Profile werden
verworfen und ihr Dateiname im seriellen Monitor ausgegeben.

Unten im Touchdisplay:

- **Weiter**: nächstes Profil, laufende Messung wird gestoppt.
- **Start/Stop**: Anschluss prüfen und messen bzw. stoppen.
- **SD**: Karte erneut einlesen. Vor Kartenwechsel Messung stoppen.

Angezeigt werden Name, Sensorgruppe, Sensortyp und aktueller Messwert bzw. Fehler.
Messwerte erscheinen zusätzlich als `Millisekunden,Sensortyp,Wert` auf Serial.
Es gibt in dieser Version keine SD-Messwertaufzeichnung, KI-Anbindung oder
statistische Messwertanalyse. Nach dem Einschalten wird keine Messung gestartet.
Die Profilauswahl erfolgt bewusst über Weiter; kein blindes Scannen aller Busse.

## Verdrahtung

Nur geeignete Breakout-Module mit 3,3-V-kompatiblen Signalen verwenden.
GND verbinden. Versorgung und Pinbelegung des konkreten Moduls vor Anschluss
prüfen. Die 5-V-Versorgung am Grove-Port ist keine Freigabe für 5-V-I2C-Pegel.
Keine Versorgung wird durch JSON automatisch umgeschaltet.

| Baustein | Core2-Anschluss | Details |
|---|---|---|
| VL6180 / VL6180X | SDA GPIO32, SCL GPIO33 | externer Port A, I2C-Adresse 0x29; geeignetes Breakout verwenden |
| VL53L4CDA / VL53L4CD | SDA GPIO32, SCL GPIO33 | externer Port A, I2C-Adresse 0x29; XSHUT muss am Modul freigegeben sein |
| ST25DV16 | SDA GPIO32, SCL GPIO33 | EEPROM-Bereich 0x53 bei passenden Adresspins; 16-Bit-Speicheradresse |
| MAX31865 | SCK GPIO18, MOSI GPIO23, MISO GPIO38, CS GPIO14 | SPI am Erweiterungsanschluss, gemeinsam mit internem SD-Bus; kein Grove-Anschluss |

MAX31865-MISO muss bei inaktivem CS hochohmig sein. CS14 ist ausschließlich für
dieses Modul reserviert; SD verwendet CS4. Keine weiteren Geräte an dieselben
CS-Pins anschließen. Alle Buspins sind in dieser Version fest vorgegeben, um
interne Display-, Touch- und Stromversorgungspins zu schützen.

### Sensorverhalten und Grenzen

- **VL6180 / VL6180X:** Abstand in mm, Hersteller-Treiberinitialisierung mit
  Modell-ID 0xB4. Messfehler werden als Range-Status angezeigt, nicht als Abstand.
  Registerzugriffe haben I2C-Timeouts und die Messwartezeit ist auf 250 ms
  begrenzt. Kein ALS-/Lux-Modus in dieser Version. VL6180 und VL53L4CD haben
  beide Adresse 0x29: nur einen gleichzeitig an Port A anschließen.
- **VL53L4CD:** ST-Treiber prüft Chip-ID 0xEBAA; Ausgabe in mm. Ein Range-Status
  ungleich 0 ist kein gültiger Messwert. Budget 10–200 ms, fortlaufendes Ranging.
- **ST25DV16:** liest 1–16 Bytes aus dem 2048-Byte-EEPROM. Keine Schreibzugriffe,
  keine RF-Konfiguration und keine Passwortänderung. ACK und Speicherlesen
  identifizieren das genaue ST25DV-Modell nicht eindeutig. Es ist ein NFC-Speicher,
  kein physikalischer Messsensor; NFC-Zugriff per Telefon ist separat zu testen.
- **MAX31865:** benötigt einen angeschlossenen Widerstandssensor. Beispiel für
  PT100, 430-Ohm-Referenz und 3-Leiter-Anschluss. Für PT1000 beispielsweise
  `rtd_nominal_ohm: 1000`, `reference_ohm: 4300` einstellen, sofern diese Referenz
  tatsächlich auf dem Modul verbaut ist. `wires` muss zur Verdrahtung und zu den
  Hardwarebrücken passen. 50-Hz-Filter ist aktiv. Der Chip hat keine eindeutige
  ID; eine plausible Temperatur ohne Fehler ist nur eine Funktionsprüfung.

## JSON-Vertrag / KI-Vorlage

Die vier Dateien unter `sd/sensors` sind die Ausgangsvorlagen. Pflichtfelder:
`schema_version` (1), `name`, `sensor_type`, `group`, `driver`, `interface`.
`name` und `group` müssen 1–40 Zeichen lang sein. `interval_ms` liegt zwischen
100 und 60000 (Standard 1000). Adressen sind **dezimal und 7 Bit**: 41 bzw. 83.
Treiber und Schnittstelle müssen zusammenpassen:

| driver | interface | parameters |
|---|---|---|
| `vl6180x` | `type: i2c`, `address: 41` | `{}`; Einzelmessung, Standardintervall 500 ms im Beispiel |
| `vl53l4cd` | `type: i2c`, `address: 41` | `timing_budget_ms`: 10–200, Standard 50 |
| `st25dv16` | `type: i2c`, `address: 83` | `read_offset`: 0–2047, `read_length`: 1–16; Summe höchstens 2048 |
| `max31865` | `type: spi`, `cs_pin: 14` | `wires`: 2/3/4, `rtd_nominal_ohm`: >0 bis 2000, `reference_ohm`: größer als nominal, höchstens 10000 |

KI-Arbeitsanweisung: Aus dem Herstellerdatenblatt ein Profil nach einer dieser
Vorlagen erstellen. Keine neuen Treibernamen, Registerprogramme oder Pinbelegungen
erfinden. Modell, elektrische Anforderungen, Referenzwiderstand und Verdrahtung
gegen das konkrete Breakout prüfen. Fehlende Angaben als offene Fragen melden.
Generierte Profile vor Hardwarebetrieb überprüfen. Geprüfte Profile und
Korrekturen versionieren; diese Firmware lernt nicht selbstständig.

## Validierung

Ein erfolgreicher Build prüft die Übersetzbarkeit für den Core2. Hardwaretests
stehen ohne angeschlossenes Gerät aus. Auf Hardware prüfen: Display/Touch, SD
mit gültigem und ungültigem Profil, jeden Sensor einzeln, Start/Stopp,
Sensor abziehen während Messung und Kartenwechsel mit anschließendem SD-Neuladen.
Abstand mit Referenzstrecke und Temperatur mit bekanntem Widerstand prüfen.

### Stand der Cloud-Prüfung

Die vier JSON-Beispiele wurden erfolgreich geparst. Nach der Korrektur des
ArduinoJson-7-Proxyzugriffs in `loadProfile` wurde `pio run -j 4` für
`m5stack-core2` erfolgreich ausgeführt und `firmware.bin` erzeugt.
Hardwaretests und Upload auf einen echten Core2 stehen weiterhin aus.
