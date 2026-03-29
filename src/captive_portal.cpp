#include "captive_portal.h"
#include "config.h"

CaptivePortal captivePortal;

void CaptivePortal::setupRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/save", HTTP_POST, [this]() { handleSave(); });
    _server.on("/scan", HTTP_GET, [this]() { handleScan(); });
    _server.onNotFound([this]() { handleNotFound(); });
}

void CaptivePortal::begin() {
    Serial.println("[Portal] Starting captive portal in AP mode...");

    _networkMode = false;

    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    IPAddress ip = WiFi.softAPIP();
    Serial.print("[Portal] AP IP: ");
    Serial.println(ip);

    // DNS server for captive portal redirect
    _dnsServer.start(53, "*", ip);

    setupRoutes();
    _server.begin();
    _active = true;
    _configured = false;
    _startTime = millis();

    Serial.println("[Portal] Connect to WiFi: " AP_SSID);
    Serial.println("[Portal] Then open http://192.168.4.1");
}

void CaptivePortal::beginOnNetwork() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Portal] Error: WiFi not connected. Connect first.");
        return;
    }

    Serial.println("[Portal] Starting web config on network...");

    _networkMode = true;

    setupRoutes();
    _server.begin();
    _active = true;
    _configured = false;
    _startTime = millis();

    Serial.println("[Portal] =================================");
    Serial.print("[Portal] Open http://");
    Serial.println(WiFi.localIP());
    Serial.println("[Portal] =================================");
}

void CaptivePortal::update() {
    if (!_active) return;

    // DNS only needed in AP mode
    if (!_networkMode) {
        _dnsServer.processNextRequest();
    }

    _server.handleClient();

    // Timeout check (only in AP mode - network mode stays open)
    if (!_networkMode && millis() - _startTime > PORTAL_TIMEOUT_MS) {
        Serial.println("[Portal] Timeout, stopping portal");
        stop();
    }
}

void CaptivePortal::stop() {
    if (!_active) return;

    _server.stop();

    if (!_networkMode) {
        _dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }

    _active = false;
    _networkMode = false;

    Serial.println("[Portal] Stopped");
}

void CaptivePortal::handleRoot() {
    _server.send(200, "text/html", generateHtml());
}

void CaptivePortal::handleSave() {
    _wifiSsid = urlDecode(_server.arg("ssid"));
    _wifiPassword = urlDecode(_server.arg("pass"));
    _wsHost = urlDecode(_server.arg("host"));
    _wsPort = _server.arg("port").toInt();

    _configured = true;

    String response = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    response += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    response += "<style>body{font-family:sans-serif;padding:20px;background:#1a1a2e;color:#eee}</style>";
    response += "<title>Saved</title></head><body>";
    response += "<h2 style='color:#0f9d58'>Configuration Saved!</h2>";
    response += "<p>WiFi: " + _wifiSsid + "</p>";

    if (_wsHost.length() > 0 && _wsPort > 0) {
        response += "<p>Daemon: " + _wsHost + ":" + String(_wsPort) + "</p>";
    } else {
        response += "<p>Daemon: auto-discover via mDNS</p>";
    }

    if (_networkMode) {
        response += "<p>Settings saved. Use 'reboot' command to apply.</p>";
        response += "<p><a href='/' style='color:#0f9d58'>Back to config</a></p>";
    } else {
        response += "<p>The device will now restart and connect.</p>";
    }

    response += "</body></html>";

    _server.send(200, "text/html", response);

    // In AP mode, stop and let main loop handle restart
    if (!_networkMode) {
        delay(1000);
        stop();
    }
}

void CaptivePortal::handleScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "\"" + WiFi.SSID(i) + "\"";
    }
    json += "]";
    _server.send(200, "application/json", json);
}

void CaptivePortal::handleNotFound() {
    if (_networkMode) {
        // In network mode, just return 404
        _server.send(404, "text/plain", "Not found");
    } else {
        // In AP mode, redirect all requests to root (captive portal behavior)
        _server.sendHeader("Location", "http://192.168.4.1/", true);
        _server.send(302, "text/plain", "");
    }
}

String CaptivePortal::urlDecode(const String& str) {
    String decoded;
    char temp[] = "0x00";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if (c == '+') {
            decoded += ' ';
        } else if (c == '%' && i + 2 < str.length()) {
            temp[2] = str[i + 1];
            temp[3] = str[i + 2];
            decoded += (char)strtol(temp, nullptr, 16);
            i += 2;
        } else {
            decoded += c;
        }
    }
    return decoded;
}

String CaptivePortal::generateHtml() {
    String html = R"(<!DOCTYPE html>
<html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Insta360 Controller Setup</title>
<style>
body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;background:#1a1a2e;color:#eee}
h2{color:#0f9d58}
input,select{width:100%;padding:10px;margin:5px 0 15px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#16213e;color:#eee}
button{background:#0f9d58;color:#fff;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;margin:5px 0}
button:hover{background:#0d8a4d}
.section{background:#16213e;padding:15px;border-radius:8px;margin:15px 0}
label{font-weight:bold;color:#aaa}
a{color:#0f9d58}
.note{background:#2d2d44;padding:10px;border-radius:4px;font-size:13px;color:#aaa;margin-top:15px}
</style>
</head><body>
<h2>Insta360 Controller</h2>

<form action='/save' method='POST'>
<div class='section'>
<h3>WiFi Settings</h3>
<label>Network Name (SSID)</label>
<input type='text' name='ssid' id='ssid' placeholder='Your WiFi network'>
<button type='button' onclick='scanWifi()'>Scan for Networks</button>
<div id='networks'></div>
<label>Password</label>
<input type='password' name='pass' placeholder='WiFi password'>
</div>

<div class='section'>
<h3>Daemon Connection (Optional)</h3>
<p style='color:#aaa;font-size:14px'>Leave blank to auto-discover via mDNS. Only set these if you need a manual override.</p>

<label>Mac IP Address</label>
<input type='text' name='host' placeholder='Auto-discover via mDNS'>
<label>Port</label>
<input type='number' name='port' placeholder='Default: 9000'>
</div>

<button type='submit'>Save Configuration</button>
</form>

<div class='note'>
<strong>Note:</strong> The daemon (linkctl-daemon) must be running on your Mac. The controller will auto-discover it via mDNS.
</div>

<script>
function scanWifi(){
  document.getElementById('networks').innerHTML='Scanning...';
  fetch('/scan').then(r=>r.json()).then(nets=>{
    let html='<select onchange="document.getElementById(\'ssid\').value=this.value"><option>Select network...</option>';
    nets.forEach(n=>html+='<option>'+n+'</option>');
    html+='</select>';
    document.getElementById('networks').innerHTML=html;
  }).catch(e=>{
    document.getElementById('networks').innerHTML='<span style="color:#f44">Scan failed</span>';
  });
}
</script>
</body></html>)";
    return html;
}
