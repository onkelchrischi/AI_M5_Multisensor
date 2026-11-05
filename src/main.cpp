#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "ProtocolEngine.h"
#include "AutoScan.h"
#include <memory>
#include <vector>

// Board wiring is fixed; all device protocol details come from SD profiles.
TwoWire sensorBus(0);
class BoardBus : public ProtocolBus {
  bool spi = false;
  uint8_t address = 0, registerBytes = 2, readMask = 0, writeMask = 0, mode = 0;
  uint32_t frequency = 1000000;
  bool addressValid(uint16_t reg) { return registerBytes == 2 || reg <= 255; }
  void sendRegister(uint16_t reg, bool writing) {
    uint8_t hi = reg >> 8, lo = reg;
    if (spi) {
      if (registerBytes == 2) { SPI.transfer(hi | (writing ? writeMask : readMask)); SPI.transfer(lo); }
      else SPI.transfer(lo | (writing ? writeMask : readMask));
    } else {
      if (registerBytes == 2) sensorBus.write(hi);
      sensorBus.write(lo);
    }
  }
public:
  void configure(JsonObjectConst config) {
    spi = std::string(config["type"] | "") == "spi";
    address = config["address"] | 0; registerBytes = config["register_bytes"] | 2;
    mode = config["mode"] | 0; frequency = config["frequency_hz"] | 1000000;
    readMask = config["read_mask"] | 0; writeMask = config["write_mask"] | 0;
  }
  bool read(uint16_t reg, uint8_t *bytes, size_t n) override {
    if (!addressValid(reg) || n < 1 || n > 4) return false;
    if (spi) {
      SPI.beginTransaction(SPISettings(frequency, MSBFIRST, mode));
      digitalWrite(14, LOW); sendRegister(reg, false);
      for (size_t i = 0; i < n; ++i) bytes[i] = SPI.transfer(0);
      digitalWrite(14, HIGH); SPI.endTransaction(); return true;
    }
    sensorBus.beginTransmission(address); sendRegister(reg, false);
    if (sensorBus.endTransmission(false) || sensorBus.requestFrom(address, uint8_t(n)) != n) return false;
    for (size_t i = 0; i < n; ++i) bytes[i] = sensorBus.read();
    return true;
  }
  bool write(uint16_t reg, const uint8_t *bytes, size_t n) override {
    if (!addressValid(reg) || n < 1 || n > 24) return false;
    if (spi) {
      SPI.beginTransaction(SPISettings(frequency, MSBFIRST, mode));
      digitalWrite(14, LOW); sendRegister(reg, true);
      for (size_t i = 0; i < n; ++i) SPI.transfer(bytes[i]);
      digitalWrite(14, HIGH); SPI.endTransaction(); return true;
    }
    sensorBus.beginTransmission(address); sendRegister(reg, true);
    sensorBus.write(bytes, n); return sensorBus.endTransmission() == 0;
  }
  uint32_t now() override { return millis(); }
  void wait(uint32_t ms) override { delay(ms); }
} bus;
ProtocolEngine engine(bus);
struct Profile { String path, name, type, group; bool modelId; };
AutoScan autoScan;
bool sensorSelected = false;
unsigned rejectedProfiles = 0;
std::vector<Profile> profiles;
std::unique_ptr<JsonDocument> active;
size_t selected = 0;
bool running = false;
uint32_t lastSample = 0;
String message, value = "-";

void draw() {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK); d.setTextColor(TFT_WHITE); d.setTextSize(2);
  d.setCursor(8, 8); d.println("Sensor JSON Engine v2");
  if (sensorSelected && !profiles.empty()) {
    const auto &p = profiles[selected];
    d.setCursor(8, 40); d.println(p.name);
    d.setCursor(8, 65); d.println(p.group);
    d.setTextSize(1); d.setCursor(8, 92); d.println(p.type);
  } else {
    d.setCursor(8, 45); d.println("Kein Sensor erkannt");
  }
  d.setTextSize(1); d.setTextColor(TFT_CYAN);
  d.setCursor(8, 112); d.println(value);
  d.setTextColor(TFT_WHITE); d.setCursor(8, 165); d.println(message);
  d.setCursor(8, 190); d.println(running ? "Sensorauswertung aktiv" :
    autoScan.enabled ? "Autoscan EIN (alle 3 s)" : "Autoscan AUS");
  const char *top[] = {"Autoscan", running ? "Stop" : "Start", "Sensor"};
  const char *bottom[] = {autoScan.enabled ? "EIN" : "AUS", "Auswertung", "durchblaettern"};
  d.setTextSize(1);
  for (unsigned i = 0; i < 3; ++i) {
    int left = i * 320 / 3, right = (i + 1) * 320 / 3;
    d.drawRect(left, 205, right - left, 35, TFT_WHITE);
    d.setCursor(left + 8, 212); d.print(top[i]);
    d.setCursor(left + 8, 225); d.print(bottom[i]);
  }
}
bool load(const String &path, JsonDocument &doc) {
  File f = SD.open(path);
  if (!f || f.size() > 32768 || deserializeJson(doc, f)) {
    message = "Profil fehlt / JSON ungueltig"; return false;
  }
  if (!engine.validate(doc.as<JsonObjectConst>())) {
    message = engine.error.c_str(); return false;
  }
  return true;
}
void prepare(JsonDocument &doc) {
  bus.configure(doc["interface"].as<JsonObjectConst>());
  engine.reset(doc["constants"].as<JsonObjectConst>());
}
bool sequence(const char *name, bool readOnly = false) {
  if (!engine.run((*active)["sequences"][name].as<JsonArrayConst>(), readOnly)) {
    message = String(name) + ": " + engine.error.c_str(); return false;
  }
  return true;
}
void stop() {
  if (running && active) {
    if (!sequence("stop")) Serial.println(message);
  }
  running = false;
}
void scanTick() {
  // Never change the bus or profile while a measurement is active.
  if (running || profiles.empty() || !autoScan.enabled) return;
  bool complete = autoScan.tick(millis(), profiles.size(), [](size_t i) {
    JsonDocument doc;
    String previousMessage = message;
    if (!load(profiles[i].path, doc)) {
      Serial.println("Autoscan: " + profiles[i].path + ": " + message);
      message = previousMessage; return false;
    }
    prepare(doc);
    bool match = engine.run(doc["sequences"]["detect"].as<JsonArrayConst>(), true);
    message = previousMessage;
    return match;
  });
  if (!complete) return;
  bool wasSelected = sensorSelected;
  size_t previousSelection = selected;
  String previousMessage = message;
  sensorSelected = autoScan.matches.size() == 1;
  if (sensorSelected) {
    selected = autoScan.matches.front();
    message = profiles[selected].modelId ? "Sensor erkannt (Modell-ID); Start" :
      "Profil plausibel; Modell nicht bestaetigt";
  } else if (autoScan.matches.empty()) {
    message = "Kein Profil antwortet; Anschluss / SD pruefen";
  } else {
    message = String(autoScan.matches.size()) + " Treffer: Sensor durchblaettern";
  }
  if (rejectedProfiles) message += "; ungueltige JSON: " + String(rejectedProfiles);
  if (wasSelected != sensorSelected || previousSelection != selected || message != previousMessage) {
    value = "-"; Serial.println("Autoscan: " + message);
    for (size_t index : autoScan.matches) Serial.println("  Treffer: " + profiles[index].name);
    draw();
  }
}
void scanSD() {
  stop(); active.reset(); profiles.clear(); selected = 0; sensorSelected = false; value = "-";
  autoScan.matches.clear(); autoScan.enable(millis()); rejectedProfiles = 0;
  SD.end();
  if (!SD.begin(4, SPI, 10000000)) { message = "SD fehlt / FAT32 pruefen"; draw(); return; }
  File dir = SD.open("/sensors");
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory() && String(f.name()).endsWith(".json")) {
        String path = f.path(); f.close(); JsonDocument doc;
        if (profiles.size() < 24 && load(path, doc)) {
          profiles.push_back({path, doc["name"].as<String>(), doc["sensor_type"].as<String>(), doc["group"].as<String>(),
            String(doc["identification"] | "response") == "model_id"});
        } else { ++rejectedProfiles; Serial.println(path + ": " + message); }
      }
      f.close();
    }
  }
  message = profiles.empty() ? "Keine gueltigen Profile auf SD" : "Autoscan sucht Sensor...";
  if (rejectedProfiles) message += "; ungueltige JSON: " + String(rejectedProfiles);
  draw();
}
void start() {
  if (profiles.empty() || !sensorSelected) {
    message = "Erst Sensor erkennen / Profil waehlen"; draw(); return;
  }
  autoScan.disable();
  active.reset(new JsonDocument());
  if (!load(profiles[selected].path, *active)) { active.reset(); draw(); return; }
  prepare(*active);
  if (!sequence("detect", true)) { active.reset(); draw(); return; }
  if (!sequence("init")) {
    String cause = message; sequence("stop"); message = cause; active.reset(); draw(); return;
  }
  running = true;
  lastSample = millis() - ((*active)["interval_ms"].as<uint32_t>());
  message = "Profil gestartet"; value = "-"; draw();
}
void sample() {
  // Clear previous sample values: an output must be produced in this run.
  engine.reset((*active)["constants"].as<JsonObjectConst>());
  if (!sequence("measure")) {
    String cause = message; value = "Messfehler"; stop(); message = cause; draw(); return;
  }
  value = "";
  for (JsonObjectConst output : (*active)["outputs"].as<JsonArrayConst>()) {
    const char *name = output["var"];
    auto it = engine.variables.find(name);
    if (it == engine.variables.end()) {
      value = "Ausgabevariable fehlt"; stop(); draw(); return;
    }
    value += String(output["label"].as<const char*>()) + ": " + String(it->second, 3) + " " +
      output["unit"].as<const char*>() + "\n";
    Serial.printf("%lu,%s,%s,%.6f,%s\n", millis(), profiles[selected].type.c_str(), name,
      it->second, output["unit"].as<const char*>());
  }
  message = "Messung OK"; draw();
}
void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1);
  pinMode(14, OUTPUT); digitalWrite(14, HIGH);
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  SPI.begin(18, 38, 23);
  sensorBus.begin(32, 33, 100000); sensorBus.setTimeOut(50);
  scanSD();
}
void loop() {
  M5.update(); auto touch = M5.Touch.getDetail();
  if (touch.wasPressed() && touch.y >= 205) {
    if (touch.x < 320 / 3) {
      if (autoScan.enabled) {
        autoScan.disable(); message = "Autoscan ausgeschaltet"; draw();
      } else {
        // A new search also reloads SD profiles; replaces the old fourth button.
        scanSD();
      }
    } else if (touch.x < 2 * 320 / 3) {
      if (running) { stop(); message = "Gestoppt"; draw(); } else start();
    } else {
      stop(); active.reset(); autoScan.disable();
      if (!profiles.empty()) {
        selected = sensorSelected ? (selected + 1) % profiles.size() : 0;
        sensorSelected = true;
        message = "Profil gewaehlt; Start prueft Anschluss";
      } else message = "Keine Profile; Autoscan zum SD-Neuladen";
      value = "-"; draw();
    }
  }
  if (running && millis() - lastSample >= (*active)["interval_ms"].as<uint32_t>()) {
    lastSample = millis(); sample();
  }
  scanTick();
  delay(5);
}
