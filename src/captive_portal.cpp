#include "captive_portal.h"
#include "config.h"

CaptivePortal captivePortal;

void CaptivePortal::begin() {
    Serial.println("[Portal] Starting captive portal...");

    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    IPAddress ip = WiFi.softAPIP();
    Serial.print("[Portal] AP IP: ");
    Serial.println(ip);

    // DNS server for captive portal redirect
    _dnsServer.start(53, "*", ip);

    // Web server routes
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/save", HTTP_POST, [this]() { handleSave(); });
    _server.on("/scan", HTTP_GET, [this]() { handleScan(); });
    _server.onNotFound([this]() { handleNotFound(); });

    _server.begin();
    _active = true;
    _configured = false;
    _startTime = millis();

    Serial.println("[Portal] Connect to WiFi: " AP_SSID);
    Serial.println("[Portal] Then open http://192.168.4.1");
}

void CaptivePortal::update() {
    if (!_active) return;

    _dnsServer.processNextRequest();
    _server.handleClient();

    // Timeout check
    if (millis() - _startTime > PORTAL_TIMEOUT_MS) {
        Serial.println("[Portal] Timeout, stopping portal");
        stop();
    }
}

void CaptivePortal::stop() {
    if (!_active) return;

    _server.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _active = false;

    Serial.println("[Portal] Stopped");
}

void CaptivePortal::handleRoot() {
    _server.send(200, "text/html", generateHtml());
}

void CaptivePortal::handleSave() {
    _wifiSsid = urlDecode(_server.arg("ssid"));
    _wifiPassword = urlDecode(_server.arg("pass"));

    // Parse QR URL or individual fields
    String qrUrl = urlDecode(_server.arg("qrurl"));
    if (qrUrl.length() > 0) {
        // Parse URL like: insta360link://connect?port=XXXXX&token=XXXXX
        int portIdx = qrUrl.indexOf("port=");
        int tokenIdx = qrUrl.indexOf("token=");

        if (portIdx >= 0) {
            int portEnd = qrUrl.indexOf('&', portIdx);
            String portStr = (portEnd >= 0)
                ? qrUrl.substring(portIdx + 5, portEnd)
                : qrUrl.substring(portIdx + 5);
            _wsPort = portStr.toInt();
        }

        if (tokenIdx >= 0) {
            int tokenEnd = qrUrl.indexOf('&', tokenIdx);
            _wsToken = (tokenEnd >= 0)
                ? qrUrl.substring(tokenIdx + 6, tokenEnd)
                : qrUrl.substring(tokenIdx + 6);
        }
    } else {
        // Use individual fields
        _wsPort = _server.arg("port").toInt();
        _wsToken = urlDecode(_server.arg("token"));
    }

    _wsHost = urlDecode(_server.arg("host"));
    if (_wsHost.length() == 0) {
        _wsHost = "192.168.1.1";  // Default, user should set this
    }

    _configured = true;

    String response = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    response += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    response += "<title>Saved</title></head><body style='font-family:sans-serif;padding:20px;'>";
    response += "<h2>Configuration Saved!</h2>";
    response += "<p>WiFi: " + _wifiSsid + "</p>";
    response += "<p>Server: " + _wsHost + ":" + String(_wsPort) + "</p>";
    response += "<p>The device will now restart and connect.</p>";
    response += "</body></html>";

    _server.send(200, "text/html", response);

    // Give time for response to send
    delay(1000);
    stop();
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
    // Redirect all requests to root (captive portal behavior)
    _server.sendHeader("Location", "http://192.168.4.1/", true);
    _server.send(302, "text/plain", "");
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
button{background:#0f9d58;color:#fff;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px}
button:hover{background:#0d8a4d}
.section{background:#16213e;padding:15px;border-radius:8px;margin:15px 0}
label{font-weight:bold;color:#aaa}
.or{text-align:center;color:#666;margin:10px 0}
a{color:#0f9d58}
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
<h3>Camera Connection</h3>
<p style='color:#aaa;font-size:14px'>Get these from the QR code in Insta360 Link app (Settings > LAN Control)</p>

<label>QR Code URL (paste the full URL)</label>
<input type='text' name='qrurl' placeholder='insta360link://connect?port=...&token=...'>

<div class='or'>- OR enter manually -</div>

<label>Mac IP Address</label>
<input type='text' name='host' placeholder='192.168.1.100'>
<label>Port</label>
<input type='number' name='port' placeholder='From QR code'>
<label>Token</label>
<input type='text' name='token' placeholder='From QR code'>
</div>

<button type='submit'>Save & Connect</button>
</form>

<script>
function scanWifi(){
  document.getElementById('networks').innerHTML='Scanning...';
  fetch('/scan').then(r=>r.json()).then(nets=>{
    let html='<select onchange="document.getElementById(\'ssid\').value=this.value"><option>Select network...</option>';
    nets.forEach(n=>html+='<option>'+n+'</option>');
    html+='</select>';
    document.getElementById('networks').innerHTML=html;
  });
}
</script>
</body></html>)";
    return html;
}
