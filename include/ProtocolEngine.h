#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Portable interpreter: no sensor names, registers or conversion formulas here.
class ProtocolBus {
public:
  virtual ~ProtocolBus() = default;
  virtual bool read(uint16_t reg, uint8_t *bytes, size_t count) = 0;
  virtual bool write(uint16_t reg, const uint8_t *bytes, size_t count) = 0;
  virtual uint32_t now() = 0;
  virtual void wait(uint32_t ms) = 0;
};
class ProtocolEngine {
public:
  explicit ProtocolEngine(ProtocolBus &b) : bus(b) {}
  std::map<std::string, double> variables;
  std::string error;
  bool validate(JsonObjectConst profile);
  bool run(JsonArrayConst steps, bool readOnly = false) {
    error.clear(); count = 0; validationCount = 0;
    if (!checkSteps(steps, readOnly, 0)) return false;
    began = bus.now();
    return execute(steps, readOnly, 0);
  }
  void reset(JsonObjectConst constants) {
    variables.clear();
    for (JsonPairConst item : constants) variables[item.key().c_str()] = item.value().as<double>();
  }
private:
  ProtocolBus &bus;
  unsigned count = 0, validationCount = 0;
  uint32_t began = 0;
  bool fail(const std::string &why) { error = why; return false; }
  bool checkSteps(JsonArrayConst steps, bool readOnly, unsigned depth);
  bool execute(JsonArrayConst steps, bool readOnly, unsigned depth);
  bool eval(JsonVariantConst expression, double &result, bool syntaxOnly = false);
  bool store(const char *name, double value) {
    if (!name || !*name || std::string(name).size() > 24 || !std::isfinite(value))
      return fail("Ungueltige Variable / Zahl");
    if (variables.find(name) == variables.end() && variables.size() >= 64)
      return fail("Zu viele Variablen");
    variables[name] = value; return true;
  }
  bool readNumber(JsonObjectConst op, double &value) {
    uint8_t bytes[4]{};
    unsigned n = op["bytes"] | 1;
    if (!bus.read(op["reg"].as<uint16_t>(), bytes, n)) return fail("Bus-Lesefehler");
    uint32_t raw = 0;
    bool little = std::string(op["endian"] | "big") == "little";
    for (unsigned i = 0; i < n; ++i) raw = (raw << 8) | bytes[little ? n - 1 - i : i];
    value = raw;
    if ((op["signed"] | false) && (raw & (uint32_t(1) << (n * 8 - 1))))
      value -= std::pow(2.0, n * 8);
    return true;
  }
};

inline bool ProtocolEngine::eval(JsonVariantConst expression, double &result, bool syntaxOnly) {
  if (expression.is<double>()) {
    result = expression.as<double>(); return std::isfinite(result) || fail("Nicht endliche Zahl");
  }
  if (!expression.is<JsonArrayConst>() || expression.size() == 0 || expression.size() > 128)
    return fail("Ausdruck erwartet: Zahl oder RPN-Array");
  std::vector<double> stack;
  for (JsonVariantConst token : expression.as<JsonArrayConst>()) {
    if (token.is<double>()) { stack.push_back(token.as<double>()); }
    else if (token.is<const char*>()) {
      std::string s = token.as<const char*>();
      if (s.size() > 1 && s[0] == '$') {
        auto it = variables.find(s.substr(1));
        if (!syntaxOnly && it == variables.end()) return fail("Variable fehlt: " + s);
        stack.push_back(syntaxOnly ? 1 : it->second);
      } else {
        bool unary = s == "sqrt" || s == "floor";
        bool binary = s == "+" || s == "-" || s == "*" || s == "/" || s == "pow" ||
          s == "&" || s == "|" || s == ">>" || s == "<<" || s == "eq" ||
          s == "lt" || s == "gt" || s == "le" || s == "ge";
        if (!unary && !binary) return fail("Unbekannter Rechenoperator: " + s);
        if (stack.size() < (unary ? 1u : 2u)) return fail("RPN-Stapel zu klein");
        double b = stack.back(); stack.pop_back();
        double a = unary ? 0 : stack.back();
        if (!unary) stack.pop_back();
        double x = 1;
        if (!syntaxOnly) {
          if (s == "+") x = a + b;
          else if (s == "-") x = a - b;
          else if (s == "*") x = a * b;
          else if (s == "/") { if (b == 0) return fail("Division durch null"); x = a / b; }
          else if (s == "pow") x = std::pow(a, b);
          else if (s == "sqrt") { if (b < 0) return fail("Negative Wurzel"); x = std::sqrt(b); }
          else if (s == "floor") x = std::floor(b);
          else if (s == "eq") x = a == b;
          else if (s == "lt") x = a < b;
          else if (s == "gt") x = a > b;
          else if (s == "le") x = a <= b;
          else if (s == "ge") x = a >= b;
          else {
            if (a < 0 || a > UINT32_MAX || b < 0 || b > UINT32_MAX ||
                std::floor(a) != a || std::floor(b) != b) return fail("Bitoperation erwartet uint32");
            uint32_t aa = uint32_t(a), bb = uint32_t(b);
            if ((s == ">>" || s == "<<") && bb > 31) return fail("Ungueltiger Shift");
            if (s == "&") x = aa & bb;
            if (s == "|") x = aa | bb;
            if (s == ">>") x = aa >> bb;
            if (s == "<<") x = aa << bb;
          }
        }
        if (!std::isfinite(x)) return fail("Ungueltiges Rechenergebnis");
        stack.push_back(x);
      }
    } else return fail("Ungueltiges RPN-Token");
    if (stack.size() > 32 || (!stack.empty() && !std::isfinite(stack.back())))
      return fail("RPN-Stapel / Zahl ungueltig");
  }
  if (stack.size() != 1) return fail("RPN muss genau einen Wert liefern");
  result = stack.back(); return true;
}

inline bool ProtocolEngine::checkSteps(JsonArrayConst steps, bool readOnly, unsigned depth) {
  if (steps.isNull() || depth > 4) return fail("Sequenz fehlt / zu tief verschachtelt");
  for (JsonVariantConst v : steps) {
    if (++validationCount > 512 || !v.is<JsonObjectConst>()) return fail("Sequenz zu gross / ungueltig");
    JsonObjectConst op = v.as<JsonObjectConst>();
    std::string type = op["op"] | "";
    double dummy;
    if (type == "read" || type == "poll" || type == "write") {
      if (!op["reg"].is<unsigned>() || op["reg"].as<unsigned>() > 65535)
        return fail("Register fehlt / ungueltig");
      if (type == "write") {
        if (readOnly) return fail("Erkennung darf nicht schreiben");
        if (op["data"].is<JsonArrayConst>()) {
          auto data = op["data"].as<JsonArrayConst>();
          if (data.size() == 0 || data.size() > 24) return fail("Schreibblock: 1..24 Bytes");
          for (auto b : data) if (!b.template is<unsigned>() || b.template as<unsigned>() > 255)
            return fail("Schreibblock enthaelt ungueltiges Byte");
        } else if (!eval(op["value"], dummy, true)) return false;
      }
      if (type != "write" || !op["data"].is<JsonArrayConst>()) {
        if (!op["bytes"].is<unsigned>() || op["bytes"].as<unsigned>() < 1 || op["bytes"].as<unsigned>() > 4)
          return fail("Wertbreite: 1..4 Bytes");
      }
      std::string endian = op["endian"] | "big";
      if (endian != "big" && endian != "little") return fail("Byte-Reihenfolge ungueltig");
      if (type == "poll") {
        if (!op["poll_interval_ms"].isNull() &&
            (!op["poll_interval_ms"].is<unsigned>() || op["poll_interval_ms"].as<unsigned>() < 1 ||
             op["poll_interval_ms"].as<unsigned>() > 100)) return fail("Pollintervall: 1..100 ms");
        if ((op["signed"] | false) || !op["mask"].is<uint32_t>() || !op["equals"].is<uint32_t>() ||
            !op["timeout_ms"].is<unsigned>() || op["timeout_ms"].as<unsigned>() < 1 ||
            op["timeout_ms"].as<unsigned>() > 2000) return fail("Poll-Parameter ungueltig");
      }
    } else if (type == "calc" || type == "assert" || type == "if" || type == "while") {
      if (!eval(op["expr"], dummy, true)) return false;
      if (type == "if") {
        if (!checkSteps(op["then"].as<JsonArrayConst>(), readOnly, depth + 1)) return false;
        if (!op["else"].isNull() && !checkSteps(op["else"].as<JsonArrayConst>(), readOnly, depth + 1)) return false;
      }
      if (type == "while") {
        if (!op["max_iterations"].is<unsigned>() || op["max_iterations"].as<unsigned>() < 1 ||
            op["max_iterations"].as<unsigned>() > 32) return fail("Schleifenlimit fehlt");
        if (!checkSteps(op["steps"].as<JsonArrayConst>(), readOnly, depth + 1)) return false;
      }
    } else if (type == "delay") {
      if (readOnly || !op["ms"].is<unsigned>() || op["ms"].as<unsigned>() > 2000)
        return fail("Wartezeit ungueltig");
    } else return fail("Unbekannte Operation: " + type);
    if (type == "read" || type == "calc") {
      const char *name = op["var"] | "";
      if (!*name || std::string(name).size() > 24) return fail("Variablenname fehlt / zu lang");
    }
  }
  return true;
}

inline bool ProtocolEngine::validate(JsonObjectConst p) {
  error.clear(); validationCount = 0;
  if (!p["schema_version"].is<unsigned>() || p["schema_version"].as<unsigned>() != 2)
    return fail("Nur Profilformat 2 unterstuetzt");
  for (const char *key : {"name", "sensor_type", "group"}) {
    const char *s = p[key] | "";
    if (!*s || std::string(s).size() > 48) return fail("Name, Sensortyp oder Gruppe fehlt / zu lang");
  }
  if (!p["interval_ms"].is<unsigned>() ||
      p["interval_ms"].as<unsigned>() > 60000) return fail("Messintervall: 0..60000 ms");
  JsonObjectConst b = p["interface"].as<JsonObjectConst>();
  std::string type = b["type"] | "";
  if (type == "i2c") {
    if (!b["address"].is<unsigned>() || b["address"].as<unsigned>() < 8 ||
        b["address"].as<unsigned>() > 119) return fail("Ungueltige I2C-Adresse");
  } else if (type == "spi") {
    if (b["cs_pin"].as<int>() != 14 || !b["mode"].is<unsigned>() || b["mode"].as<unsigned>() > 3 ||
        !b["frequency_hz"].is<unsigned>() || b["frequency_hz"].as<unsigned>() < 10000 ||
        b["frequency_hz"].as<unsigned>() > 10000000 || !b["write_mask"].is<unsigned>() ||
        b["write_mask"].as<unsigned>() > 255 || !b["read_mask"].is<unsigned>() ||
        b["read_mask"].as<unsigned>() > 255) return fail("Ungueltige SPI-Konfiguration");
  } else return fail("Schnittstelle nicht unterstuetzt (I2C/SPI)");
  if (!b["register_bytes"].is<unsigned>() || b["register_bytes"].as<unsigned>() < 1 ||
      b["register_bytes"].as<unsigned>() > 2) return fail("Registeradresse: 1..2 Bytes");
  if (!p["constants"].is<JsonObjectConst>() || p["constants"].size() > 32) return fail("Konstanten ungueltig");
  for (JsonPairConst item : p["constants"].as<JsonObjectConst>()) {
    if (std::string(item.key().c_str()).size() > 24 || !item.value().is<double>() ||
        !std::isfinite(item.value().as<double>())) return fail("Konstante ungueltig");
  }
  auto sequences = p["sequences"].as<JsonObjectConst>();
  for (const char *key : {"detect", "init", "measure", "stop"})
    if (!checkSteps(sequences[key].as<JsonArrayConst>(), std::string(key) == "detect", 0)) return false;
  if (sequences["detect"].size() == 0 || sequences["measure"].size() == 0)
    return fail("Erkennungs-/Messsequenz leer");
  auto outputs = p["outputs"].as<JsonArrayConst>();
  if (outputs.isNull() || outputs.size() < 1 || outputs.size() > 8) return fail("Ausgabekanaele: 1..8");
  for (JsonObjectConst out : outputs) {
    if (!out["var"].is<const char*>() || !out["label"].is<const char*>() || !out["unit"].is<const char*>())
      return fail("Ausgabekanal ungueltig");
  }
  return true;
}

inline bool ProtocolEngine::execute(JsonArrayConst steps, bool readOnly, unsigned depth) {
  if (depth > 4) return fail("Verschachtelungslimit");
  for (JsonObjectConst op : steps) {
    if (++count > 2048 || bus.now() - began > 10000) return fail("Sequenzlimit / Timeout");
    std::string type = op["op"] | "";
    double x = 0;
    if (type == "read") {
      if (!readNumber(op, x) || !store(op["var"], x)) return false;
    } else if (type == "write") {
      if (readOnly) return fail("Schreiben waehrend Erkennung gesperrt");
      uint8_t bytes[24]{}; size_t n = 0;
      if (op["data"].is<JsonArrayConst>()) {
        for (JsonVariantConst v : op["data"].as<JsonArrayConst>()) bytes[n++] = v.as<uint8_t>();
      } else {
        if (!eval(op["value"], x)) return false;
        n = op["bytes"].as<unsigned>();
        if (x < 0 || x >= std::pow(2.0, n * 8) || std::floor(x) != x) return fail("Schreibwert ausserhalb Bytebreite");
        uint32_t raw = uint32_t(x);
        bool little = std::string(op["endian"] | "big") == "little";
        for (size_t i = 0; i < n; ++i) { bytes[little ? i : n - 1 - i] = raw & 255; raw >>= 8; }
      }
      if (!bus.write(op["reg"].as<uint16_t>(), bytes, n)) return fail("Bus-Schreibfehler");
    } else if (type == "poll") {
      uint32_t since = bus.now();
      do {
        if (!readNumber(op, x)) return false;
        if ((uint32_t(x) & op["mask"].as<uint32_t>()) == op["equals"].as<uint32_t>()) break;
        if (bus.now() - since >= op["timeout_ms"].as<uint32_t>() || bus.now() - began > 10000)
          return fail("Poll Timeout");
        bus.wait(op["poll_interval_ms"] | 2u);
      } while (true);
    } else if (type == "delay") {
      if (readOnly) return fail("Delay waehrend Erkennung gesperrt");
      bus.wait(op["ms"].as<unsigned>());
    } else if (type == "calc") {
      if (!eval(op["expr"], x) || !store(op["var"], x)) return false;
    } else if (type == "assert") {
      if (!eval(op["expr"], x)) return false;
      if (!x) return fail(op["message"] | "Profil-Pruefung fehlgeschlagen");
    } else if (type == "if") {
      if (!eval(op["expr"], x)) return false;
      JsonArrayConst branch = op[x ? "then" : "else"].as<JsonArrayConst>();
      if (!branch.isNull() && !execute(branch, readOnly, depth + 1)) return false;
    } else if (type == "while") {
      unsigned i = 0;
      while (true) {
        if (!eval(op["expr"], x)) return false;
        if (!x) break;
        if (i++ >= op["max_iterations"].as<unsigned>()) return fail("Schleifenlimit erreicht");
        if (!execute(op["steps"].as<JsonArrayConst>(), readOnly, depth + 1)) return false;
      }
    } else return fail("Unbekannte Operation");
  }
  return bus.now() - began <= 10000 || fail("Sequenz Timeout");
}
