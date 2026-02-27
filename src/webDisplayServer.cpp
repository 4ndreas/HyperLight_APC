#include "webDisplayServer.h"

#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WebServer.h>

#include "config.h"
#include "networkMidiBridge.h"

namespace {
constexpr uint16_t kImageWidth = 64;
constexpr uint16_t kImageHeight = 64;
constexpr size_t kRowBytesIn = kImageWidth * 3;
constexpr size_t kRowBytesOut = kImageWidth * 2;
constexpr size_t kFrameBytesOut = kImageWidth * kImageHeight * 2;

WebServer g_server(80);
RGB_OLED_64x64** g_displays = nullptr;
size_t g_displayCount = 0;
fs::FS* g_storage = nullptr;
const char* g_storageName = "none";
File g_uploadFile;
int g_uploadDisplay = -1;
uint8_t g_frameOut[kFrameBytesOut];
uint32_t g_rebootAtMs = 0;

uint16_t readLe16(File& f) {
  uint8_t b0 = f.read();
  uint8_t b1 = f.read();
  return static_cast<uint16_t>(b0 | (b1 << 8));
}

uint32_t readLe32(File& f) {
  uint32_t b0 = f.read();
  uint32_t b1 = f.read();
  uint32_t b2 = f.read();
  uint32_t b3 = f.read();
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool parseIp(const String& text, IPAddress& out) {
  int a = -1, b = -1, c = -1, d = -1;
  if (sscanf(text.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }
  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
    return false;
  }
  out = IPAddress(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
  return true;
}

String filePathForDisplay(int idx) {
  return String("/disp") + String(idx) + ".bmp";
}

bool renderBmpToDisplay(int idx, const String& path) {
  if (idx < 0 || g_displays == nullptr || static_cast<size_t>(idx) >= g_displayCount || g_storage == nullptr) {
    return false;
  }

  File f = g_storage->open(path, FILE_READ);
  if (!f) {
    return false;
  }

  if (f.size() < 54) {
    f.close();
    return false;
  }

  if (readLe16(f) != 0x4D42) {
    f.close();
    return false;
  }

  (void)readLe32(f);
  (void)readLe16(f);
  (void)readLe16(f);
  uint32_t pixelOffset = readLe32(f);

  uint32_t dibSize = readLe32(f);
  if (dibSize < 40) {
    f.close();
    return false;
  }

  int32_t width = static_cast<int32_t>(readLe32(f));
  int32_t height = static_cast<int32_t>(readLe32(f));
  uint16_t planes = readLe16(f);
  uint16_t bpp = readLe16(f);
  uint32_t compression = readLe32(f);

  if (planes != 1 || compression != 0 || bpp != 24) {
    f.close();
    return false;
  }

  bool bottomUp = true;
  if (height < 0) {
    bottomUp = false;
    height = -height;
  }

  if (width != kImageWidth || height != kImageHeight) {
    f.close();
    return false;
  }

  uint32_t rowStride = ((static_cast<uint32_t>(width) * 3U + 3U) / 4U) * 4U;
  uint8_t rowIn[kRowBytesIn];
  RGB_OLED_64x64* disp = g_displays[idx];

  for (uint16_t y = 0; y < kImageHeight; y++) {
    uint32_t srcY = bottomUp ? (kImageHeight - 1U - y) : y;
    uint32_t rowPos = pixelOffset + srcY * rowStride;
    if (!f.seek(rowPos)) {
      f.close();
      return false;
    }

    size_t got = f.read(rowIn, kRowBytesIn);
    if (got != kRowBytesIn) {
      f.close();
      return false;
    }

    for (uint16_t x = 0; x < kImageWidth; x++) {
      uint8_t b = rowIn[x * 3 + 0];
      uint8_t g = rowIn[x * 3 + 1];
      uint8_t r = rowIn[x * 3 + 2];
#if OLED_BMP_SWAP_RB
      uint16_t c = rgb888To565(b, g, r);
#else
      uint16_t c = rgb888To565(r, g, b);
#endif
      size_t outIndex = y * kRowBytesOut + x * 2;
      g_frameOut[outIndex + 0] = static_cast<uint8_t>((c >> 8) & 0xFF);
      g_frameOut[outIndex + 1] = static_cast<uint8_t>(c & 0xFF);
    }
  }

  disp->write_ram(
      g_frameOut,
      OLED_64x64_START_ROW,
      OLED_64x64_START_COL,
      OLED_64x64_STOP_ROW,
      OLED_64x64_STOP_COL,
      kFrameBytesOut);
  disp->setCShigh();
  f.close();
  return true;
}

int parseDisplayArg(int minDisplay = 1) {
  if (!g_server.hasArg("display")) {
    return -1;
  }

  int idx = g_server.arg("display").toInt();
  if (idx < minDisplay || g_displays == nullptr || static_cast<size_t>(idx) >= g_displayCount) {
    return -1;
  }

  return idx;
}

void handleRoot() {
  NetworkIpConfig cfg = NetworkMidi_GetConfig();
  bool linkUp = NetworkMidi_IsLinkUp();
  IPAddress ip = NetworkMidi_GetLocalIP();

  String html;
  html.reserve(5000);
  html += "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:980px;margin:20px auto;padding:0 12px;}";
  html += "input,select,button{font-size:16px;margin:4px 0;}";
  html += "table{border-collapse:collapse;}td,th{border:1px solid #ccc;padding:6px 8px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;}";
  html += ".card{border:1px solid #ddd;border-radius:6px;padding:8px;}";
  html += ".thumb{width:64px;height:64px;border:1px solid #aaa;image-rendering:pixelated;}";
  html += "</style></head><body>";

  html += "<h2>OLED Control</h2>";
  html += "<p>Storage: ";
  html += g_storageName;
  html += "</p>";

  html += "<h3>Network Status</h3><table>";
  html += "<tr><th>Link</th><th>IP</th><th>Mode</th></tr><tr><td>";
  html += (linkUp ? "UP" : "DOWN");
  html += "</td><td>";
  html += ip.toString();
  html += "</td><td>";
  html += (cfg.dhcp ? "DHCP" : "STATIC");
  html += "</td></tr></table>";

  html += "<h3>Network Config</h3>";
  html += "<form method='POST' action='/netcfg'>";
  html += "<label>Mode:</label><select name='mode'>";
  html += String("<option value='dhcp'") + (cfg.dhcp ? " selected" : "") + ">DHCP</option>";
  html += String("<option value='static'") + (!cfg.dhcp ? " selected" : "") + ">Static</option>";
  html += "</select><br>";
  html += "IP <input name='ip' value='" + cfg.ip.toString() + "'><br>";
  html += "Gateway <input name='gateway' value='" + cfg.gateway.toString() + "'><br>";
  html += "Subnet <input name='subnet' value='" + cfg.subnet.toString() + "'><br>";
  html += "DNS1 <input name='dns1' value='" + cfg.dns1.toString() + "'><br>";
  html += "DNS2 <input name='dns2' value='" + cfg.dns2.toString() + "'><br>";
  html += "<button type='submit'>Save Network Config</button></form>";

  html += "<h3>Upload Image</h3>";
  html += "<p>Display 0 is reserved for IP/status. Upload targets 1.." + String((int)g_displayCount - 1) + ".</p>";
  html += "<p>Image format: BMP, uncompressed, 24-bit, exactly 64x64.</p>";
  html += "<form id='upf' method='POST' action='/upload?display=1' enctype='multipart/form-data'>";
  html += "<label>Display:</label><select name='display'>";
  for (size_t i = 1; i < g_displayCount; i++) {
    html += "<option value='" + String(i) + "'>" + String(i) + "</option>";
  }
  html += "</select><br>";
  html += "<input type='file' name='image' accept='.bmp' required><br>";
  html += "<button type='submit'>Upload and Show</button></form>";
  html += "<script>const f=document.getElementById('upf');const s=f.querySelector('select[name=display]');";
  html += "f.addEventListener('submit',()=>{f.action='/upload?display='+encodeURIComponent(s.value);});</script>";

  html += "<h3>Gallery</h3><div class='grid'>";
  for (size_t i = 1; i < g_displayCount; i++) {
    String p = filePathForDisplay(static_cast<int>(i));
    html += "<div class='card'><div>Display " + String(i) + "</div>";
    if (g_storage != nullptr && g_storage->exists(p)) {
      html += "<img class='thumb' src='/image?display=" + String(i) + "' alt='disp'>";
      html += "<div><a href='/show?display=" + String(i) + "'>show</a></div>";
    } else {
      html += "<div class='thumb' style='display:flex;align-items:center;justify-content:center;'>none</div>";
    }
    html += "</div>";
  }
  html += "</div></body></html>";

  g_server.send(200, "text/html", html);
}

void handleImage() {
  int idx = parseDisplayArg(1);
  if (idx < 0) {
    g_server.send(400, "text/plain", "Invalid display index");
    return;
  }

  String path = filePathForDisplay(idx);
  if (g_storage == nullptr || !g_storage->exists(path)) {
    g_server.send(404, "text/plain", "No image");
    return;
  }

  File f = g_storage->open(path, FILE_READ);
  if (!f) {
    g_server.send(500, "text/plain", "Open failed");
    return;
  }

  g_server.streamFile(f, "image/bmp");
  f.close();
}

void handleShow() {
  int idx = parseDisplayArg(1);
  if (idx < 0) {
    g_server.send(400, "text/plain", "Invalid display index");
    return;
  }

  String path = filePathForDisplay(idx);
  if (g_storage == nullptr || !g_storage->exists(path)) {
    g_server.send(404, "text/plain", "No image stored for this display");
    return;
  }

  if (!renderBmpToDisplay(idx, path)) {
    g_server.send(415, "text/plain", "Stored image format unsupported or corrupted");
    return;
  }

  g_server.sendHeader("Location", "/", true);
  g_server.send(303, "text/plain", "");
}

void handleNetCfg() {
  NetworkIpConfig cfg = NetworkMidi_GetConfig();
  if (g_server.hasArg("mode")) {
    cfg.dhcp = (g_server.arg("mode") != "static");
  }

  if (!cfg.dhcp) {
    IPAddress ip, gw, sn, d1, d2;
    if (!g_server.hasArg("ip") || !parseIp(g_server.arg("ip"), ip) ||
        !g_server.hasArg("gateway") || !parseIp(g_server.arg("gateway"), gw) ||
        !g_server.hasArg("subnet") || !parseIp(g_server.arg("subnet"), sn)) {
      g_server.send(400, "text/plain", "Invalid static IP settings");
      return;
    }

    cfg.ip = ip;
    cfg.gateway = gw;
    cfg.subnet = sn;

    if (g_server.hasArg("dns1") && parseIp(g_server.arg("dns1"), d1)) {
      cfg.dns1 = d1;
    }
    if (g_server.hasArg("dns2") && parseIp(g_server.arg("dns2"), d2)) {
      cfg.dns2 = d2;
    }
  }

  if (!NetworkMidi_SaveConfig(cfg)) {
    g_server.send(500, "text/plain", "Failed to save network config");
    return;
  }

  g_rebootAtMs = millis() + 1500;
  g_server.send(200, "text/html", "<html><body>Saved. Rebooting...<meta http-equiv='refresh' content='4;url=/'></body></html>");
}

void handleUploadFinalize() {
  if (g_uploadDisplay < 1) {
    g_server.send(400, "text/plain", "Upload failed: missing/invalid display index");
    return;
  }

  String path = filePathForDisplay(g_uploadDisplay);
  if (g_storage == nullptr || !g_storage->exists(path)) {
    g_server.send(500, "text/plain", "Upload failed: file not stored");
    return;
  }

  if (!renderBmpToDisplay(g_uploadDisplay, path)) {
    g_storage->remove(path);
    g_server.send(415, "text/plain", "Only uncompressed 24-bit BMP in 64x64 is supported");
    return;
  }

  g_uploadDisplay = -1;
  g_server.sendHeader("Location", "/", true);
  g_server.send(303, "text/plain", "");
}

void handleUploadData() {
  HTTPUpload& upload = g_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    g_uploadDisplay = parseDisplayArg(1);
    if (g_uploadDisplay < 1 || g_storage == nullptr) {
      return;
    }

    String path = filePathForDisplay(g_uploadDisplay);
    if (g_storage->exists(path)) {
      g_storage->remove(path);
    }
    g_uploadFile = g_storage->open(path, FILE_WRITE);
    if (!g_uploadFile) {
      g_uploadDisplay = -1;
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_uploadDisplay < 1 || !g_uploadFile) {
      return;
    }

    size_t written = g_uploadFile.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      g_uploadFile.close();
      g_storage->remove(filePathForDisplay(g_uploadDisplay));
      g_uploadDisplay = -1;
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (g_uploadFile) {
      g_uploadFile.close();
    }
  }
}
}  // namespace

void WebDisplayServer_Setup(RGB_OLED_64x64** displays, size_t count) {
  g_displays = displays;
  g_displayCount = count;

  if (SD.cardType() != CARD_NONE) {
    g_storage = &SD;
    g_storageName = "SD";
  } else if (SPIFFS.begin(true)) {
    g_storage = &SPIFFS;
    g_storageName = "SPIFFS";
  } else {
    Serial.println("WebDisplay: no storage available (SD/SPIFFS)");
    return;
  }

  for (size_t i = 1; i < g_displayCount; i++) {
    String path = filePathForDisplay(static_cast<int>(i));
    if (g_storage->exists(path)) {
      renderBmpToDisplay(static_cast<int>(i), path);
    }
  }

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/show", HTTP_GET, handleShow);
  g_server.on("/image", HTTP_GET, handleImage);
  g_server.on("/netcfg", HTTP_POST, handleNetCfg);
  g_server.on("/upload", HTTP_POST, handleUploadFinalize, handleUploadData);
  g_server.onNotFound([]() { g_server.send(404, "text/plain", "Not found"); });

  g_server.begin();
  Serial.printf("WebDisplay: HTTP server started on port 80, storage=%s\r\n", g_storageName);
}

void WebDisplayServer_Loop() {
  g_server.handleClient();

  if (g_rebootAtMs != 0 && millis() >= g_rebootAtMs) {
    ESP.restart();
  }
}
