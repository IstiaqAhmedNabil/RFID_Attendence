/*
  ESP32 RFID Scanner with Captive Portal WiFi Manager
  =====================================================
  Hardware:
    RDM6300 TX  → GPIO16 (RX2)
    RDM6300 VCC → 5V
    RDM6300 GND → GND
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>        // ← needed for captive portal auto-redirect
#include <Preferences.h>
#include <HTTPClient.h>

// ─── Configuration ────────────────────────────────────────────────────────────
#define RFID_RX_PIN     16
#define RFID_BAUD       9600
#define AP_SSID         "ESP32-RFID-Config"
#define AP_PASSWORD     ""            // "" = open network (easiest to connect)
#define SERVER_URL "https://netjetlabs.com/api/scan"
#define WIFI_TIMEOUT_MS 10000
#define MAX_NETWORKS    3
#define DNS_PORT        53

// ─── Globals ──────────────────────────────────────────────────────────────────
HardwareSerial RFID(2);
WebServer      server(80);
DNSServer      dnsServer;
Preferences    prefs;

bool wifiConnected = false;
bool portalActive  = false;

// ─── Card ID Conversion ───────────────────────────────────────────────────────
/*
  RDM6300 14-byte frame:
    [0]      0x02  STX
    [1..2]   version/facility  (2 ASCII hex chars)
    [3..10]  card number       (8 ASCII hex chars)  <- we parse this
    [11..12] checksum          (2 ASCII hex chars)
    [13]     0x03  ETX

  Example raw: 02 34 33 30 30 38 30 30 37 41 36 xx xx 03
  Card hex:    "008007A6"  ->  decimal 8390566  ->  "0008390566"
*/
String convertCardID(const String& raw) {
  if (raw.length() < 14)        return "";
  if (raw[0] != 0x02)           return "";
  if (raw[13] != 0x03)          return "";

  String cardHex = raw.substring(3, 11);
  if (cardHex.length() != 8)    return "";
  for (char c : cardHex) {
    if (!isHexadecimalDigit(c)) return "";
  }

  unsigned long cardNum = strtoul(cardHex.c_str(), NULL, 16);
  char buf[12];
  snprintf(buf, sizeof(buf), "%010lu", cardNum);
  return String(buf);
}

// ─── WiFi: try every saved network ───────────────────────────────────────────
bool tryStoredNetworks() {
  prefs.begin("wifi", true);
  int count = prefs.getInt("count", 0);
  prefs.end();
  if (count == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  for (int i = 0; i < count; i++) {
    prefs.begin("wifi", true);
    String ssid = prefs.getString(("ssid" + String(i)).c_str(), "");
    String pass = prefs.getString(("pass" + String(i)).c_str(), "");
    prefs.end();
    if (ssid.isEmpty()) continue;

    Serial.printf("Trying [%d/%d]: %s\n", i + 1, count, ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    WiFi.disconnect(true);
    delay(300);
  }
  return false;
}

// ─── Build Config Page ────────────────────────────────────────────────────────
String buildConfigPage() {
  prefs.begin("wifi", true);
  int count = prefs.getInt("count", 0);
  String s[3], p[3];
  for (int i = 0; i < 3; i++) {
    s[i] = prefs.getString(("ssid" + String(i)).c_str(), "");
    p[i] = prefs.getString(("pass" + String(i)).c_str(), "");
  }
  prefs.end();

  String notice = "";
  if (count > 0)
    notice = "<div class='ok'>&#10003; " + String(count) + " network(s) saved.</div>";

  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RFID WiFi Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#f0f4f8;display:flex;"
         "justify-content:center;padding:16px}"
    ".card{background:#fff;border-radius:12px;padding:24px;max-width:400px;"
          "width:100%;box-shadow:0 2px 16px rgba(0,0,0,.12);margin-top:20px}"
    "h1{font-size:18px;color:#1a1a2e;margin-bottom:4px}"
    ".sub{font-size:12px;color:#888;margin-bottom:16px}"
    ".ok{background:#d4edda;border:1px solid #c3e6cb;border-radius:6px;"
        "padding:10px 12px;font-size:13px;color:#155724;margin-bottom:14px}"
    ".net{background:#f8f9fa;border:1px solid #e9ecef;border-radius:8px;"
         "padding:14px;margin-bottom:10px}"
    ".net h3{font-size:11px;color:#555;margin-bottom:10px;"
            "text-transform:uppercase;letter-spacing:.5px}"
    "label{display:block;font-size:11px;color:#666;margin-bottom:3px}"
    "input{width:100%;padding:8px 10px;border:1px solid #ddd;border-radius:6px;"
          "font-size:14px;margin-bottom:10px;background:#fff}"
    "input:focus{outline:none;border-color:#4a90d9}"
    "button{width:100%;padding:13px;background:#4a90d9;color:#fff;border:none;"
           "border-radius:8px;font-size:15px;font-weight:bold;cursor:pointer;"
           "margin-top:4px}"
    "button:active{background:#357abd}"
    "</style></head><body><div class='card'>"
    "<h1>RFID WiFi Setup</h1>"
    "<p class='sub'>Save up to 3 networks. ESP32 tries them in order on boot.</p>");

  html += notice;
  html += "<form action='/save' method='POST'>";

  const char* labels[3] = {
    "Network 1 (primary)",
    "Network 2 (fallback)",
    "Network 3 (fallback)"
  };
  for (int i = 0; i < 3; i++) {
    html += "<div class='net'><h3>" + String(labels[i]) + "</h3>";
    html += "<label>SSID</label>";
    html += "<input name='ssid" + String(i) + "' placeholder='Network name' value='" + s[i] + "'>";
    html += "<label>Password</label>";
    html += "<input name='pass" + String(i) + "' type='password' placeholder='Leave blank if open' value='" + p[i] + "'>";
    html += "</div>";
  }

  html += "<button type='submit'>Save &amp; Reboot</button></form></div></body></html>";
  return html;
}

// ─── Web Server Handlers ──────────────────────────────────────────────────────
void handleRoot() {
  server.send(200, "text/html", buildConfigPage());
}

void handleRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleSave() {
  prefs.begin("wifi", false);
  int count = 0;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    String ssid = server.arg("ssid" + String(i));
    String pass = server.arg("pass" + String(i));
    ssid.trim();
    if (ssid.length() > 0) {
      prefs.putString(("ssid" + String(i)).c_str(), ssid);
      prefs.putString(("pass" + String(i)).c_str(), pass);
      count++;
    } else {
      prefs.remove(("ssid" + String(i)).c_str());
      prefs.remove(("pass" + String(i)).c_str());
    }
  }
  prefs.putInt("count", count);
  prefs.end();

  Serial.printf("Saved %d network(s). Rebooting...\n", count);

  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<style>body{font-family:Arial;display:flex;justify-content:center;"
    "padding:40px;background:#f0f4f8}.c{background:#fff;border-radius:12px;"
    "padding:30px;text-align:center}h2{color:#155724}p{color:#666;font-size:14px;"
    "margin-top:8px}</style></head><body><div class='c'>"
    "<h2>&#10003; Saved!</h2><p>Saved " + String(count) + " network(s).<br>"
    "Rebooting now...</p></div></body></html>");

  delay(2000);
  ESP.restart();
}

// ─── Start AP + DNS + Web Server ──────────────────────────────────────────────
void startCaptivePortal() {
  portalActive = true;

  WiFi.mode(WIFI_AP);
  if (strlen(AP_PASSWORD) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  } else {
    WiFi.softAP(AP_SSID);  // open AP
  }
  delay(500);  // must wait for AP to be fully up before starting DNS

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP IP: %s\n", apIP.toString().c_str());

  // DNS wildcard: ALL domain queries answered with our IP
  // This triggers the automatic "sign in to network" popup on phones
  dnsServer.setTTL(300);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Main config page
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);

  // Captive portal detection endpoints used by different OSes
  // Without these, phones show "no internet" and may block the page
  server.on("/generate_204",          HTTP_GET, handleRedirect);  // Android
  server.on("/gen_204",               HTTP_GET, handleRedirect);  // Android old
  server.on("/hotspot-detect.html",   HTTP_GET, handleRedirect);  // iOS / macOS
  server.on("/library/test/success.html", HTTP_GET, handleRedirect); // iOS
  server.on("/ncsi.txt",              HTTP_GET, handleRedirect);  // Windows
  server.on("/connecttest.txt",       HTTP_GET, handleRedirect);  // Windows
  server.on("/fwlink",                HTTP_GET, handleRedirect);  // Windows
  server.on("/redirect",              HTTP_GET, handleRedirect);
  server.onNotFound(handleRedirect);  // catch everything else

  server.begin();

  Serial.println("==============================================");
  Serial.println("Captive portal active!");
  Serial.printf("  Connect to WiFi:  \"%s\"\n", AP_SSID);
  if (strlen(AP_PASSWORD) == 0)
    Serial.println("  (Open network, no password needed)");
  else
    Serial.printf("  Password:         %s\n", AP_PASSWORD);
  Serial.printf("  Manual URL:       http://%s\n", apIP.toString().c_str());
  Serial.println("==============================================");
}

// ─── Send card to server ──────────────────────────────────────────────────────
void sendCardToServer(const String& cardNumber) {
  if (!wifiConnected) {
    Serial.println("No WiFi - cannot send.");
    return;
  }
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"card_number\":\"" + cardNumber + "\"}";
  int code = http.POST(payload);
  if (code > 0) {
    Serial.printf("Server response %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 RFID Scanner ===");

  RFID.begin(RFID_BAUD, SERIAL_8N1, RFID_RX_PIN, -1);
  Serial.println("RFID reader ready.");

  wifiConnected = tryStoredNetworks();

  if (!wifiConnected) {
    Serial.println("No WiFi. Starting captive portal...");
    startCaptivePortal();
  } else {
    // Also run web server on STA mode so config page stays accessible
    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRedirect);
    server.begin();
    Serial.printf("Config page: http://%s\n", WiFi.localIP().toString().c_str());
  }
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // FIX 1: server.handleClient() must run ALWAYS (not just in portal mode)
  server.handleClient();

  // FIX 2: DNS must also be pumped every loop tick in portal mode
  if (portalActive) {
    dnsServer.processNextRequest();
  }

  // RFID reading
  if (RFID.available()) {
    delay(50);  // allow full 14-byte frame to arrive in buffer
    String raw = "";
    while (RFID.available()) {
      raw += (char)RFID.read();
    }

    Serial.print("Raw bytes: ");
    for (size_t i = 0; i < raw.length(); i++) {
      Serial.printf("%02X ", (uint8_t)raw[i]);
    }
    Serial.println();

    String cardNumber = convertCardID(raw);
    if (cardNumber.length() > 0) {
      Serial.println(">>> Card: " + cardNumber);
      sendCardToServer(cardNumber);
    } else {
      Serial.println("Invalid frame, ignoring.");
    }
  }

  // WiFi watchdog: check every 30s, restart portal if connection lost
  static unsigned long lastCheck = 0;
  if (wifiConnected && millis() - lastCheck > 30000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi dropped. Reconnecting...");
      wifiConnected = tryStoredNetworks();
      if (!wifiConnected) {
        Serial.println("Could not reconnect. Starting portal...");
        startCaptivePortal();
      }
    }
  }
}
