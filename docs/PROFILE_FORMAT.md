# Profilformat 2

Pro Sensor eine UTF-8-JSON in `/sensors/*.json`. Höchstens 24 Profile, je 32 KiB.
Nur geprüfte Profile einsetzen: Initialisierung kann Register schreiben.
Die Firmware prüft die Struktur und Laufzeitgrenzen, nicht die elektrische oder
fachliche Richtigkeit der Angaben.

## Pflichtfelder

- `schema_version`: Zahl 2.
- `name`, `sensor_type`, `group`: nicht leere Texte, höchstens 48 Bytes.
- `interval_ms`: Ganzzahl 100..60000; Messintervall, keine Hardware-Timingvorgabe.
- `interface`: siehe unten.
- `constants`: Objekt mit höchstens 32 endlichen Zahlen; Variablennamen höchstens
  24 Bytes. Beim Start und vor jeder Messung in den Variablenspeicher kopiert.
- `sequences`: Arrays `detect`, `init`, `measure`, `stop`; init/stop dürfen leer sein.
- `outputs`: 1..8 Objekte mit `label`, `var`, `unit`.

`notes`, `sources` und `electrical` sind zusätzliche Dokumentation. Sie werden
nicht als Hardwarebefehle ausgeführt. Die bisherigen Treibernamen entfallen.

I2C: `{"type":"i2c","address":41,"register_bytes":2}`.
Adressen sind dezimale **7-Bit-Adressen** (8..119). Registeradresse wird MSB zuerst
gesendet, mit 1 oder 2 Bytes. Lesen nutzt einen Repeated Start.

SPI: `{"type":"spi","cs_pin":14,"mode":1,"frequency_hz":1000000,
"register_bytes":1,"write_mask":128,"read_mask":0}`.
Mode 0..3, 10 kHz..10 MHz, MSB zuerst. Die Maske wird auf das erste Registerbyte
geodert. CS bleibt während Register und Nutzdaten aktiv. Keine beliebigen
GPIO-Adressen, Pinumschaltungen, CS- oder Raw-Command-Sequenzen in Version 2.

## Automatische Erkennung

Optionales Feld `identification`: `model_id` oder `response` (Standard).
`model_id` nur verwenden, wenn `detect` eine verlässliche Modellkennung liest
und mittels `assert` überprüft. Sonst `response`: das Display kennzeichnet den
Treffer als plausibel, nicht als eindeutig identifiziert. Das Feld ergänzt nur
die Aussagekraft; alle Register und Prüfschritte bleiben in `detect`.

Der Autoscan lädt die Profile regelmäßig und führt ausschließlich ihre lesenden
Erkennungssequenzen aus. Ein eindeutiger Treffer wird ausgewählt und schaltet den Autoscan aus. Bei
mehreren Treffern erfolgt keine automatische Auswahl. Kein Treffer löscht die
automatische Auswahl. Messungen starten weiterhin über Start. Nach Stop oder
Messfehler bleibt die Suche aus. Ein neuer Autoscan muss über die Taste gestartet
werden; dabei werden auch die SD-Dateien neu geladen. Die Sensor-Taste blättert
manuell durch alle geladenen Profile und beendet die Suche.

## Operationen

Alle Register und Bytewerte sind JSON-Zahlen (dezimal, keine Hex-Strings).
Beispiele benutzen frei gewählte Register; keine Vorlage direkt an unbekannter
Hardware ausführen.

| op | Felder | Wirkung |
|---|---|---|
| `read` | `reg`, `bytes` (1..4), `var` | Wert in Variable lesen |
| `write` | `reg`, `bytes` (1..4), `value` (Zahl oder Ausdruck) | Ganzzahl in Register schreiben |
| `write` | `reg`, `data` (1..24 Bytewerte) | Konfigurationsblock schreiben |
| `poll` | `reg`, `bytes`, `mask`, `equals`, `timeout_ms` (1..2000) | wiederholt lesen bis `(Wert & mask) == equals` |
| `delay` | `ms` (0..2000) | warten |
| `calc` | `var`, `expr` | Formel berechnen |
| `assert` | `expr`, optional `message` | bei 0 abbrechen; Nachricht anzeigen |
| `if` | `expr`, `then`, optional `else` | Zweig ausführen |
| `while` | `expr`, `max_iterations` (1..32), `steps` | begrenzte Schleife |

`read`, `write` und `poll` unterstützen optional `endian`: `big` (Standard) oder
`little`. Nur `read` verwendet optional `signed: true` für Zweierkomplement.
Bitmasken arbeiten auf uint32-Werten. `poll` als unsigned verwenden.

`detect` ist ausschließlich lesend: `write` und `delay` sind auch in Zweigen
verboten. Erkennung nicht durch Initialisierung erzwingen. Antwortet ein Sensor
ohne Modellerkennung, diese Unsicherheit in `notes` angeben.

Alle Sequenzen werden vor Ausführung geprüft. Grenzen: 512 definierte Schritte
über alle Sequenzen, 2048 ausgeführte Schritte je Aufruf, 4 Verschachtelungsebenen,
64 Variablen und 10 Sekunden je Sequenz (inkl. begrenzter Buswartezeiten).
Bei Messfehler wird kein gültiger Messwert angezeigt; Firmware versucht `stop`.
Stoppsequenzen möglichst ohne zusätzliche Voraussetzungen formulieren, damit
sie auch nach einer teilweise fehlgeschlagenen Initialisierung funktionieren.

## Formeln (RPN)

Eine Zahl oder ein Array in umgekehrter polnischer Notation. Variablen werden
mit `$` referenziert. Beispiel: `["$raw", 0.01, "*", -40, "+"]` bedeutet
`raw * 0.01 - 40`. `value` bei write unterstützt dieselbe Ausdruckssprache.

- Arithmetik: `+`, `-`, `*`, `/`, `pow`; `sqrt`, `floor` sind einstellig.
- Bits: `&`, `|`, `<<`, `>>`; Operanden müssen uint32 sein, Shift 0..31.
- Vergleiche: `eq`, `lt`, `gt`, `le`, `ge` liefern 0 oder 1.

Keine eval-Funktion und kein C++ oder JavaScript in Profilen. Ausdrücke haben
maximal 128 Tokens und 32 Stackeinträge. Unbekannte Variablen, Division durch
null, negative Wurzeln und nicht endliche Ergebnisse brechen ab.

Der Variablenspeicher wird vor **jeder Messung auf die Konstanten zurückgesetzt**.
Kalibrierregister, die für eine Formel gebraucht werden, deshalb in `measure`
lesen. Ergebnisse aus `detect`/`init` nicht als dauerhaft annehmen.
Jeder Kanal unter `outputs` muss von dieser Messsequenz erzeugt worden sein.

## Erweiterungsgrenzen

UART, CRC, Analogmessung, Streaming und GPIO-Handshakes fehlen in dieser Engine.
Ein Profil mit unbekanntem Befehl wird verworfen. Erst wenn ein Protokoll neue
allgemeine Operationen erfordert, ist eine Firmwareerweiterung nötig. Für neue
registerbasierte I2C-/SPI-Geräte mit vorhandenen Operationen genügt die SD-Datei.
