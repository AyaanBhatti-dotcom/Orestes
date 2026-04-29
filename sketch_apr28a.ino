#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "bsp_cst816.h"
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include <BLEUtils.h>
#include <math.h>
#include <Preferences.h>

// --- DISPLAY PINS ---
#define LCD_SCLK 39
#define LCD_MOSI 38
#define LCD_MISO 40
#define LCD_DC   42
#define LCD_RST  -1
#define LCD_CS   45
#define LCD_BL    1

// --- TOUCH PINS ---
#define TP_SDA 48
#define TP_SCL 47

// --- DS COLOR PALETTE ---
// DS uses light gray body, white screens, pastel accents
#define BLACK      0x0000
#define WHITE      0xFFFF
#define DS_BG      0xCE79  // DS shell gray
#define DS_DARK    0x9CF3  // darker gray
#define DS_DARKER  0x6B4D  // darkest gray
#define DS_WHITE   0xFFFF  // screen white
#define DS_SCREEN  0xEF7D  // top screen light gray/green tint
#define DS_BLUE    0x035F  // DS blue accent
#define DS_LTBLUE  0x867F  // light blue
#define DS_RED     0xF8C3  // DS red (power light)
#define DS_GREEN   0x07E0  // green
#define DS_DKGREEN 0x03E0
#define DS_YELLOW  0xFFE0
#define DS_ORANGE  0xFD20
#define DS_PURPLE  0x881F
#define DS_LTPURP  0xD81F
#define DS_CYAN    0x07FF
#define DS_GOLD    0xFEA0
#define DS_GRAY    0x7BEF
#define DS_DKGRAY  0x4A69
#define DS_BORDER  0x8410  // card border gray
#define RED        0xF800
#define GREEN      0x07E0
#define CYAN       0x07FF
#define YELLOW     0xFFE0
#define ORANGE     0xFD20
#define MAGENTA    0xF81F
#define BLUE       0x001F
#define LTBLUE     0x04FF
#define GRAY       0x7BEF
#define DKGRAY     0x2945
#define DKGRAY2    0x18C3
#define GOLD       0xFEA0
#define LTPURPLE   0xD81F
#define PURPLE     0x780F

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, 240, 320);

Preferences prefs;

// --- SCREENS ---
#define SCREEN_NICKNAME   0
#define SCREEN_MENU       1
#define SCREEN_WIFI       2
#define SCREEN_BLE        3
#define SCREEN_MAP        4
#define SCREEN_STREETPASS 5
#define SCREEN_HANDSHAKE  6
#define SCREEN_CARDS      7
#define SCREEN_BADGES     8
#define SCREEN_WHOIMet    9
int currentScreen = SCREEN_NICKNAME;

// --- TOUCH ---
uint16_t touchX, touchY;
int lastTouchX = -1;
int lastTouchY = -1;
bool touchActive = false;

// --- NICKNAME ---
String myNickname = "";
String myDeviceID = "";
char nicknameBuffer[17] = "";
int nicknameLen = 0;

const char* kbRows[] = {
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM<OK"
};

// --- STREETPASS ---
#define ORESTES_SERVICE_UUID "4a6f8c2e-1234-5678-abcd-ef0987654321"
#define ORESTES_CHAR_UUID    "4a6f8c2e-1234-5678-abcd-ef0987654322"
#define ORESTES_MAGIC        "CYBSCAN1"

struct Encounter {
  String nickname;
  String deviceID;
  int rssi;
  int points;
  unsigned long timestamp;
};

#define MAX_ENCOUNTERS 20
Encounter encounters[MAX_ENCOUNTERS];
int encounterCount = 0;
int totalPoints = 0;

bool handshakePending = false;
String pendingNickname = "";
String pendingDeviceID = "";
int pendingRSSI = 0;

BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
BLEScan *pBLEScan = nullptr;
bool bleInitialized = false;

// --- BADGES --- (lowered requirements)
struct Badge {
  String name;
  String desc;
  uint16_t color;
  bool unlocked;
  int requirement;
};

Badge badges[] = {
  {"FIRST CONTACT", "Met your first user",       DS_GOLD,   false, 1},
  {"SOCIAL",        "Met 3 different users",      DS_CYAN,   false, 3},
  {"NETWORKER",     "Met 5 different users",      DS_GREEN,  false, 5},
  {"GHOST",         "Scanned 20+ WiFi networks",  DS_LTBLUE, false, 20},
  {"BLE HUNTER",    "Found 10+ BLE devices",      MAGENTA,   false, 10},
  {"EXPLORER",      "Logged 5+ map locations",    DS_ORANGE, false, 5},
  {"ELITE",         "Earned 200+ points",         DS_LTPURP, false, 200},
  {"LEGEND",        "Met 8 different users",      RED,       false, 8},
};
const int BADGE_COUNT = 8;

// --- WIFI ---
int wifiCount = 0;
int wifiSelected = 0;
int wifiScroll = 0;
bool wifiDetail = false;
int totalWifiSeen = 0;

// --- BLE ---
struct BLEEntry {
  String name;
  String mac;
  int rssi;
  bool connectable;
  String appearance;
  bool isOrestes;
};

#define MAX_BLE 30
BLEEntry bleDevices[MAX_BLE];
int bleCount = 0;
int bleScroll = 0;
int bleSelected = 0;
bool bleDetail = false;
int totalBLESeen = 0;

// --- MAP ---
#define MAX_SCAN_POINTS 50
#define MAP_X 8
#define MAP_Y 52
#define MAP_W 224
#define MAP_H 200
#define REF_RSSI  -40.0
#define PATH_LOSS  2.5

struct ScanPoint {
  float x, y;
  int wifiCount, bleCount;
  int strongestRSSI;
  String strongestSSID;
};

ScanPoint scanPoints[MAX_SCAN_POINTS];
int scanPointCount = 0;
int mapSelectedPoint = -1;
float currentX = 0, currentY = 0;
bool hasPosition = false;
float mapMinX = -10, mapMaxX = 10;
float mapMinY = -10, mapMaxY = 10;

int cardScroll = 0;
int whoMetScroll = 0;

// =====================
// POSITIONING
// =====================

float rssiToDistance(int rssi) {
  return pow(10.0, (REF_RSSI - rssi) / (10.0 * PATH_LOSS));
}

void estimatePosition() {
  if (wifiCount < 1) { hasPosition = false; return; }
  float totalWeight = 0, weightedX = 0, weightedY = 0;
  float anchorAngles[] = {0, 72, 144, 216, 288};
  int usedAnchors = min(wifiCount, 5);
  for (int i = 0; i < usedAnchors; i++) {
    int rssi = WiFi.RSSI(i);
    float dist = rssiToDistance(rssi);
    float weight = 1.0 / (dist * dist + 0.1);
    float angle = anchorAngles[i] * PI / 180.0;
    weightedX += 5.0 * cos(angle) * weight;
    weightedY += 5.0 * sin(angle) * weight;
    totalWeight += weight;
  }
  if (totalWeight > 0) {
    currentX = weightedX / totalWeight;
    currentY = weightedY / totalWeight;
    hasPosition = true;
  }
}

void autoMapBounds() {
  if (scanPointCount == 0) return;
  float minX = scanPoints[0].x, maxX = scanPoints[0].x;
  float minY = scanPoints[0].y, maxY = scanPoints[0].y;
  for (int i = 1; i < scanPointCount; i++) {
    minX = min(minX, scanPoints[i].x); maxX = max(maxX, scanPoints[i].x);
    minY = min(minY, scanPoints[i].y); maxY = max(maxY, scanPoints[i].y);
  }
  float padX = max((maxX - minX) * 0.3f, 2.0f);
  float padY = max((maxY - minY) * 0.3f, 2.0f);
  mapMinX = minX - padX; mapMaxX = maxX + padX;
  mapMinY = minY - padY; mapMaxY = maxY + padY;
}

int worldToScreenX(float wx) {
  return MAP_X + (int)((wx - mapMinX) / (mapMaxX - mapMinX) * MAP_W);
}
int worldToScreenY(float wy) {
  return MAP_Y + MAP_H - (int)((wy - mapMinY) / (mapMaxY - mapMinY) * MAP_H);
}

void recordScanPoint() {
  if (!hasPosition) return;
  if (scanPointCount >= MAX_SCAN_POINTS) {
    for (int i = 0; i < MAX_SCAN_POINTS - 1; i++) scanPoints[i] = scanPoints[i + 1];
    scanPointCount = MAX_SCAN_POINTS - 1;
  }
  ScanPoint sp;
  sp.x = currentX; sp.y = currentY;
  sp.wifiCount = wifiCount; sp.bleCount = bleCount;
  sp.strongestRSSI = -100; sp.strongestSSID = "";
  for (int i = 0; i < wifiCount; i++) {
    if (WiFi.RSSI(i) > sp.strongestRSSI) {
      sp.strongestRSSI = WiFi.RSSI(i);
      sp.strongestSSID = WiFi.SSID(i);
      if (sp.strongestSSID.length() == 0) sp.strongestSSID = "(hidden)";
    }
  }
  scanPoints[scanPointCount++] = sp;
  autoMapBounds();
}

// =====================
// SHARED HELPERS
// =====================

String signalBars(int rssi) {
  if (rssi >= -50) return "[####]";
  if (rssi >= -60) return "[### ]";
  if (rssi >= -70) return "[##  ]";
  if (rssi >= -80) return "[#   ]";
  return "[    ]";
}

uint16_t signalColor(int rssi) {
  if (rssi >= -50) return DS_GREEN;
  if (rssi >= -70) return DS_YELLOW;
  return DS_RED;
}

String encType(wifi_auth_mode_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN:         return "OPEN";
    case WIFI_AUTH_WEP:          return "WEP";
    case WIFI_AUTH_WPA_PSK:      return "WPA";
    case WIFI_AUTH_WPA2_PSK:     return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:     return "WPA3";
    default:                     return "????";
  }
}

// =====================
// DS UI COMPONENTS
// =====================

// Draw DS-style status bar at top
void drawDSStatusBar(String title) {
  // Dark bar
  gfx->fillRect(0, 0, 240, 18, DS_DARKER);
  // Left side — power light + title
  gfx->fillCircle(8, 9, 4, DS_RED);  // power LED
  gfx->fillCircle(8, 9, 2, 0xF9A0);  // glow
  gfx->setTextSize(1);
  gfx->setTextColor(DS_DARKER);
  gfx->setCursor(18, 5);
  gfx->print(title);
  // Right side — nickname + signal dots
  gfx->setCursor(170, 5);
  gfx->setTextColor(DS_LTBLUE);
  String nick = myNickname;
  if (nick.length() > 8) nick = nick.substring(0, 7);
  gfx->print(nick);
  // WiFi signal dots
  for (int i = 0; i < 3; i++) {
    gfx->fillRect(220 + i*5, 13 - i*3, 3, 3 + i*3,
                  i < 2 ? WHITE : DS_DKGRAY);
  }
  // Bottom border
  gfx->drawFastHLine(0, 18, 240, DS_BLUE);
}

// Draw DS-style white card
void drawDSCard(int x, int y, int w, int h, uint16_t accentColor) {
  gfx->fillRoundRect(x, y, w, h, 6, WHITE);
  gfx->drawRoundRect(x, y, w, h, 6, DS_BORDER);
  gfx->drawFastHLine(x+6, y+1, w-12, accentColor);  // colored top stripe
}

// Draw DS-style back button (L button style)
void drawDSBackButton() {
  gfx->fillRoundRect(2, 20, 72, 22, 5, DS_DARKER);
  gfx->drawRoundRect(2, 20, 72, 22, 5, DS_BLUE);
  gfx->setTextSize(1);
  gfx->setTextColor(WHITE);  // always white on dark button
  gfx->setCursor(10, 27);
  gfx->print("<< BACK");
}

// Draw DS bottom dock bar
void drawDSDock(int activeIcon) {
  gfx->fillRect(0, 282, 240, 38, DS_DARKER);
  gfx->drawFastHLine(0, 282, 240, DS_BLUE);
  gfx->setTextSize(1);
  gfx->setTextColor(DS_GRAY);
  gfx->setCursor(2, 295); gfx->print("L");
  gfx->setCursor(228, 295); gfx->print("R");

  // Arrow indicators
  gfx->setTextSize(1);
  gfx->setCursor(2, 295);  gfx->print("L");
  gfx->setCursor(228, 295); gfx->print("R");

  // 4 icon slots
  const char* icons[] = {"WIFI", "BLE", "MAP", "PASS"};
  uint16_t iconColors[] = {DS_GREEN, DS_LTBLUE, DS_ORANGE, DS_LTPURP};

  for (int i = 0; i < 4; i++) {
    int ix = 14 + i * 54;
    bool active = (i == activeIcon);
    if (active) {
      // Active icon pops up like DS
      gfx->fillRoundRect(ix - 2, 274, 44, 44, 6, WHITE);
      gfx->drawRoundRect(ix - 2, 274, 44, 44, 6, iconColors[i]);
      gfx->drawFastHLine(ix, 275, 40, iconColors[i]);
    } else {
      gfx->fillRoundRect(ix, 284, 40, 30, 5, DS_DARK);
      gfx->drawRoundRect(ix, 284, 40, 30, 5, DS_DKGRAY);
    }
    gfx->setTextSize(1);
    gfx->setTextColor(active ? iconColors[i] : DS_GRAY);
    int tx = ix + (active ? -2 : 0) + 20 - strlen(icons[i]) * 3;
    gfx->setCursor(tx, active ? 292 : 296);
    gfx->print(icons[i]);
  }
}

// Draw DS-style button
void drawDSButton(int x, int y, int w, int h, String label,
                  uint16_t bg, uint16_t border, uint16_t textColor) {
  gfx->fillRoundRect(x, y, w, h, 5, bg);
  gfx->drawRoundRect(x, y, w, h, 5, border);
  // Shine effect on top
  gfx->drawFastHLine(x+4, y+2, w-8, 0xFFFF);
  gfx->setTextSize(1);
  gfx->setTextColor(DS_DARKER);
  int tw = label.length() * 6;
  gfx->setCursor(x + (w-tw)/2, y + (h-8)/2);
  gfx->print(label);
}

// =====================
// BADGE SYSTEM
// =====================

void checkBadges() {
  if (encounterCount >= 1) badges[0].unlocked = true;
  if (encounterCount >= 3) badges[1].unlocked = true;
  if (encounterCount >= 5) badges[2].unlocked = true;
  if (totalWifiSeen >= 20) badges[3].unlocked = true;
  if (totalBLESeen >= 10)  badges[4].unlocked = true;
  if (scanPointCount >= 5) badges[5].unlocked = true;
  if (totalPoints >= 200)  badges[6].unlocked = true;
  if (encounterCount >= 8) badges[7].unlocked = true;
}

int countUnlockedBadges() {
  int c = 0;
  for (int i = 0; i < BADGE_COUNT; i++) if (badges[i].unlocked) c++;
  return c;
}

// =====================
// NICKNAME SCREEN
// =====================

void drawNicknameScreen() {
  // DS-style nickname setup
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Orestes");

  // Top screen area (like DS top screen)
  gfx->fillRoundRect(8, 22, 224, 90, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 90, 4, DS_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 32); gfx->print("Welcome to Orestes!");
  gfx->setCursor(20, 46); gfx->print("Please enter your nickname.");
  gfx->setCursor(20, 58); gfx->print("This will be shown to other");
  gfx->setCursor(20, 68); gfx->print("Orestes users you meet.");
  gfx->setCursor(20, 82); gfx->setTextColor(DS_BLUE);
  gfx->print("Max 16 characters.");

  // Divider
  gfx->drawFastHLine(0, 114, 240, DS_DARKER);

  // Bottom screen (touch area)
  gfx->fillRect(0, 115, 240, 205, DS_BG);

  // Name input box — DS style
  gfx->fillRoundRect(8, 120, 224, 22, 4, WHITE);
  gfx->drawRoundRect(8, 120, 224, 22, 4, DS_BLUE);
  gfx->setTextSize(1);
  String display = String(nicknameBuffer);
  if (display.length() == 0) {
    gfx->setTextColor(DS_GRAY); gfx->setCursor(14, 127);
    gfx->print("Tap keys below...");
  } else {
    gfx->setTextColor(DS_DARKER); gfx->setCursor(14, 127);
    gfx->print(display);
    gfx->setTextColor(DS_BLUE); gfx->print("_");
  }

  // DS-style keyboard
  int keyW = 21, keyH = 18, startY = 148;
  for (int row = 0; row < 3; row++) {
    String keys = kbRows[row];
    int rowLen = keys.length();
    int startX = (240 - rowLen * (keyW + 2)) / 2;
    for (int col = 0; col < rowLen; col++) {
      int kx = startX + col * (keyW + 2);
      int ky = startY + row * (keyH + 3);
      char k = keys[col];
      bool isSpecial = (k == '<' || k == 'O');
      // DS key style — white with border
      gfx->fillRoundRect(kx, ky, keyW, keyH, 3, isSpecial ? DS_BLUE : WHITE);
      gfx->drawRoundRect(kx, ky, keyW, keyH, 3, isSpecial ? DS_DARKER : DS_BORDER);
      gfx->drawFastHLine(kx+2, ky+1, keyW-4, isSpecial ? DS_LTBLUE : DS_LTBLUE);
      gfx->setTextSize(1);
      gfx->setTextColor(isSpecial ? WHITE : DS_DARKER);
      gfx->setCursor(kx + (keyW-6)/2, ky + 5);
      if (k == '<') gfx->print("<<");
      else if (k == 'O') gfx->print("OK");
      else gfx->print(k);
    }
  }

  // Space bar
  int spY = startY + 3*(keyH+3);
  gfx->fillRoundRect(40, spY, 160, keyH, 4, WHITE);
  gfx->drawRoundRect(40, spY, 160, keyH, 4, DS_BORDER);
  gfx->drawFastHLine(44, spY+1, 152, DS_LTBLUE);
  gfx->setTextSize(1); gfx->setTextColor(DS_GRAY);
  gfx->setCursor(100, spY+5); gfx->print("SPACE");
}

void handleNicknameKey(int tx, int ty) {
  int keyW = 21, keyH = 18, startY = 148;

  // Space bar
  int spY = startY + 3*(keyH+3);
  if (ty >= spY && ty <= spY+keyH && tx >= 40 && tx <= 200) {
    if (nicknameLen < 16) { nicknameBuffer[nicknameLen++] = ' '; nicknameBuffer[nicknameLen] = 0; }
    drawNicknameScreen(); return;
  }

  for (int row = 0; row < 3; row++) {
    String keys = kbRows[row];
    int rowLen = keys.length();
    int startX = (240 - rowLen*(keyW+2)) / 2;
    for (int col = 0; col < rowLen; col++) {
      int kx = startX + col*(keyW+2);
      int ky = startY + row*(keyH+3);
      if (tx >= kx && tx <= kx+keyW && ty >= ky && ty <= ky+keyH) {
        char k = keys[col];
        if (k == '<') {
          if (nicknameLen > 0) nicknameBuffer[--nicknameLen] = 0;
        } else if (k == 'O') {
          if (nicknameLen > 0) {
            myNickname = String(nicknameBuffer);
            prefs.putString("nickname", myNickname);
            currentScreen = SCREEN_MENU;
            drawMenu();
          }
        } else {
          if (nicknameLen < 16) { nicknameBuffer[nicknameLen++] = k; nicknameBuffer[nicknameLen] = 0; }
        }
        drawNicknameScreen(); return;
      }
    }
  }
}

// =====================
// MAIN MENU — DS Style
// =====================

void drawMenu() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Orestes");

  // Top screen — DS style info panel
  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_BORDER);

  // Top screen content
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(16, 30); gfx->print("ORESTES ");
  gfx->setTextColor(DS_BLUE); gfx->print("SCAN");
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(16, 52); gfx->print("User: ");
  gfx->setTextColor(DS_BLUE); gfx->print(myNickname);
  gfx->setTextColor(DS_DARKER); gfx->setCursor(16, 64);
  gfx->print("WiFi: "); gfx->print(wifiCount);
  gfx->print("  BLE: "); gfx->print(bleCount);
  gfx->print("  Pts: "); gfx->print(totalPoints);
  gfx->setCursor(16, 76); gfx->setTextColor(DS_DKGRAY);
  gfx->print("Tap an icon to launch");

  // Divider — like DS hinge
  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->drawFastHLine(0, 103, 240, BLACK);
  gfx->drawFastHLine(0, 110, 240, BLACK);

  // Bottom screen — icon grid like DS menu
  gfx->fillRect(0, 111, 240, 171, DS_BG);

  // 2x2 icon grid — DS style
  // Row 1
  // WiFi icon
  gfx->fillRoundRect(14, 118, 96, 70, 8, WHITE);
  gfx->drawRoundRect(14, 118, 96, 70, 8, DS_GREEN);
  gfx->drawFastHLine(18, 119, 88, DS_GREEN);
  gfx->drawCircle(62, 143, 16, DS_GREEN);
  gfx->drawCircle(62, 143, 10, DS_DKGREEN);
  gfx->fillCircle(62, 143, 4, DS_GREEN);
  gfx->fillRect(59, 143, 6, 8, WHITE);
  gfx->fillRect(61, 149, 2, 4, DS_GREEN);
  gfx->setTextSize(1); gfx->setTextColor(DS_GREEN);
  gfx->setCursor(26, 168); gfx->print("WIFI SCAN");

  // BLE icon
  gfx->fillRoundRect(130, 118, 96, 70, 8, WHITE);
  gfx->drawRoundRect(130, 118, 96, 70, 8, DS_LTBLUE);
  gfx->drawFastHLine(134, 119, 88, DS_LTBLUE);
  gfx->fillRoundRect(164, 128, 28, 28, 4, DS_LTBLUE);
  gfx->setTextSize(3); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(168, 130); gfx->print("B");
  gfx->setTextSize(1); gfx->setTextColor(DS_LTBLUE);
  gfx->setCursor(142, 168); gfx->print("BLE SCAN");

  // Row 2
  // Map icon
  gfx->fillRoundRect(14, 196, 96, 70, 8, WHITE);
  gfx->drawRoundRect(14, 196, 96, 70, 8, DS_ORANGE);
  gfx->drawFastHLine(18, 197, 88, DS_ORANGE);
  gfx->drawRect(42, 208, 40, 30, DS_ORANGE);
  gfx->drawFastHLine(42, 223, 40, DS_ORANGE);
  gfx->drawFastVLine(62, 208, 30, DS_ORANGE);
  gfx->fillCircle(52, 216, 3, DS_ORANGE);
  gfx->fillCircle(72, 228, 3, DS_GREEN);
  gfx->setTextSize(1); gfx->setTextColor(DS_ORANGE);
  gfx->setCursor(24, 246); gfx->print("SCAN MAP");

  // StreetPass icon
  gfx->fillRoundRect(130, 196, 96, 70, 8, WHITE);
  gfx->drawRoundRect(130, 196, 96, 70, 8, DS_LTPURP);
  gfx->drawFastHLine(134, 197, 88, DS_LTPURP);
  gfx->drawCircle(178, 221, 16, DS_LTPURP);
  gfx->drawCircle(178, 221, 10, DS_PURPLE);
  gfx->fillCircle(178, 221, 5, DS_LTPURP);
  gfx->setTextSize(1); gfx->setTextColor(DS_LTPURP);
  gfx->setCursor(138, 246); gfx->print("STREETPASS");

  // Bottom dock — no active icon on menu
  gfx->fillRect(0, 282, 240, 38, DS_DARKER);
  gfx->drawFastHLine(0, 282, 240, DS_BLUE);
  gfx->setTextSize(1); gfx->setTextColor(DS_GRAY);
  gfx->setCursor(60, 295); gfx->print("TAP AN ICON TO START");
}

// =====================
// WIFI SCREEN
// =====================

void drawWifiList() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("WiFi Scanner");
  drawDSBackButton();

  // Count badge
  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_GREEN);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(176, 25); gfx->print("n="); gfx->print(wifiCount);

  // List area — white card
  gfx->fillRoundRect(4, 38, 232, 236, 4, WHITE);
  gfx->drawRoundRect(4, 38, 232, 236, 4, DS_BORDER);

  int rowH = 26, visibleRows = 9;
  for (int i = 0; i < visibleRows; i++) {
    int idx = i + wifiScroll;
    if (idx >= wifiCount) break;
    int y = 40 + i * rowH;
    bool selected = (idx == wifiSelected);

    if (selected) {
      gfx->fillRect(5, y, 230, rowH-1, DS_LTBLUE);
    } else if (i % 2 == 0) {
      gfx->fillRect(5, y, 230, rowH-1, 0xF7DE);  // slight tint
    }

    gfx->setCursor(10, y+3); gfx->setTextSize(1);


    String ssid = WiFi.SSID(idx);
    if (ssid.length() == 0) ssid = "(hidden)";
    if (ssid.length() > 16) ssid = ssid.substring(0, 15) + "~";
    gfx->print(ssid);

    String enc = encType(WiFi.encryptionType(idx));
    gfx->setCursor(168, y+3);
    gfx->setTextColor(enc == "OPEN" ? RED : DS_GREEN);
    gfx->print(enc);

    int rssi = WiFi.RSSI(idx);
    gfx->setCursor(10, y+14); gfx->setTextColor(signalColor(rssi));
    gfx->print(signalBars(rssi));
    gfx->setTextColor(selected ? WHITE : DS_DKGRAY);
    gfx->print(" CH:"); gfx->print(WiFi.channel(idx));

    gfx->drawFastHLine(5, y+rowH-1, 230, DS_BORDER);
  }

  // Bottom buttons — DS style
  drawDSButton(8, 282, 70, 18, "RESCAN", DS_GREEN, DS_DKGREEN, WHITE);
  drawDSDock(0);
}

void drawWifiDetail(int idx) {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Network Info");
  drawDSBackButton();

  // Top info card
  drawDSCard(4, 38, 232, 50, DS_GREEN);
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(12, 46);
  String ssid = WiFi.SSID(idx);
  if (ssid.length() == 0) ssid = "(hidden)";
  if (ssid.length() > 11) ssid = ssid.substring(0, 10) + "~";
  gfx->print(ssid);
  gfx->setTextSize(1);
  String enc = encType(WiFi.encryptionType(idx));
  gfx->setTextColor(enc == "OPEN" ? RED : DS_GREEN);
  gfx->setCursor(12, 68); gfx->print(enc);
  gfx->setTextColor(DS_DKGRAY); gfx->print("  CH:"); gfx->print(WiFi.channel(idx));

  // Detail rows
  int y = 96;
  auto drawRow = [&](String label, String value, uint16_t vColor) {
    gfx->fillRoundRect(4, y, 232, 22, 3, WHITE);
    gfx->drawRoundRect(4, y, 232, 22, 3, DS_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
    gfx->setCursor(10, y+7); gfx->print(label);
    gfx->setTextColor(vColor);
    gfx->setCursor(90, y+7); gfx->print(value);
    y += 26;
  };

  int rssi = WiFi.RSSI(idx);
  drawRow("BSSID:", WiFi.BSSIDstr(idx), DS_BLUE);
  drawRow("SIGNAL:", signalBars(rssi) + " " + String(rssi) + "dBm", signalColor(rssi));
  drawRow("SECURITY:", enc, enc == "OPEN" ? RED : DS_GREEN);
  drawRow("DISTANCE:", String(rssiToDistance(rssi), 1) + "m", DS_ORANGE);

  drawDSDock(0);
}

void doWifiScan() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("WiFi Scanner");

  // Top screen scanning animation
  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 40); gfx->print("Searching for networks...");

  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->fillRect(0, 111, 240, 171, DS_BG);

  for (int i = 0; i < 5; i++) {
    gfx->fillRect(20, 125, 200, 20, DS_BG);
    gfx->fillRoundRect(20, 125, i * 40, 16, 4, DS_GREEN);
    gfx->drawRoundRect(20, 125, 200, 16, 4, DS_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
    gfx->setCursor(20, 150); gfx->print("Scanning channel ");
    gfx->print(i * 3 + 1); gfx->print("...");
    delay(300);
  }

  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  wifiCount = WiFi.scanNetworks();
  totalWifiSeen += wifiCount;
  wifiSelected = 0; wifiScroll = 0; wifiDetail = false;
  estimatePosition(); recordScanPoint(); checkBadges();
  drawWifiList();
}

// =====================
// BLE SCREEN
// =====================

String getAppearance(uint16_t appearance) {
  switch (appearance >> 6) {
    case 1:  return "Phone";      case 2:  return "Computer";
    case 3:  return "Watch";      case 4:  return "Clock";
    case 5:  return "Display";    case 6:  return "Remote";
    case 7:  return "Glasses";    case 10: return "Sensor";
    case 15: return "Headphones"; case 31: return "Speaker";
    case 33: return "Keyboard";   case 34: return "Mouse";
    default: return "Unknown";
  }
}

class BLECallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (bleCount >= MAX_BLE) return;
    String mac = dev.getAddress().toString().c_str();
    bool isCS = dev.haveServiceUUID() &&
                dev.isAdvertisingService(BLEUUID(ORESTES_SERVICE_UUID));
    for (int i = 0; i < bleCount; i++) {
      if (bleDevices[i].mac == mac) { bleDevices[i].rssi = dev.getRSSI(); return; }
    }
    bleDevices[bleCount].mac = mac;
    bleDevices[bleCount].name = dev.haveName() ? dev.getName().c_str() : "Unknown";
    bleDevices[bleCount].rssi = dev.getRSSI();
    bleDevices[bleCount].connectable = dev.isConnectable();
    bleDevices[bleCount].appearance = dev.haveAppearance() ? getAppearance(dev.getAppearance()) : "Unknown";
    bleDevices[bleCount].isOrestes = isCS;
    bleCount++;
  }
};

void drawBLEList() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("BLE Scanner");
  drawDSBackButton();

  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_LTBLUE);
  gfx->setCursor(176, 25); gfx->print("n="); gfx->print(bleCount);

  gfx->fillRoundRect(4, 38, 232, 236, 4, WHITE);
  gfx->drawRoundRect(4, 38, 232, 236, 4, DS_BORDER);

  int rowH = 26, visibleRows = 9;
  for (int i = 0; i < visibleRows; i++) {
    int idx = i + bleScroll;
    if (idx >= bleCount) break;
    int y = 40 + i * rowH;
    bool selected = (idx == bleSelected);
    bool isCS = bleDevices[idx].isOrestes;

    if (selected) {
      gfx->fillRect(5, y, 230, rowH-1, DS_LTBLUE);
    } else if (i % 2 == 0) {
      gfx->fillRect(5, y, 230, rowH-1, 0xF7DE);
    }

    gfx->setCursor(10, y+3); gfx->setTextSize(1);
    gfx->setTextColor(selected ? WHITE : (isCS ? DS_PURPLE : BLACK));

    String name = bleDevices[idx].name;
    if (name.length() > 14) name = name.substring(0, 13) + "~";
    gfx->print(name);
    if (isCS) { gfx->setTextColor(DS_LTPURP); gfx->print(" [CS]"); }

    gfx->setCursor(168, y+3);
    gfx->setTextColor(bleDevices[idx].connectable ? DS_GREEN : RED);
    gfx->print(bleDevices[idx].connectable ? "CONN" : "NCON");

    int rssi = bleDevices[idx].rssi;
    gfx->setCursor(10, y+14); gfx->setTextColor(signalColor(rssi));
    gfx->print(rssi); gfx->print("dBm");
    gfx->setTextColor(DS_DKGRAY);
    gfx->print("  "); gfx->print(bleDevices[idx].appearance.substring(0,8));

    gfx->drawFastHLine(5, y+rowH-1, 230, DS_BORDER);
  }

  drawDSButton(8, 282, 70, 18, "RESCAN", DS_LTBLUE, DS_BLUE, WHITE);
  drawDSDock(1);
}

void drawBLEDetail(int idx) {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Device Info");
  drawDSBackButton();

  drawDSCard(4, 38, 232, 50, DS_LTBLUE);
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(12, 46);
  String name = bleDevices[idx].name;
  if (name.length() > 10) name = name.substring(0, 9) + "~";
  gfx->print(name);
  if (bleDevices[idx].isOrestes) {
    gfx->setTextSize(1); gfx->setTextColor(DS_LTPURP);
    gfx->setCursor(12, 68); gfx->print("[Orestes User]");
  } else {
    gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
    gfx->setCursor(12, 68); gfx->print(bleDevices[idx].appearance);
  }

  int y = 96;
  auto drawRow = [&](String label, String value, uint16_t vColor) {
    gfx->fillRoundRect(4, y, 232, 22, 3, WHITE);
    gfx->drawRoundRect(4, y, 232, 22, 3, DS_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
    gfx->setCursor(10, y+7); gfx->print(label);
    gfx->setTextColor(vColor);
    gfx->setCursor(90, y+7); gfx->print(value);
    y += 26;
  };

  int rssi = bleDevices[idx].rssi;
  drawRow("MAC:", bleDevices[idx].mac, DS_BLUE);
  drawRow("SIGNAL:", signalBars(rssi) + " " + String(rssi), signalColor(rssi));
  drawRow("CONNECT:", bleDevices[idx].connectable ? "YES" : "NO",
          bleDevices[idx].connectable ? DS_GREEN : RED);

  drawDSDock(1);
}

void doBLEScan() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("BLE Scanner");

  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 50); gfx->print("Scanning for BLE devices...");

  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->fillRect(0, 111, 240, 171, DS_BG);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 125); gfx->print("Searching for BLE devices...");
 
for (int i = 0; i < 5; i++) {
  gfx->fillRect(20, 145, 200, 16, DS_BG);
  gfx->fillRoundRect(20, 145, i * 40, 16, 4, DS_LTBLUE);
  gfx->drawRoundRect(20, 145, 200, 16, 4, DS_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->fillRect(20, 168, 200, 10, DS_BG);
  gfx->setCursor(20, 168); gfx->print("Scanning channel ");
  gfx->print(i * 3 + 1); gfx->print("...");
  delay(300);
}

  bleCount = 0; bleScroll = 0; bleSelected = 0; bleDetail = false;

  BLEDevice::stopAdvertising();
  delay(100);

  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new BLECallback(), true);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->clearResults();
  scan->start(5, false);

  totalBLESeen += bleCount;
  checkBadges();
  BLEDevice::startAdvertising();
  drawBLEList();
}

// =====================
// MAP SCREEN
// =====================

void drawMapScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Scan Map");
  drawDSBackButton();

  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_ORANGE);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(176, 25); gfx->print("pts="); gfx->print(scanPointCount);

  // Map card — white background like DS
  gfx->fillRoundRect(MAP_X-2, MAP_Y-2, MAP_W+4, MAP_H+4, 4, WHITE);
  gfx->drawRoundRect(MAP_X-2, MAP_Y-2, MAP_W+4, MAP_H+4, 4, DS_ORANGE);

  // Grid
  for (int gx = MAP_X; gx < MAP_X+MAP_W; gx += MAP_W/4)
    gfx->drawFastVLine(gx, MAP_Y, MAP_H, 0xE71C);
  for (int gy = MAP_Y; gy < MAP_Y+MAP_H; gy += MAP_H/4)
    gfx->drawFastHLine(MAP_X, gy, MAP_W, 0xE71C);

  // Origin
  int ox = worldToScreenX(0), oy = worldToScreenY(0);
  if (ox >= MAP_X && ox <= MAP_X+MAP_W && oy >= MAP_Y && oy <= MAP_Y+MAP_H) {
    gfx->drawFastHLine(ox-5, oy, 11, DS_DKGRAY);
    gfx->drawFastVLine(ox, oy-5, 11, DS_DKGRAY);
  }

  // Scan dots
  for (int i = 0; i < scanPointCount; i++) {
    int sx = worldToScreenX(scanPoints[i].x);
    int sy = worldToScreenY(scanPoints[i].y);
    if (sx < MAP_X || sx > MAP_X+MAP_W || sy < MAP_Y || sy > MAP_Y+MAP_H) continue;
    uint16_t dotColor = scanPoints[i].wifiCount >= 10 ? DS_GREEN :
                        scanPoints[i].wifiCount >= 5  ? DS_YELLOW : RED;
    if (i == mapSelectedPoint) {
      gfx->fillCircle(sx, sy, 6, DS_BLUE);
      gfx->fillCircle(sx, sy, 4, dotColor);
    } else {
      gfx->fillCircle(sx, sy, 4, dotColor);
      gfx->drawCircle(sx, sy, 5, DS_DKGRAY);
    }
    gfx->setCursor(sx+6, sy-3); gfx->setTextColor(DS_DARKER); gfx->setTextSize(1);
    gfx->print(i+1);
  }

  // Current position — DS style blue dot
  if (hasPosition) {
    int cx = worldToScreenX(currentX), cy = worldToScreenY(currentY);
    if (cx >= MAP_X && cx <= MAP_X+MAP_W && cy >= MAP_Y && cy <= MAP_Y+MAP_H) {
      gfx->fillCircle(cx, cy, 5, DS_BLUE);
      gfx->fillCircle(cx, cy, 3, WHITE);
      gfx->drawCircle(cx, cy, 7, DS_LTBLUE);
    }
  }

  // Info panel below map
  int infoY = MAP_Y + MAP_H + 4;
  if (mapSelectedPoint >= 0 && mapSelectedPoint < scanPointCount) {
    ScanPoint &sp = scanPoints[mapSelectedPoint];
    drawDSCard(4, infoY, 232, 46, DS_ORANGE);
    gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
    gfx->setCursor(10, infoY+6);
    gfx->print("PT"); gfx->print(mapSelectedPoint+1);
    gfx->print("  WiFi:"); gfx->print(sp.wifiCount);
    gfx->print("  BLE:"); gfx->print(sp.bleCount);
    gfx->setCursor(10, infoY+18); gfx->setTextColor(DS_GREEN);
    gfx->print("Best: ");
    String ssid = sp.strongestSSID;
    if (ssid.length() > 12) ssid = ssid.substring(0, 11) + "~";
    gfx->print(ssid);
    gfx->setTextColor(signalColor(sp.strongestRSSI));
    gfx->print(" "); gfx->print(sp.strongestRSSI); gfx->print("dBm");
    gfx->setCursor(10, infoY+30); gfx->setTextColor(DS_BLUE);
    gfx->print("Pos: ("); gfx->print(sp.x,1); gfx->print("m, ");
    gfx->print(sp.y,1); gfx->print("m)");
  } else {
    gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
    gfx->setCursor(10, infoY+8);
    gfx->print(scanPointCount == 0 ? "Scan WiFi to add points" : "Tap a dot for details");
  }

  drawDSDock(2);
}

void handleMapTap(int tx, int ty) {
  if (tx >= MAP_X && tx <= MAP_X+MAP_W && ty >= MAP_Y && ty <= MAP_Y+MAP_H) {
    int closest = -1; float closestDist = 15;
    for (int i = 0; i < scanPointCount; i++) {
      int sx = worldToScreenX(scanPoints[i].x);
      int sy = worldToScreenY(scanPoints[i].y);
      float d = sqrt((float)((tx-sx)*(tx-sx)+(ty-sy)*(ty-sy)));
      if (d < closestDist) { closestDist = d; closest = i; }
    }
    mapSelectedPoint = (closest >= 0 && closest != mapSelectedPoint) ? closest : -1;
    drawMapScreen();
  }
}

// =====================
// STREETPASS BLE
// =====================

class CSServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) {}
  void onDisconnect(BLEServer* s) { s->startAdvertising(); }
};

class CSCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String val = c->getValue().c_str();
    if (val.startsWith(ORESTES_MAGIC)) {
      int sep1 = val.indexOf('|');
      int sep2 = val.lastIndexOf('|');
      if (sep1 > 0 && sep2 > sep1) {
        String nick = val.substring(sep1+1, sep2);
        String devid = val.substring(sep2+1);
        bool alreadyMet = false;
        for (int i = 0; i < encounterCount; i++) {
          if (encounters[i].deviceID == devid) { alreadyMet = true; break; }
        }
        if (!handshakePending && !alreadyMet) {
          pendingNickname = nick;
          pendingDeviceID = devid;
          handshakePending = true;
        }
      }
    }
  }
};

void startOrestesServer() {
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new CSServerCallbacks());
  BLEService *svc = pServer->createService(ORESTES_SERVICE_UUID);
  pCharacteristic = svc->createCharacteristic(
    ORESTES_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new CSCharCallbacks());
  pCharacteristic->setValue(myDeviceID.c_str());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(ORESTES_SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

class SPScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.haveServiceUUID() &&
        dev.isAdvertisingService(BLEUUID(ORESTES_SERVICE_UUID))) {
      String mac = dev.getAddress().toString().c_str();
      bool alreadyMet = false;
      for (int i = 0; i < encounterCount; i++) {
        if (encounters[i].deviceID == mac) { alreadyMet = true; break; }
      }
      if (!alreadyMet && !handshakePending) {
        pendingNickname = dev.haveName() ? dev.getName().c_str() : "Unknown";
        pendingDeviceID = mac;
        pendingRSSI = dev.getRSSI();
        handshakePending = true;
      }
    }
  }
};

// =====================
// STREETPASS SCREEN
// =====================

void drawStreetPassScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("StreetPass");
  drawDSBackButton();

  // Profile card — DS top screen style
  drawDSCard(4, 38, 232, 60, DS_LTPURP);
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY); gfx->setCursor(12, 44);
  gfx->print("YOUR PROFILE");
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER); gfx->setCursor(12, 56);
  gfx->print(myNickname);
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY); gfx->setCursor(12, 78);
  gfx->print("Points: "); gfx->setTextColor(DS_GOLD); gfx->print(totalPoints);
  gfx->setTextColor(DS_DKGRAY); gfx->print("  Met: "); gfx->setTextColor(DS_LTPURP);
  gfx->print(encounterCount);
  gfx->setTextColor(DS_DKGRAY); gfx->print("  Badges: "); gfx->setTextColor(DS_ORANGE);
  gfx->print(countUnlockedBadges());

  // Action buttons — DS style in a row
  drawDSButton(4,  106, 70, 24, "SCAN",    DS_LTPURP, DS_PURPLE,  WHITE);
  drawDSButton(80, 106, 70, 24, "MET",     DS_GREEN,  DS_DKGREEN, WHITE);
  drawDSButton(156,106, 70, 24, "BADGES",  DS_ORANGE, DS_DARKER,  WHITE);
  drawDSButton(4,  136, 70, 24, "CARDS",   DS_BLUE,   DS_DARKER,  WHITE);

  // Recent list
  gfx->drawFastHLine(4, 166, 232, DS_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(4, 170); gfx->print("RECENT:");

  if (encounterCount == 0) {
    gfx->fillRoundRect(4, 180, 232, 60, 4, WHITE);
    gfx->drawRoundRect(4, 180, 232, 60, 4, DS_BORDER);
    gfx->setTextColor(DS_GRAY); gfx->setCursor(20, 196);
    gfx->print("No encounters yet!");
    gfx->setCursor(20, 210); gfx->print("Tap SCAN to find users.");
  } else {
    int shown = min(encounterCount, 4);
    for (int i = encounterCount-1; i >= encounterCount-shown; i--) {
      int y = 180 + (encounterCount-1-i)*22;
      gfx->fillRoundRect(4, y, 232, 20, 3, WHITE);
      gfx->drawRoundRect(4, y, 232, 20, 3, DS_BORDER);
      gfx->setCursor(10, y+6); gfx->setTextColor(DS_DARKER);
      gfx->print(encounters[i].nickname);
      gfx->setCursor(130, y+6); gfx->setTextColor(DS_GOLD);
      gfx->print("+"); gfx->print(encounters[i].points); gfx->print("pts");
      gfx->setCursor(185, y+6); gfx->setTextColor(signalColor(encounters[i].rssi));
      gfx->print(encounters[i].rssi); gfx->print("dBm");
    }
  }

  drawDSDock(3);
}

// =====================
// WHO I MET SCREEN
// =====================

void drawWhoIMetScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("Who I Met");
  drawDSBackButton();

  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_LTPURP);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(176, 25); gfx->print(encounterCount); gfx->print(" met");

  if (encounterCount == 0) {
    drawDSCard(4, 38, 232, 200, DS_LTPURP);
    gfx->setTextSize(1); gfx->setTextColor(DS_GRAY);
    gfx->setCursor(20, 120); gfx->print("Nobody yet!");
    gfx->setCursor(20, 134); gfx->print("Go find some Orestes users.");
    drawDSDock(3);
    return;
  }

  // List all encounters
  gfx->fillRoundRect(4, 38, 232, 236, 4, WHITE);
  gfx->drawRoundRect(4, 38, 232, 236, 4, DS_BORDER);

  int rowH = 52;
  int visibleRows = 4;

  for (int i = 0; i < visibleRows; i++) {
    int idx = i + whoMetScroll;
    if (idx >= encounterCount) break;
    int y = 40 + i * rowH;

    if (i % 2 == 0) gfx->fillRect(5, y, 230, rowH-1, 0xF7DE);

    // Avatar circle
    gfx->fillCircle(28, y+26, 18, DS_LTPURP);
    gfx->drawCircle(28, y+26, 18, DS_PURPLE);
    gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
    char initial = encounters[idx].nickname.length() > 0 ? encounters[idx].nickname[0] : '?';
    gfx->setCursor(21, y+18); gfx->print(initial);

    // Name
    gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
    gfx->setCursor(54, y+8); gfx->print(encounters[idx].nickname);

    // Points badge
    gfx->fillRoundRect(160, y+6, 60, 14, 4, DS_GOLD);
    gfx->setTextColor(DS_DARKER); gfx->setCursor(165, y+10);
    gfx->print("+"); gfx->print(encounters[idx].points); gfx->print(" pts");

    // Signal
    gfx->setCursor(54, y+22);
    gfx->setTextColor(signalColor(encounters[idx].rssi));
    gfx->print(signalBars(encounters[idx].rssi));
    gfx->print(" "); gfx->print(encounters[idx].rssi); gfx->print("dBm");

    // Device ID (truncated)
    gfx->setCursor(54, y+36); gfx->setTextColor(DS_DKGRAY);
    gfx->print(encounters[idx].deviceID.substring(0, 17));

    // Rarity label
    uint16_t rColor = encounters[idx].points >= 200 ? DS_GOLD :
                      encounters[idx].points >= 150 ? DS_LTBLUE : DS_GRAY;
    String rLabel = encounters[idx].points >= 200 ? "GOLD" :
                    encounters[idx].points >= 150 ? "SILVER" : "BRONZE";
    gfx->fillRoundRect(162, y+26, 56, 12, 3, rColor);
    gfx->setTextColor(DS_DARKER); gfx->setCursor(167, y+30);
    gfx->print(rLabel);

    gfx->drawFastHLine(5, y+rowH-1, 230, DS_BORDER);
  }

  // Scroll arrows
  if (whoMetScroll > 0)
    drawDSButton(4, 282, 50, 16, "^ UP", DS_LTPURP, DS_PURPLE, WHITE);
  if (whoMetScroll + visibleRows < encounterCount)
    drawDSButton(186, 282, 50, 16, "DN v", DS_LTPURP, DS_PURPLE, WHITE);

  drawDSDock(3);
}

// =====================
// HANDSHAKE SCREEN
// =====================

void drawHandshakeScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("StreetPass Alert!");

  // Top screen — like DS notification
  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_LTPURP);
  gfx->drawFastHLine(12, 23, 216, DS_LTPURP);

  // Pulsing circles
  gfx->drawCircle(120, 62, 30, DS_LTPURP);
  gfx->drawCircle(120, 62, 20, DS_PURPLE);
  gfx->fillCircle(120, 62, 10, DS_LTPURP);
  gfx->fillCircle(86, 52, 6, DS_BLUE);
  gfx->setTextColor(DS_DARKER); gfx->setTextSize(1);
  gfx->setCursor(83, 49); gfx->print("?");

  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 92); gfx->print("A Orestes user is nearby!");

  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->fillRect(0, 111, 240, 171, DS_BG);

  // Encounter card
  drawDSCard(8, 116, 224, 80, DS_LTPURP);

  // Avatar
  gfx->fillCircle(36, 156, 20, DS_LTPURP);
  gfx->drawCircle(36, 156, 20, DS_PURPLE);
  gfx->setTextSize(3); gfx->setTextColor(DS_DARKER);
  char initial = pendingNickname.length() > 0 ? pendingNickname[0] : '?';
  gfx->setCursor(27, 144); gfx->print(initial);

  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(64, 124); gfx->print(pendingNickname);
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(64, 148);
  gfx->print(pendingDeviceID.substring(0, 14));
  gfx->setCursor(64, 162);
  gfx->setTextColor(signalColor(pendingRSSI));
  gfx->print(signalBars(pendingRSSI));
  gfx->print(" "); gfx->print(pendingRSSI); gfx->print("dBm");

  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(12, 204); gfx->print("Both players must tap CONNECT!");

  // DS-style A/B buttons
  drawDSButton(14,  224, 100, 34, "A  CONNECT", DS_LTPURP, DS_PURPLE,  WHITE);
  drawDSButton(126, 224, 100, 34, "B  IGNORE",  WHITE,     DS_BORDER,  DS_DARKER);
}

void confirmHandshake() {
  BLEClient *client = BLEDevice::createClient();
  BLEAddress addr(pendingDeviceID.c_str());
  if (client->connect(addr)) {
    BLERemoteService *svc = client->getService(ORESTES_SERVICE_UUID);
    if (svc) {
      BLERemoteCharacteristic *ch = svc->getCharacteristic(ORESTES_CHAR_UUID);
      if (ch && ch->canWrite()) {
        String payload = String(ORESTES_MAGIC) + "|" + myNickname + "|" + myDeviceID;
        ch->writeValue(payload.c_str());
      }
    }
    client->disconnect();
  }
  delete client;

  int pts = 100;
  if (pendingRSSI >= -50) pts = 200;
  else if (pendingRSSI >= -70) pts = 150;

  if (encounterCount < MAX_ENCOUNTERS) {
    encounters[encounterCount].nickname  = pendingNickname;
    encounters[encounterCount].deviceID  = pendingDeviceID;
    encounters[encounterCount].rssi      = pendingRSSI;
    encounters[encounterCount].points    = pts;
    encounters[encounterCount].timestamp = millis();
    encounterCount++;
  }
  totalPoints += pts;
  handshakePending = false;
  checkBadges();
  prefs.putInt("points", totalPoints);
  prefs.putInt("encounters", encounterCount);

  // DS-style reward screen
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("StreetPass!");

  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_GOLD);
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(30, 40); gfx->print("Handshake!");
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(20, 68); gfx->print("Met: ");
  gfx->setTextColor(DS_BLUE); gfx->print(pendingNickname);

  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->fillRect(0, 111, 240, 171, DS_BG);

  // Points burst
  gfx->fillCircle(120, 170, 50, DS_GOLD);
  gfx->drawCircle(120, 170, 52, DS_YELLOW);
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4;
    gfx->drawLine(120+52*cos(angle), 170+52*sin(angle),
                  120+62*cos(angle), 170+62*sin(angle), DS_GOLD);
  }
  gfx->setTextSize(3); gfx->setTextColor(DS_DARKER);
  String ptsStr = "+" + String(pts);
  gfx->setCursor(120 - ptsStr.length()*9, 160); gfx->print(ptsStr);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(104, 194); gfx->print("POINTS!");

  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 220); gfx->print("Total: ");
  gfx->setTextColor(DS_GOLD); gfx->print(totalPoints);
  gfx->setTextColor(DS_DARKER); gfx->print("  Encounters: ");
  gfx->setTextColor(DS_LTPURP); gfx->print(encounterCount);

  // New badge?
  for (int i = 0; i < BADGE_COUNT; i++) {
    bool was = badges[i].unlocked;
    checkBadges();
    if (!was && badges[i].unlocked) {
      drawDSCard(8, 236, 224, 30, DS_GOLD);
      gfx->setTextColor(DS_GOLD); gfx->setCursor(14, 242);
      gfx->print("NEW BADGE: ");
      gfx->setTextColor(badges[i].color); gfx->print(badges[i].name);
      break;
    }
  }

  drawDSButton(60, 272, 120, 24, "A  AWESOME!", DS_LTPURP, DS_PURPLE, WHITE);
  delay(200);
}

void doStreetPassScan() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("StreetPass");

  gfx->fillRoundRect(8, 22, 224, 80, 4, DS_SCREEN);
  gfx->drawRoundRect(8, 22, 224, 80, 4, DS_LTPURP);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 50); gfx->print("Searching for Orestes users...");

  gfx->fillRect(0, 103, 240, 8, DS_DARKER);
  gfx->fillRect(0, 111, 240, 171, DS_BG);

  // Radar animation
  for (int r = 0; r < 2; r++) {
    for (int sweep = 0; sweep < 360; sweep += 20) {
      gfx->fillCircle(120, 190, 70, DS_BG);
      gfx->drawCircle(120, 190, 70, DS_LTPURP);
      gfx->drawCircle(120, 190, 46, DS_PURPLE);
      gfx->drawCircle(120, 190, 23, DS_DKGRAY);
      gfx->fillCircle(120, 190, 4, DS_LTPURP);
      float angle = sweep * PI / 180.0;
      gfx->drawLine(120, 190, 120+70*cos(angle), 190+70*sin(angle), DS_LTPURP);
      delay(20);
    }
  }

  BLEScan *spScan = BLEDevice::getScan();
  spScan->setAdvertisedDeviceCallbacks(new SPScanCallback());
  spScan->setActiveScan(true);
  spScan->start(5, false);
  spScan->clearResults();

  if (handshakePending) {
    currentScreen = SCREEN_HANDSHAKE;
    drawHandshakeScreen();
  } else {
    gfx->fillRect(0, 111, 240, 171, DS_BG);
    gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
    gfx->setCursor(30, 180); gfx->print("No Orestes users found.");
    gfx->setCursor(40, 194); gfx->print("Try again later!");
    delay(2000);
    drawStreetPassScreen();
  }
}

// =====================
// CARDS SCREEN
// =====================

void drawCollectibleCard(int idx, int x, int y, int w, int h) {
  if (idx >= encounterCount) {
    gfx->fillRoundRect(x, y, w, h, 6, 0xE71C);
    gfx->drawRoundRect(x, y, w, h, 6, DS_BORDER);
    gfx->setTextColor(DS_GRAY); gfx->setTextSize(1);
    gfx->setCursor(x+w/2-6, y+h/2-4); gfx->print("???");
    return;
  }
  Encounter &e = encounters[idx];
  uint16_t cardColor = e.points >= 200 ? DS_GOLD :
                       e.points >= 150 ? DS_LTBLUE : DS_LTPURP;
  gfx->fillRoundRect(x, y, w, h, 6, WHITE);
  gfx->drawRoundRect(x, y, w, h, 6, cardColor);
  gfx->drawFastHLine(x+4, y+1, w-8, cardColor);
  gfx->drawRoundRect(x+2, y+2, w-4, h-4, 4, 0xE71C);

  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(x+4, y+5); gfx->print("#"); gfx->print(idx+1);

  gfx->fillRoundRect(x+w-32, y+3, 30, 12, 3, cardColor);
  gfx->setTextColor(DS_DARKER); gfx->setCursor(x+w-28, y+6);
  gfx->print(e.points);

  gfx->fillCircle(x+w/2, y+h/2-8, 16, DS_BG);
  gfx->drawCircle(x+w/2, y+h/2-8, 16, cardColor);
  gfx->setTextSize(2); gfx->setTextColor(cardColor);
  char initial = e.nickname.length() > 0 ? e.nickname[0] : '?';
  gfx->setCursor(x+w/2-6, y+h/2-16); gfx->print(initial);

  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  String nick = e.nickname;
  if (nick.length() > 8) nick = nick.substring(0,7) + "~";
  gfx->setCursor(x+(w-nick.length()*6)/2, y+h/2+10); gfx->print(nick);

  gfx->setTextColor(signalColor(e.rssi));
  gfx->setCursor(x+4, y+h-12); gfx->print(e.rssi); gfx->print("dBm");
}

void drawCardsScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("My Cards");
  drawDSBackButton();

  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_GOLD);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(176, 25); gfx->print(encounterCount); gfx->print("/"); gfx->print(MAX_ENCOUNTERS);

  int cardW = 108, cardH = 84, startX = 8, startY = 38, gap = 8;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 2; col++) {
      int idx = cardScroll*6 + row*2 + col;
      drawCollectibleCard(idx, startX+col*(cardW+gap), startY+row*(cardH+gap), cardW, cardH);
    }
  }

  if (cardScroll > 0)
    drawDSButton(4, 300, 50, 16, "^ UP", DS_LTPURP, DS_PURPLE, WHITE);
  if ((cardScroll+1)*6 < MAX_ENCOUNTERS)
    drawDSButton(186, 300, 50, 16, "DN v", DS_LTPURP, DS_PURPLE, WHITE);

  drawDSDock(3);
}

// =====================
// BADGES SCREEN
// =====================

void drawBadgesScreen() {
  gfx->fillScreen(DS_BG);
  drawDSStatusBar("My Badges");
  drawDSBackButton();

  gfx->fillRoundRect(170, 20, 68, 16, 4, DS_YELLOW);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(174, 25);
  gfx->print(countUnlockedBadges()); gfx->print("/"); gfx->print(BADGE_COUNT);

  int y = 38;
  for (int i = 0; i < BADGE_COUNT; i++) {
    bool unlocked = badges[i].unlocked;
    gfx->fillRoundRect(4, y, 232, 28, 4, unlocked ? WHITE : 0xE71C);
    gfx->drawRoundRect(4, y, 232, 28, 4, unlocked ? badges[i].color : DS_BORDER);
    if (unlocked) gfx->drawFastHLine(8, y+1, 224, badges[i].color);

    // Badge circle
    gfx->fillCircle(22, y+14, 10, unlocked ? badges[i].color : DS_DKGRAY);
    gfx->setTextSize(1); gfx->setTextColor(unlocked ? WHITE : DS_GRAY);
    gfx->setCursor(18, y+10); gfx->print(unlocked ? "!" : "?");

    gfx->setTextColor(unlocked ? badges[i].color : DS_GRAY);
    gfx->setCursor(38, y+5); gfx->print(badges[i].name);
    gfx->setTextColor(unlocked ? DS_DKGRAY : DS_GRAY);
    gfx->setCursor(38, y+16);
    String desc = badges[i].desc;
    if (desc.length() > 26) desc = desc.substring(0,25) + "~";
    gfx->print(desc);

    if (unlocked) {
      gfx->fillRoundRect(196, y+8, 36, 12, 3, DS_GREEN);
      gfx->setTextColor(DS_DARKER); gfx->setCursor(200, y+12); gfx->print("GOT!");
    } else {
      // Show requirement
      gfx->setCursor(196, y+12); gfx->setTextColor(DS_GRAY);
      gfx->print("n="); gfx->print(badges[i].requirement);
    }

    y += 30;
  }

  drawDSDock(3);
}

// =====================
// MENU TAP HANDLER
// =====================

void handleMenuTap(int tx, int ty) {
  // 2x2 icon grid
  if (ty >= 118 && ty <= 188) {
    if (tx >= 14 && tx <= 110) { currentScreen = SCREEN_WIFI; doWifiScan(); }
    else if (tx >= 130 && tx <= 226) { currentScreen = SCREEN_BLE; doBLEScan(); }
  } else if (ty >= 196 && ty <= 266) {
    if (tx >= 14 && tx <= 110) { currentScreen = SCREEN_MAP; drawMapScreen(); }
    else if (tx >= 130 && tx <= 226) { currentScreen = SCREEN_STREETPASS; drawStreetPassScreen(); }
  }
}

// =====================
// SETUP & LOOP
// =====================

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(DS_BG);

  Wire.begin(TP_SDA, TP_SCL);
  Wire.setClock(400000);
  while (bsp_touch_init(&Wire, 0, gfx->width(), gfx->height()) == false) {
    delay(500);
  }

  // DS-style boot screen
  gfx->fillScreen(BLACK);
  delay(300);
  gfx->fillScreen(DS_BG);

  // Top screen
  gfx->fillRoundRect(8, 10, 224, 140, 6, DS_SCREEN);
  gfx->drawRoundRect(8, 10, 224, 140, 6, DS_BORDER);
  gfx->setTextSize(2); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(30, 30); gfx->print("Orestes");
  gfx->setTextSize(1); gfx->setTextColor(DS_DKGRAY);
  gfx->setCursor(30, 58); gfx->print("WiFi + BLE + StreetPass");
  gfx->setCursor(30, 72); gfx->print("Version 1.0");

  // DS hinge
  gfx->fillRect(0, 152, 240, 8, DS_DARKER);

  // Bottom screen
  gfx->fillRect(0, 160, 240, 160, DS_BG);
  gfx->setTextSize(1); gfx->setTextColor(DS_DARKER);
  gfx->setCursor(20, 170); gfx->print("Loading saved data...");

  prefs.begin("orestes", false);
  myNickname = prefs.getString("nickname", "");
  myDeviceID = prefs.getString("deviceid", "");
  totalPoints = prefs.getInt("points", 0);
  encounterCount = prefs.getInt("encounters", 0);

  gfx->setCursor(20, 184); gfx->print("Starting WiFi...");
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  if (myDeviceID.length() == 0) {
    myDeviceID = WiFi.macAddress();
    prefs.putString("deviceid", myDeviceID);
  }

  gfx->setCursor(20, 198); gfx->print("Starting BLE...");
  String bleName = myNickname.length() > 0 ? myNickname : "Orestes";
  BLEDevice::init(bleName.c_str());
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BLECallback());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  gfx->setCursor(20, 212); gfx->print("Starting StreetPass...");
  startOrestesServer();
  bleInitialized = true;

  gfx->setTextColor(DS_GREEN);
  gfx->setCursor(20, 226); gfx->print("Ready! Touch the screen.");
  delay(1200);

  if (myNickname.length() == 0) {
    currentScreen = SCREEN_NICKNAME;
    drawNicknameScreen();
  } else {
    currentScreen = SCREEN_MENU;
    drawMenu();
  }
}

void loop() {
  if (handshakePending &&
      currentScreen != SCREEN_HANDSHAKE &&
      currentScreen != SCREEN_NICKNAME) {
    currentScreen = SCREEN_HANDSHAKE;
    drawHandshakeScreen();
  }

  bsp_touch_read();

  if (bsp_touch_get_coordinates(&touchX, &touchY)) {
    if (!touchActive) {
      touchActive = true;
      lastTouchX = touchX;
      lastTouchY = touchY;
    } else {
      int swipeDY = (int)touchY - lastTouchY;
      if (abs(swipeDY) > 25) {
        if (currentScreen == SCREEN_WIFI && !wifiDetail) {
          if (swipeDY < 0 && wifiScroll < wifiCount-9) { wifiScroll++; drawWifiList(); }
          if (swipeDY > 0 && wifiScroll > 0) { wifiScroll--; drawWifiList(); }
          lastTouchY = touchY;
        } else if (currentScreen == SCREEN_BLE && !bleDetail) {
          if (swipeDY < 0 && bleScroll < bleCount-9) { bleScroll++; drawBLEList(); }
          if (swipeDY > 0 && bleScroll > 0) { bleScroll--; drawBLEList(); }
          lastTouchY = touchY;
        } else if (currentScreen == SCREEN_WHOIMet) {
          if (swipeDY < 0 && whoMetScroll + 4 < encounterCount) { whoMetScroll++; drawWhoIMetScreen(); }
          if (swipeDY > 0 && whoMetScroll > 0) { whoMetScroll--; drawWhoIMetScreen(); }
          lastTouchY = touchY;
        }
      }
    }
  } else {
    if (touchActive) {
      int tx = lastTouchX;
      int ty = lastTouchY;
      int dx = abs((int)touchX - tx);
      int dy = abs((int)touchY - ty);
      bool isTap = (dx < 25 && dy < 25);
      bool backTapped = (tx < 65 && ty >= 20 && ty <= 38);

      if (isTap) {
        if (currentScreen == SCREEN_NICKNAME) {
          handleNicknameKey(tx, ty);

        } else if (currentScreen == SCREEN_MENU) {
          handleMenuTap(tx, ty);

        } else if (currentScreen == SCREEN_WIFI) {
          if (backTapped) { currentScreen = SCREEN_MENU; drawMenu(); }
          else if (wifiDetail) { wifiDetail = false; drawWifiList(); }
          else if (ty >= 282 && tx <= 78) { doWifiScan(); }
          else {
            int row = (ty - 40) / 26;
            int idx = row + wifiScroll;
            if (idx >= 0 && idx < wifiCount) {
              wifiSelected = idx; wifiDetail = true; drawWifiDetail(idx);
            }
          }

        } else if (currentScreen == SCREEN_BLE) {
          if (backTapped) { currentScreen = SCREEN_MENU; drawMenu(); }
          else if (bleDetail) { bleDetail = false; drawBLEList(); }
          else if (ty >= 282 && tx <= 78) { doBLEScan(); }
          else {
            int row = (ty - 40) / 26;
            int idx = row + bleScroll;
            if (idx >= 0 && idx < bleCount) {
              bleSelected = idx; bleDetail = true; drawBLEDetail(idx);
            }
          }

        } else if (currentScreen == SCREEN_MAP) {
          if (backTapped) { currentScreen = SCREEN_MENU; drawMenu(); }
          else { handleMapTap(tx, ty); }

        } else if (currentScreen == SCREEN_STREETPASS) {
          if (backTapped) { currentScreen = SCREEN_MENU; drawMenu(); }
          // SCAN button
          else if (ty >= 106 && ty <= 130 && tx >= 4 && tx <= 74) {
            doStreetPassScan();
          }
          // MET button
          else if (ty >= 106 && ty <= 130 && tx >= 80 && tx <= 150) {
            currentScreen = SCREEN_WHOIMet; whoMetScroll = 0; drawWhoIMetScreen();
          }
          // BADGES button
          else if (ty >= 106 && ty <= 130 && tx >= 156 && tx <= 226) {
            currentScreen = SCREEN_BADGES; drawBadgesScreen();
          }
          // CARDS button
          else if (ty >= 136 && ty <= 160 && tx >= 4 && tx <= 74) {
            currentScreen = SCREEN_CARDS; cardScroll = 0; drawCardsScreen();
          }

        } else if (currentScreen == SCREEN_HANDSHAKE) {
          if (tx >= 14 && tx <= 114 && ty >= 224 && ty <= 258) {
            confirmHandshake();
            delay(3000);
            currentScreen = SCREEN_STREETPASS;
            drawStreetPassScreen();
          } else if (tx >= 126 && tx <= 226 && ty >= 224 && ty <= 258) {
            handshakePending = false;
            currentScreen = SCREEN_STREETPASS;
            drawStreetPassScreen();
          }

        } else if (currentScreen == SCREEN_CARDS) {
          if (backTapped) { currentScreen = SCREEN_STREETPASS; drawStreetPassScreen(); }
          else if (ty >= 300 && tx <= 54 && cardScroll > 0) { cardScroll--; drawCardsScreen(); }
          else if (ty >= 300 && tx >= 186) { cardScroll++; drawCardsScreen(); }

        } else if (currentScreen == SCREEN_BADGES) {
          if (backTapped) { currentScreen = SCREEN_STREETPASS; drawStreetPassScreen(); }

        } else if (currentScreen == SCREEN_WHOIMet) {
          if (backTapped) { currentScreen = SCREEN_STREETPASS; drawStreetPassScreen(); }
          else if (ty >= 282 && tx <= 54 && whoMetScroll > 0) {
            whoMetScroll--; drawWhoIMetScreen();
          } else if (ty >= 282 && tx >= 186 && whoMetScroll+4 < encounterCount) {
            whoMetScroll++; drawWhoIMetScreen();
          }
        }
      }
      touchActive = false;
    }
  }

  delay(30);
}