#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <RCSwitch.h>

// ---------- Feste Einstellungen ----------
const int   TX_PIN    = D4;
const int   RESET_PIN = D3;                // FLASH-Taste: 5 s halten = WLAN-Zugang loeschen
const char* AP_SSID   = "RF-Sender";       // offener Einrichtungs-AP
const char* HOSTNAME  = "rf-sender";       // im Heim-WLAN: http://rf-sender.local
const IPAddress AP_IP     (10, 4, 33, 1);  // Adresse des ESP im Einrichtungs-AP
const IPAddress AP_SUBNET (255, 255, 255, 0);
const unsigned long WLAN_TIMEOUT = 20000;  // so lange beim Start auf das WLAN warten
const unsigned long AP_NACH      = 30000;  // so lange ohne WLAN, dann AP wieder oeffnen

// ---------- Gespeicherte Einstellungen ----------
struct Config {
  uint32_t magic;
  uint32_t codeAn;
  uint32_t codeAus;
  uint8_t  bits;
  uint8_t  protocol;
  uint16_t pulse;
  uint8_t  repeat;
  uint32_t intervallAn;    // ms
  uint32_t intervallAus;   // ms
  bool     autostart;
};
const uint32_t MAGIC = 0x52463433;        // Kennung, ob schon gespeichert wurde
Config cfg;

struct WlanConfig {
  uint32_t magic;
  char ssid[33];
  char pass[65];
};
const uint32_t WLAN_MAGIC = 0x574C414E;   // "WLAN"
const int      WLAN_ADDR  = 256;          // eigener Bereich, unabhaengig von cfg
WlanConfig wlan;

RCSwitch mySwitch = RCSwitch();
ESP8266WebServer server(80);
DNSServer dns;

// ---------- Laufzeit-Status ----------
bool laeuft = false;
bool istAn  = false;
unsigned long letzteZeit = 0;
unsigned long anzahl = 0;
String letzteAktion = "";
unsigned long letzteAktionZeit = 0;

bool apAktiv = false;
bool staPausiert = false;
unsigned long letzteVerbindung = 0;
unsigned long neustartUm = 0;
unsigned long resetGedruecktSeit = 0;

// ---------- Webseite ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>433-MHz-Sender</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0 auto;padding:16px;max-width:520px}
h1{font-size:1.3em}.card{background:#1e1e1e;border-radius:10px;padding:14px;margin-bottom:14px}
h2{font-size:1em;margin:0 0 10px;color:#9cf}
button{font-size:1em;padding:10px 14px;border:0;border-radius:8px;margin:3px 3px 3px 0;cursor:pointer;color:#fff;background:#444}
.g{background:#2a7}.r{background:#c33}.b{background:#36c}
label{display:block;margin:8px 0 2px;font-size:.9em;color:#aaa}
input,select{width:100%;box-sizing:border-box;padding:8px;font-size:1em;border-radius:6px;border:1px solid #444;background:#2a2a2a;color:#eee}
.row{display:flex;gap:10px}.row>div{flex:1}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:6px;background:#666}
.small{font-size:.8em;color:#888}
</style></head><body>
<h1>📡 433-MHz-Sender</h1>

<div class="card"><h2>Status</h2>
<div><span class="dot" id="dot"></span><span id="sttxt">…</span></div>
<div class="small" id="info"></div>
<div style="margin-top:10px">
<button class="g" onclick="cmd('start')">▶ Start</button>
<button class="r" onclick="cmd('stop')">■ Stop</button></div></div>

<div class="card"><h2>Einzeln senden</h2>
<button class="b" onclick="send('an')">Code AN senden</button>
<button class="b" onclick="send('aus')">Code AUS senden</button>
<label>Beliebiger Code (dezimal)</label>
<div class="row"><div><input id="cc" type="number" min="0"></div>
<div style="flex:0"><button onclick="send('custom')">Senden</button></div></div></div>

<div class="card"><h2>Einstellungen</h2>
<form id="f">
<div class="row">
<div><label>Code AN</label><input name="codeAn" type="number" min="0" required><span class="small" id="hAn"></span></div>
<div><label>Code AUS</label><input name="codeAus" type="number" min="0" required><span class="small" id="hAus"></span></div></div>
<div class="row">
<div><label>AN-Dauer (s)</label><input name="intAn" type="number" min="1" step="0.1" required></div>
<div><label>AUS-Dauer (s)</label><input name="intAus" type="number" min="1" step="0.1" required></div></div>
<div class="row">
<div><label>Bits</label><input name="bits" type="number" min="1" max="32" required></div>
<div><label>Protokoll</label><input name="protocol" type="number" min="1" max="12" required></div></div>
<div class="row">
<div><label>Pulslänge (µs)</label><input name="pulse" type="number" min="50" max="2000" required></div>
<div><label>Wiederholungen</label><input name="repeat" type="number" min="1" max="30" required></div></div>
<label><input type="checkbox" name="autostart" style="width:auto"> Intervall nach dem Einschalten automatisch starten</label>
<button class="g" type="submit">💾 Speichern</button> <span id="msg" class="small"></span>
</form></div>

<div class="card"><h2>WLAN</h2>
<div id="wst">…</div>
<button id="scanbtn" onclick="scan()">🔍 Netze suchen</button>
<label>Gefundene Netze</label>
<select id="wl"><option value="">– erst „Netze suchen“ –</option></select>
<label>WLAN-Name (SSID)</label><input id="ws" maxlength="32">
<label>Passwort</label><input id="wp" type="password" maxlength="64">
<button class="g" onclick="wsave()">💾 WLAN speichern &amp; verbinden</button>
<button class="r" id="wfg" onclick="wforget()">WLAN vergessen</button>
<div class="small" id="wmsg"></div>
<div class="small">Nach dem Speichern startet der ESP neu und ist dann im Heim-WLAN unter
<b>http://rf-sender.local</b> oder der IP aus deinem Router erreichbar. Findet er das WLAN nicht,
öffnet er nach 30 s wieder den offenen Accesspoint „RF-Sender“ (http://10.4.33.1).</div>
</div>

<script>
let loaded=false;
const $=id=>document.getElementById(id);
async function post(u,d){try{return (await fetch(u,{method:'POST',body:d})).ok}catch(e){return false}}
async function cmd(c){await post('/api/'+c);upd()}
async function send(w){const d=new URLSearchParams({was:w});
 if(w=='custom'){if(!$('cc').value)return;d.append('code',$('cc').value)}
 await post('/api/send',d);upd()}
function hex(v){return v!==''?'0x'+Number(v).toString(16).toUpperCase():''}
function hx(){const f=$('f');$('hAn').textContent=hex(f.codeAn.value);$('hAus').textContent=hex(f.codeAus.value)}
function wtxt(w){
 if(w.verbunden)return 'Verbunden mit „'+w.ssid+'“ · IP '+w.ip+' · '+w.rssi+' dBm'+(w.ap?' · Einrichtungs-AP zusätzlich aktiv':'');
 if(w.konfiguriert)return 'Keine Verbindung zu „'+w.ssid+'“ – offener Einrichtungs-AP aktiv ('+w.apIp+')';
 return 'Kein WLAN eingerichtet – offener Einrichtungs-AP aktiv ('+w.apIp+')';}
async function upd(){try{
 const s=await (await fetch('/api/status')).json();
 $('dot').style.background=s.laeuft?(s.istAn?'#2c7':'#c33'):'#666';
 $('sttxt').textContent=s.laeuft?('Intervall läuft – gerade '+(s.istAn?'AN':'AUS')+', Wechsel in '+Math.ceil(s.rest)+' s'):'Gestoppt';
 $('info').textContent='Sendungen: '+s.anzahl+' · Zuletzt: '+(s.letzte||'–');
 $('wst').textContent=wtxt(s.wifi);
 $('wfg').style.display=s.wifi.konfiguriert?'':'none';
 if(!loaded){const f=$('f');for(const k in s.cfg){const e=f.elements[k];if(!e)continue;
   if(e.type=='checkbox')e.checked=s.cfg[k];else e.value=s.cfg[k]}
   if(s.wifi.konfiguriert)$('ws').value=s.wifi.ssid;loaded=true;hx()}
}catch(e){$('sttxt').textContent='Keine Verbindung'}}
async function scan(){$('scanbtn').textContent='Suche …';
 try{const l=await (await fetch('/api/scan')).json();const s=$('wl');
  s.innerHTML='';const o0=document.createElement('option');o0.value='';o0.textContent='– Netz wählen ('+l.length+' gefunden) –';s.appendChild(o0);
  l.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const o=document.createElement('option');o.value=n.ssid;
   o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.offen?', offen':'')+')';s.appendChild(o)});
 }catch(e){}$('scanbtn').textContent='🔍 Netze suchen'}
$('wl').onchange=()=>{if($('wl').value)$('ws').value=$('wl').value};
async function wsave(){const ssid=$('ws').value.trim();
 if(!ssid){$('wmsg').textContent='Bitte einen WLAN-Namen angeben.';return}
 const ok=await post('/api/wifi',new URLSearchParams({ssid:ssid,pass:$('wp').value}));
 $('wmsg').textContent=ok?'Gespeichert – der ESP startet neu und verbindet sich mit „'+ssid+'“.':'Fehler beim Speichern';}
async function wforget(){if(!confirm('WLAN-Zugang löschen? Der ESP startet danach als offener Accesspoint.'))return;
 const ok=await post('/api/wifi/forget');
 $('wmsg').textContent=ok?'Gelöscht – der ESP startet neu als offener Accesspoint „RF-Sender“.':'Fehler';}
$('f').addEventListener('input',hx);
$('f').addEventListener('submit',async e=>{e.preventDefault();
 const ok=await post('/api/config',new URLSearchParams(new FormData($('f'))));
 $('msg').textContent=ok?'Gespeichert ✓':'Fehler beim Speichern';
 setTimeout(()=>$('msg').textContent='',3000);upd()});
upd();setInterval(upd,1000);
</script></body></html>
)rawliteral";

// ---------- Einstellungen laden/speichern ----------
void setDefaults() {
  cfg.magic        = MAGIC;
  cfg.codeAn       = 1193041;   // Beispiel (0x123451), eigene Codes in der WebUI eintragen
  cfg.codeAus      = 1193042;   // Beispiel (0x123452)
  cfg.bits         = 24;
  cfg.protocol     = 1;
  cfg.pulse        = 360;
  cfg.repeat       = 8;
  cfg.intervallAn  = 5000;
  cfg.intervallAus = 10000;
  cfg.autostart    = false;
}

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC) {
    setDefaults();
    saveConfig();
  }
  EEPROM.get(WLAN_ADDR, wlan);
  if (wlan.magic != WLAN_MAGIC) memset(&wlan, 0, sizeof(wlan));
  wlan.ssid[32] = 0;
  wlan.pass[64] = 0;
}

void saveWlan() {
  EEPROM.put(WLAN_ADDR, wlan);
  EEPROM.commit();
}

bool wlanKonfiguriert() {
  return wlan.magic == WLAN_MAGIC && wlan.ssid[0] != 0;
}

void applyRadio() {
  mySwitch.setProtocol(cfg.protocol);     // setzt auch eine Standard-Pulslaenge ...
  mySwitch.setPulseLength(cfg.pulse);     // ... deshalb danach die eigene setzen
  mySwitch.setRepeatTransmit(cfg.repeat);
}

// ---------- WLAN ----------
void startAP() {
  WiFi.mode(wlanKonfiguriert() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
  WiFi.softAP(AP_SSID);                   // ohne Passwort = offen
  dns.start(53, "*", AP_IP);
  apAktiv = true;
  Serial.print("Offener AP: "); Serial.print(AP_SSID);
  Serial.print("  IP: "); Serial.println(AP_IP);
}

void stopAP() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apAktiv = false;
  Serial.println("Einrichtungs-AP geschlossen");
}

void startWlan() {
  WiFi.persistent(false);
  WiFi.hostname(HOSTNAME);

  if (!wlanKonfiguriert()) {
    Serial.println("Kein WLAN eingerichtet");
    startAP();
    return;
  }

  Serial.print("Verbinde mit "); Serial.print(wlan.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wlan.ssid, wlan.pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WLAN_TIMEOUT) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    letzteVerbindung = millis();
    Serial.print("Verbunden, IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WLAN nicht erreichbar");
    startAP();                            // STA versucht es im Hintergrund weiter
  }
}

// Laeuft in loop(): AP oeffnen, wenn das WLAN wegbleibt, und wieder schliessen,
// sobald es zurueck ist und niemand mehr am AP haengt.
void wlanPflege() {
  if (!wlanKonfiguriert()) return;

  int apClients = apAktiv ? WiFi.softAPgetStationNum() : 0;

  if (WiFi.status() == WL_CONNECTED) {
    letzteVerbindung = millis();
    if (apAktiv && apClients == 0) stopAP();
    return;
  }

  if (!apAktiv && millis() - letzteVerbindung > AP_NACH) startAP();

  // Die Verbindungsversuche wechseln den Funkkanal und stoeren den AP.
  // Solange jemand am AP haengt, deshalb pausieren.
  if (apAktiv && apClients > 0 && !staPausiert) {
    WiFi.disconnect();
    staPausiert = true;
  } else if (apAktiv && apClients == 0 && staPausiert) {
    WiFi.begin(wlan.ssid, wlan.pass);
    staPausiert = false;
  }
}

void resetTastePruefen() {
  if (digitalRead(RESET_PIN) == LOW) {
    if (resetGedruecktSeit == 0) resetGedruecktSeit = millis();
    if (millis() - resetGedruecktSeit > 5000) {
      Serial.println("FLASH-Taste 5 s gehalten: WLAN-Zugang wird geloescht");
      memset(&wlan, 0, sizeof(wlan));
      saveWlan();
      delay(200);
      ESP.restart();
    }
  } else {
    resetGedruecktSeit = 0;
  }
}

// ---------- Senden & Intervall ----------
void sendeCode(uint32_t code, const String& name) {
  Serial.println("Sende " + name + " (" + String(code) + ")");
  mySwitch.send(code, cfg.bits);
  anzahl++;
  letzteAktion = name + " " + String(code);
  letzteAktionZeit = millis();
}

void startIntervall() {
  laeuft = true;
  istAn = false;
  letzteZeit = millis() - cfg.intervallAus;   // sofort mit AN beginnen
}

void stopIntervall() {
  laeuft = false;
  istAn = false;
  sendeCode(cfg.codeAus, "AUS");              // Geraet sicher ausschalten
}

// ---------- Web-Handler ----------
uint32_t argU32(const char* name, uint32_t def) {
  return server.hasArg(name) ? strtoul(server.arg(name).c_str(), nullptr, 10) : def;
}

String jsonEsc(const String& s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

void handleStatus() {
  float rest = 0;
  if (laeuft) {
    unsigned long warte = istAn ? cfg.intervallAn : cfg.intervallAus;
    unsigned long vergangen = millis() - letzteZeit;
    rest = vergangen < warte ? (warte - vergangen) / 1000.0 : 0;
  }
  String letzte = letzteAktion.length()
    ? letzteAktion + " (vor " + String((millis() - letzteAktionZeit) / 1000) + " s)"
    : "";
  bool verbunden = WiFi.status() == WL_CONNECTED;

  String j = "{";
  j += "\"laeuft\":" + String(laeuft ? "true" : "false");
  j += ",\"istAn\":" + String(istAn ? "true" : "false");
  j += ",\"rest\":" + String(rest, 1);
  j += ",\"anzahl\":" + String(anzahl);
  j += ",\"letzte\":\"" + letzte + "\"";
  j += ",\"cfg\":{";
  j += "\"codeAn\":" + String(cfg.codeAn);
  j += ",\"codeAus\":" + String(cfg.codeAus);
  j += ",\"intAn\":" + String(cfg.intervallAn / 1000.0, 1);
  j += ",\"intAus\":" + String(cfg.intervallAus / 1000.0, 1);
  j += ",\"bits\":" + String(cfg.bits);
  j += ",\"protocol\":" + String(cfg.protocol);
  j += ",\"pulse\":" + String(cfg.pulse);
  j += ",\"repeat\":" + String(cfg.repeat);
  j += ",\"autostart\":" + String(cfg.autostart ? "true" : "false");
  j += "},\"wifi\":{";
  j += "\"konfiguriert\":" + String(wlanKonfiguriert() ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEsc(String(wlan.ssid)) + "\"";
  j += ",\"verbunden\":" + String(verbunden ? "true" : "false");
  j += ",\"ip\":\"" + (verbunden ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"rssi\":" + String(verbunden ? WiFi.RSSI() : 0);
  j += ",\"ap\":" + String(apAktiv ? "true" : "false");
  j += ",\"apIp\":\"" + AP_IP.toString() + "\"";
  j += "}}";
  server.send(200, "application/json", j);
}

void handleSend() {
  String was = server.arg("was");
  if (was == "an")       sendeCode(cfg.codeAn, "AN");
  else if (was == "aus") sendeCode(cfg.codeAus, "AUS");
  else if (was == "custom" && server.hasArg("code"))
                         sendeCode(argU32("code", 0), "Einzel");
  else { server.send(400, "text/plain", "Ungueltig"); return; }
  server.send(200, "text/plain", "OK");
}

void handleConfig() {
  cfg.codeAn   = argU32("codeAn",  cfg.codeAn);
  cfg.codeAus  = argU32("codeAus", cfg.codeAus);
  cfg.bits     = constrain(argU32("bits",     cfg.bits),     1, 32);
  cfg.protocol = constrain(argU32("protocol", cfg.protocol), 1, 12);
  cfg.pulse    = constrain(argU32("pulse",    cfg.pulse),    50, 2000);
  cfg.repeat   = constrain(argU32("repeat",   cfg.repeat),   1, 30);

  float a = server.arg("intAn").toFloat();
  float b = server.arg("intAus").toFloat();
  if (a >= 1) cfg.intervallAn  = a * 1000;
  if (b >= 1) cfg.intervallAus = b * 1000;

  cfg.autostart = server.hasArg("autostart");   // Checkbox fehlt, wenn nicht angehakt

  saveConfig();
  applyRadio();
  server.send(200, "text/plain", "OK");
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  int anzahlNetze = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;           // versteckte Netze
    bool doppelt = false;                       // gleiches Netz auf mehreren APs
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == ssid) doppelt = true;
    if (doppelt) continue;
    if (anzahlNetze++) j += ",";
    j += "{\"ssid\":\"" + jsonEsc(ssid) + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI(i));
    j += ",\"offen\":" + String(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "true" : "false") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

void handleWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    server.send(400, "text/plain", "Ungueltig");
    return;
  }
  memset(&wlan, 0, sizeof(wlan));
  wlan.magic = WLAN_MAGIC;
  strncpy(wlan.ssid, ssid.c_str(), 32);
  strncpy(wlan.pass, pass.c_str(), 64);
  saveWlan();
  server.send(200, "text/plain", "OK");
  neustartUm = millis() + 1500;               // Antwort erst noch ausliefern
}

void handleWifiForget() {
  memset(&wlan, 0, sizeof(wlan));
  saveWlan();
  server.send(200, "text/plain", "OK");
  neustartUm = millis() + 1500;
}

void handleNotFound() {
  // Captive Portal: im Einrichtungs-AP alles auf die Startseite umleiten
  if (apAktiv && server.hostHeader() != AP_IP.toString()) {
    server.sendHeader("Location", "http://" + AP_IP.toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(RESET_PIN, INPUT_PULLUP);
  loadConfig();
  mySwitch.enableTransmit(TX_PIN);
  applyRadio();

  startWlan();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/",                 HTTP_GET,  []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status",       HTTP_GET,  handleStatus);
  server.on("/api/start",        HTTP_POST, []() { startIntervall(); server.send(200, "text/plain", "OK"); });
  server.on("/api/stop",         HTTP_POST, []() { stopIntervall();  server.send(200, "text/plain", "OK"); });
  server.on("/api/send",         HTTP_POST, handleSend);
  server.on("/api/config",       HTTP_POST, handleConfig);
  server.on("/api/scan",         HTTP_GET,  handleScan);
  server.on("/api/wifi",         HTTP_POST, handleWifi);
  server.on("/api/wifi/forget",  HTTP_POST, handleWifiForget);
  server.onNotFound(handleNotFound);
  server.begin();

  if (cfg.autostart) startIntervall();
}

void loop() {
  if (apAktiv) dns.processNextRequest();
  server.handleClient();
  MDNS.update();
  wlanPflege();
  resetTastePruefen();

  if (neustartUm && (long)(millis() - neustartUm) >= 0) {
    Serial.println("Neustart ...");
    delay(100);
    ESP.restart();
  }

  if (laeuft) {
    unsigned long warte = istAn ? cfg.intervallAn : cfg.intervallAus;
    if (millis() - letzteZeit >= warte) {
      letzteZeit = millis();
      istAn = !istAn;
      if (istAn) sendeCode(cfg.codeAn,  "AN");
      else       sendeCode(cfg.codeAus, "AUS");
    }
  }
}
