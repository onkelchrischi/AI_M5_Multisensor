#include "ProtocolEngine.h"
#include "AutoScan.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

struct FakeBus : ProtocolBus {
  std::map<uint16_t, uint8_t> memory;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> writes;
  uint32_t time = 0;
  bool failRead = false, failWrite = false, emulateRange = false;
  bool read(uint16_t reg, uint8_t *p, size_t n) override {
    if (failRead) return false;
    for (size_t i = 0; i < n; ++i) {
      auto it = memory.find(reg + i); if (it == memory.end()) return false;
      p[i] = it->second;
    }
    return true;
  }
  bool write(uint16_t reg, const uint8_t *p, size_t n) override {
    if (failWrite) return false;
    writes.push_back({reg, {p, p+n}});
    // Keep mocked sensor results separate from writable config registers.
    if (emulateRange && reg == 0x18 && p[0] == 1) memory[0x4f] = 4;
    return true;
  }
  uint32_t now() override { return time; }
  void wait(uint32_t ms) override { time += ms; }
  void number(uint16_t reg, uint32_t x, unsigned n) {
    for (unsigned i=0; i<n; ++i) { memory[reg+n-1-i]=x & 255; x >>= 8; }
  }
};
JsonDocument load(const std::string &file) {
  std::ifstream f(file); assert(f.good());
  std::string text((std::istreambuf_iterator<char>(f)), {});
  JsonDocument doc; assert(!deserializeJson(doc,text)); return doc;
}
bool sequence(ProtocolEngine &e, JsonDocument &p, const char *name) {
  return e.run(p["sequences"][name].as<JsonArrayConst>(), std::string(name)=="detect");
}
void require(bool ok, ProtocolEngine &e) {
  if (!ok) { std::cerr << e.error << '\n'; std::abort(); }
}
void profiles() {
  for (const char *name : {"vl6180", "vl53l4cda", "max31865", "st25dv16"}) {
    auto p=load(std::string("sd/sensors/")+name+".json"); FakeBus b; ProtocolEngine e(b);
    require(e.validate(p.as<JsonObjectConst>()),e);
    std::string kind=name;
    if (kind=="vl6180") {
      b.emulateRange=true; b.number(0,180,1); b.number(0x16,1,1);
      b.number(0x4d,1,1); b.number(0x4f,0,1); b.number(0x62,42,1);
    } else if (kind=="vl53l4cda") {
      b.number(0x10f,0xebaa,2); b.number(0xe5,3,1); b.number(0x31,0,1);
      b.number(6,4096,2); b.number(0x89,9,1); b.number(0x8c,256,2); b.number(0x96,123,2);
    } else if (kind=="max31865") {
      b.number(0,0,1); b.number(7,0,1); b.number(1,15240,2);
    } else for (unsigned i=0;i<8;++i) b.number(i,i,1);
    e.reset(p["constants"].as<JsonObjectConst>());
    require(sequence(e,p,"detect"),e); assert(b.writes.empty());
    require(sequence(e,p,"init"),e);
    e.reset(p["constants"].as<JsonObjectConst>());
    require(sequence(e,p,"measure"),e);
    if(kind=="vl6180") {
      assert(e.variables.at("distance")==42);
      for (unsigned status=1; status<=15; ++status) {
        b.number(0x4d,(status<<4)|1,1);
        e.reset(p["constants"].as<JsonObjectConst>());
        assert(!sequence(e,p,"measure"));
        assert(e.variables.at("range_status")==status);
        assert(e.error.find("VL6180 Status "+std::to_string(status)+":")==0);
        // A failed range is rejected after clearing the interrupt, so retry works.
        assert(b.writes.back().first==0x15 && b.writes.back().second[0]==7);
      }
      b.number(0x4d,1,1);
      e.reset(p["constants"].as<JsonObjectConst>());
      require(sequence(e,p,"measure"),e); assert(e.variables.at("distance")==42);
      // Move out of range and back repeatedly without stop/init in between.
      for (unsigned status : {13u, 0u, 15u, 0u, 7u, 0u}) {
        b.number(0x4d,(status<<4)|1,1);
        e.reset(p["constants"].as<JsonObjectConst>());
        bool valid = sequence(e,p,"measure");
        assert(valid == (status==0));
        if (valid) { assert(e.error.empty()); assert(e.variables.at("distance")==42); }
      }
    }
    if(kind=="vl53l4cda") {
      assert(e.variables.at("distance")==123);
      // Verify encoded timing writes against the upstream uint32 algorithm.
      for (auto pair : {std::make_pair(16u,0x5eu),std::make_pair(12u,0x61u)}) {
        uint32_t macro=(2304u*(0x40000000u/4096u))>>6;
        uint32_t tmp=macro*pair.first, budget=47500u<<12;
        uint32_t ls=(budget+((tmp>>6)>>1))/(tmp>>6)-1, ms=0;
        while(ls & 0xffffff00u) {ls>>=1;ms++;}
        uint16_t expected=(ms<<8)+(ls & 255); bool found=false;
        for(auto &w:b.writes) if(w.first==pair.second && w.second.size()==2 &&
          w.second[0]==(expected>>8) && w.second[1]==(expected & 255)) found=true;
        assert(found);
      }
      b.number(0x89,4,1); assert(!sequence(e,p,"measure"));
    }
    if(kind=="max31865") {
      assert(std::abs(e.variables.at("temperature"))<0.1);
      double t=-50, resistance=100*(1+0.0039083*t-0.0000005775*t*t-4.183e-12*(t-100)*t*t*t);
      b.number(1,uint32_t(std::round(resistance/430*32768))<<1,2);
      e.reset(p["constants"].as<JsonObjectConst>()); require(sequence(e,p,"measure"),e);
      assert(std::abs(e.variables.at("temperature")+50)<0.2);
      // PT1000 profile modification needs no interpreter changes.
      p["constants"]["rtd_nominal_ohm"]=1000; p["constants"]["reference_ohm"]=4300;
      e.reset(p["constants"].as<JsonObjectConst>()); require(sequence(e,p,"measure"),e);
      assert(std::abs(e.variables.at("temperature")+50)<0.2);
      b.number(7,128,1); assert(!sequence(e,p,"measure"));
    }
    if(kind=="st25dv16") { assert(e.variables.at("word0")==0x00010203); assert(b.writes.empty()); }
    require(sequence(e,p,"stop"),e);
    std::cout<<name<<": detect/init/measure/stop OK (simulated bus)\n";
  }
}
void failures() {
  FakeBus b; ProtocolEngine e(b); JsonDocument d;
  auto run=[&](const char *json,bool readOnly=false) {
    assert(!deserializeJson(d,json));return e.run(d.as<JsonArrayConst>(),readOnly);
  };
  assert(!run(R"([{"op":"write","reg":0,"bytes":1,"value":1}])",true)); assert(b.writes.empty());
  assert(!run(R"([{"op":"unknown"}])"));
  assert(!run(R"([{"op":"read","reg":0,"bytes":5,"var":"x"}])"));
  assert(!run(R"([{"op":"calc","var":"x","expr":[1,0,"/"]}])"));
  assert(!run(R"([{"op":"calc","var":"x","expr":["$missing"]}])"));
  assert(!run(R"([{"op":"calc","var":"x","expr":[1,32,"<<"]}])"));
  assert(!run(R"([{"op":"calc","var":"x","expr":[1,2]}])"));
  assert(!run(R"([{"op":"while","expr":1,"max_iterations":2,"steps":[]}])"));
  b.number(0,0,1);
  assert(!run(R"([{"op":"poll","reg":0,"bytes":1,"mask":1,"equals":1,"timeout_ms":10}])"));
  assert(b.time==10);
  b.number(0,0xfeff,2);
  require(run(R"([{"op":"read","reg":0,"bytes":2,"endian":"little","signed":true,"var":"x"}])"),e);
  assert(e.variables.at("x")==-2);
  b.failRead=true;
  assert(!run(R"([{"op":"read","reg":0,"bytes":1,"var":"x"}])"));
  b.failRead=false; b.failWrite=true;
  assert(!run(R"([{"op":"write","reg":0,"bytes":1,"value":1}])"));
  b.failWrite=false;
  assert(!run(R"([{"op":"write","reg":0,"bytes":1,"value":256}])"));
  auto p=load("sd/sensors/vl6180.json"); p["schema_version"]=1; assert(!e.validate(p.as<JsonObjectConst>()));
  std::cout<<"Invalid profiles, RPN, timeouts, bus errors and write protection OK\n";
}
void newSensorWithoutFirmwareChanges() {
  // A fifth, invented device uses new registers and conversion entirely in JSON.
  auto p=load("sd/sensors/vl6180.json");
  p["name"]="Synthetic temperature device";
  p["sensor_type"]="Unknown-to-firmware";
  p["group"]="Temperatur";
  p["interface"]["address"]=72;
  JsonDocument sequences;
  assert(!deserializeJson(sequences, R"({
    "detect":[{"op":"read","reg":17,"bytes":1,"var":"id"},
              {"op":"assert","expr":["$id",77,"eq"]}],
    "init":[{"op":"write","reg":32,"bytes":1,"value":1}],
    "measure":[{"op":"read","reg":256,"bytes":2,"endian":"little","signed":true,"var":"raw"},
               {"op":"calc","var":"temperature","expr":["$raw",128,"/"]}],
    "stop":[]})"));
  p["sequences"].set(sequences.as<JsonObjectConst>());
  p["outputs"][0]["var"]="temperature";
  FakeBus b; b.number(17,77,1); b.number(256,0x00fb,2); // little-endian -1280 => -10 C
  ProtocolEngine e(b); require(e.validate(p.as<JsonObjectConst>()),e);
  e.reset(p["constants"].as<JsonObjectConst>());
  require(sequence(e,p,"detect"),e); assert(b.writes.empty());
  require(sequence(e,p,"init"),e); assert(b.writes.size()==1);
  require(sequence(e,p,"measure"),e); assert(e.variables.at("temperature")==-10);
  std::cout<<"New device defined exclusively in JSON OK\n";
}
void autoScanTests() {
  AutoScan scan; unsigned probes = 0;
  std::vector<bool> connected = {false,true,false};
  auto probe = [&](size_t i) { ++probes; return connected.at(i); };
  assert(!scan.tick(0,3,probe)); assert(probes==1);
  assert(!scan.tick(5,3,probe)); assert(scan.matches.empty());
  assert(scan.tick(10,3,probe)); assert(scan.matches==std::vector<size_t>{1});
  assert(!scan.enabled);
  assert(!scan.tick(100,3,probe)); assert(!scan.tick(10000,3,probe)); assert(probes==3);
  scan.enable(3010);
  connected={true,false,true};
  assert(!scan.tick(3010,3,probe)); assert(scan.matches==std::vector<size_t>{1});
  assert(!scan.tick(3015,3,probe)); assert(scan.tick(3020,3,probe));
  assert((scan.matches==std::vector<size_t>{0,2})); // ambiguous
  scan.disable(); assert(!scan.tick(10000,3,probe)); assert(probes==6);
  scan.enable(10000); connected={false,false,false};
  assert(!scan.tick(10000,3,probe)); assert(!scan.tick(10005,3,probe));
  assert(scan.tick(10010,3,probe)); assert(scan.matches.empty()); // unplugged
  scan.restart(UINT32_MAX-5); assert(!scan.tick(UINT32_MAX-5,3,probe));
  assert(!scan.tick(UINT32_MAX-1,3,probe)); assert(scan.tick(0,3,probe));
  assert(!scan.tick(100,3,probe)); // next scan scheduled across millis wrap
  scan.restart(200); assert(scan.tick(200,0,probe)); assert(scan.matches.empty());

  // Scan real profile detect sequences with one simulated shared I2C bus.
  std::vector<JsonDocument> p;
  p.push_back(load("sd/sensors/vl6180.json"));
  p.push_back(load("sd/sensors/vl53l4cda.json"));
  FakeBus b; b.number(0,180,1); ProtocolEngine e(b); scan.enable(0);
  auto profileProbe=[&](size_t i) {
    require(e.validate(p[i].as<JsonObjectConst>()),e);
    e.reset(p[i]["constants"].as<JsonObjectConst>());
    return sequence(e,p[i],"detect");
  };
  assert(!scan.tick(0,2,profileProbe)); assert(scan.tick(1,2,profileProbe));
  assert(scan.matches==std::vector<size_t>{0});
  b.memory.clear(); b.number(0x10f,0xebaa,2); scan.enable(100);
  assert(!scan.tick(100,2,profileProbe)); assert(scan.tick(101,2,profileProbe));
  assert(scan.matches==std::vector<size_t>{1}); // different connected sensor
  assert(!scan.enabled);
  b.memory.clear(); scan.enable(200);
  assert(!scan.tick(200,2,profileProbe)); assert(scan.tick(201,2,profileProbe));
  assert(scan.matches.empty()); assert(b.writes.empty());
  std::cout<<"Autoscan: stop on unique match, explicit restart, ambiguity and read-only probes OK\n";
}
int main(){profiles();failures();newSensorWithoutFirmwareChanges();autoScanTests();}
