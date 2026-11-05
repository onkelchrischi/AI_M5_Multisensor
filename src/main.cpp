#include <M5Unified.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_MAX31865.h>
#include <vl53l4cd_class.h>
#include <vector>
#include <memory>
#include <math.h>

// External Port A only: never scan the internal PMIC/touch I2C bus.
TwoWire sensorBus(0);
VL53L4CD tof(&sensorBus, -1);
struct Profile {
  String name, type, group, driver;
  uint8_t address = 0;
  uint32_t interval = 1000;
  int budget = 50, offset = 0, length = 8, wires = 3;
  float nominal = 100, reference = 430;
};
std::vector<Profile> profiles;
std::unique_ptr<Adafruit_MAX31865> rtd;
size_t selected = 0;
bool running = false, sdReady = false;
uint32_t lastSample = 0, started = 0;
String message = "SD-Karte einlegen", value = "-";

void draw() {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK); d.setTextColor(TFT_WHITE); d.setTextSize(2);
  d.setCursor(8, 8); d.println("Sensor Test / Core2");
  if (!profiles.empty()) {
    const auto &p = profiles[selected];
    d.setCursor(8, 40); d.println(p.name);
    d.setCursor(8, 65); d.println(p.group);
    d.setCursor(8, 90); d.println(p.type);
  }
  d.setCursor(8, 120); d.setTextColor(TFT_CYAN); d.println(value);
  d.setTextSize(1); d.setTextColor(TFT_WHITE);
  d.setCursor(8, 163); d.println(message);
  d.setCursor(8, 185); d.println(running ? "Messung aktiv" : "Gestoppt");
  d.drawFastHLine(0, 205, 320, TFT_WHITE);
  d.setTextSize(2); d.setCursor(5, 215); d.print("Weiter Start/Stop SD");
}
void stop() {
  if (running && !profiles.empty() && profiles[selected].driver == "vl53l4cd")
    tof.VL53L4CD_StopRanging();
  running = false;
}
bool ack(uint8_t address) {
  sensorBus.beginTransmission(address);
  return sensorBus.endTransmission() == 0;
}
bool loadProfile(File &file, Profile &p) {
  if (file.size() > 8192) return false;
  JsonDocument doc;
  if (deserializeJson(doc, file)) return false;
  if (doc["schema_version"].as<int>() != 1 ||
      !doc["name"].is<const char*>() || !doc["sensor_type"].is<const char*>() ||
      !doc["group"].is<const char*>() || !doc["driver"].is<const char*>()) return false;
  p.name = doc["name"].as<String>(); p.type = doc["sensor_type"].as<String>();
  p.group = doc["group"].as<String>(); p.driver = doc["driver"].as<String>();
  if (!p.name.length() || p.name.length() > 40 || !p.type.length() ||
      !p.group.length() || p.group.length() > 40) return false;
  int interval = doc["interval_ms"] | 1000;
  if (interval < 100 || interval > 60000) return false;
  p.interval = interval;
  String bus = doc["interface"]["type"] | "";
  if (p.driver == "vl53l4cd" || p.driver == "st25dv16") {
    if (bus != "i2c") return false;
    int address = doc["interface"]["address"] | -1;
    // Initial prototype only supports known default addresses.
    if (address != (p.driver == "vl53l4cd" ? 0x29 : 0x53)) return false;
    p.address = address;
  } else if (p.driver == "max31865") {
    if (bus != "spi" || (doc["interface"]["cs_pin"] | -1) != 14) return false;
  } else return false;
  auto params = doc["parameters"];
  p.budget = params["timing_budget_ms"] | 50;
  p.offset = params["read_offset"] | 0; p.length = params["read_length"] | 8;
  p.wires = params["wires"] | 3;
  p.nominal = params["rtd_nominal_ohm"] | 100.0f;
  p.reference = params["reference_ohm"] | 430.0f;
  return p.budget >= 10 && p.budget <= 200 && p.offset >= 0 &&
    p.length >= 1 && p.length <= 16 && p.offset + p.length <= 2048 &&
    p.wires >= 2 && p.wires <= 4 && isfinite(p.nominal) && isfinite(p.reference) &&
    p.nominal > 0 && p.nominal <= 2000 && p.reference > p.nominal && p.reference <= 10000;
}
void scanSD() {
  stop(); rtd.reset(); profiles.clear(); selected = 0; value = "-";
  SD.end(); sdReady = SD.begin(4, SPI, 10000000);
  unsigned rejected = 0;
  if (sdReady) {
    File dir = SD.open("/sensors");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".json")) {
          Profile p;
          if (profiles.size() < 24 && loadProfile(f, p)) profiles.push_back(p);
          else { ++rejected; Serial.printf("Profil ungueltig: %s\n", f.name()); }
        }
        f.close();
      }
    }
    message = String(profiles.size()) + " Profile; verworfen: " + rejected;
  } else message = "SD fehlt / nicht lesbar (FAT32)";
  draw();
}
void start() {
  if (profiles.empty()) return;
  auto &p = profiles[selected]; value = "-"; bool ok = false;
  if (p.driver == "vl53l4cd") {
    tof.begin();
    ok = ack(p.address) && tof.InitSensor() == 0 &&
      tof.VL53L4CD_SetRangeTiming(p.budget, 0) == 0 &&
      tof.VL53L4CD_StartRanging() == 0;
  } else if (p.driver == "st25dv16") {
    ok = ack(p.address);
  } else {
    rtd.reset(new Adafruit_MAX31865(14, &SPI));
    auto mode = p.wires == 2 ? MAX31865_2WIRE : p.wires == 3 ? MAX31865_3WIRE : MAX31865_4WIRE;
    rtd->begin(mode); rtd->enable50Hz(true);
    // No chip-ID exists; require a plausible RTD read and no fault.
    float temp = rtd->temperature(p.nominal, p.reference);
    ok = rtd->readFault() == 0 && isfinite(temp) && temp >= -200 && temp <= 850;
  }
  running = ok; started = millis(); lastSample = millis() - p.interval;
  message = ok ? "Gestartet" : "Nicht erkannt / Anschluss oder RTD pruefen";
  draw();
}
void sample() {
  const auto &p = profiles[selected];
  if (p.driver == "vl53l4cd") {
    uint8_t ready = 0;
    if (tof.VL53L4CD_CheckForDataReady(&ready)) { value = "I2C Fehler"; }
    else if (!ready) {
      if (millis() - started > 2000) { value = "Timeout"; stop(); }
      else return;
    } else {
      VL53L4CD_Result_t result{};
      if (tof.VL53L4CD_GetResult(&result) || tof.VL53L4CD_ClearInterrupt()) value = "Lesefehler";
      else if (result.range_status != 0) value = "Range Status " + String(result.range_status);
      else value = String(result.distance_mm) + " mm";
      started = millis();
    }
  } else if (p.driver == "st25dv16") {
    // Read-only EEPROM probe. ACK + bytes does not prove the exact chip identity.
    sensorBus.beginTransmission(p.address);
    sensorBus.write(uint8_t(p.offset >> 8)); sensorBus.write(uint8_t(p.offset));
    if (sensorBus.endTransmission(false) ||
        sensorBus.requestFrom(p.address, uint8_t(p.length)) != p.length) value = "I2C Lesefehler";
    else {
      value = "";
      while (sensorBus.available()) {
        char hex[4]; snprintf(hex, sizeof(hex), "%02X ", sensorBus.read()); value += hex;
      }
    }
    message = "EEPROM bytes; Modell nicht eindeutig bestaetigt";
  } else {
    float temp = rtd->temperature(p.nominal, p.reference);
    uint8_t fault = rtd->readFault();
    if (fault || !isfinite(temp) || temp < -200 || temp > 850) {
      value = "RTD Fehler " + String(fault, HEX); rtd->clearFault();
    } else value = String(temp, 2) + " C";
  }
  Serial.printf("%lu,%s,%s\n", millis(), p.type.c_str(), value.c_str());
  draw();
}
void setup() {
  auto cfg = M5.config(); M5.begin(cfg);
  Serial.begin(115200); M5.Display.setRotation(1);
  pinMode(14, OUTPUT); digitalWrite(14, HIGH);
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  SPI.begin(18, 38, 23);
  sensorBus.begin(32, 33, 100000); sensorBus.setTimeOut(50);
  scanSD();
}
void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasPressed() && t.y >= 205) {
    if (t.x < 100) {
      stop(); if (!profiles.empty()) selected = (selected + 1) % profiles.size();
      value = "-"; message = "Profil gewaehlt; Start prueft Anschluss"; draw();
    } else if (t.x < 245) {
      if (running) { stop(); message = "Gestoppt"; draw(); } else start();
    } else scanSD();
  }
  if (running && millis() - lastSample >= profiles[selected].interval) {
    lastSample = millis(); sample();
  }
  delay(5);
}
