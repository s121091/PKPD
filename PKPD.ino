/*
 * Memory Bloom - V18.7 (Layout Adjustment)
 * ------------------------------------------------
 * 修改內容：
 * 1. 【UI 佈局】IP 地址和 Cloud 狀態移動到屏幕左上角。
 * 2. 【UI 佈局】Hold '9' Exit 移動到屏幕左下角。
 * 3. 【UI 優化】標題向右微調，避免遮擋狀態信息。
 * * 包含 V18.6 的所有功能。
 */

#include <Arduino_GFX_Library.h> 
#include <Keypad.h>
#include <XPT2046_Touchscreen.h> 
#include <vector>
#include <SPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h> 
#include <WebServer.h> 
#include <DNSServer.h> 

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==========================================
// 1. 配置 (動態讀取)
// ==========================================
String wifi_ssid = "";
String wifi_pass = "";

// ⚠️ Firebase 仍需在此填寫
#define FIREBASE_HOST "pkpd-database-default-rtdb.asia-southeast1.firebasedatabase.app" 
#define FIREBASE_AUTH "your_database_secret_key" 

// ==========================================
// 2. 結構體定義
// ==========================================
struct SystemConfig {
    bool wifiEnabled; 
    int difficulty;   
    bool colorBlind;  
    int fontSize;     
    int gameMode;     
};
SystemConfig sysConfig = {true, 2, false, 0, 0}; 

struct GameSession {
    int modeType; 
    int score;
    unsigned long durationSeconds;
    time_t timestamp;      
    String sessionID;      
    bool uploaded;
};

// ==========================================
// 3. 全局變量
// ==========================================
Preferences preferences;
Preferences wifiPrefs; 
std::vector<GameSession> gameHistory;    
std::vector<GameSession> pendingUploads; 
unsigned long gameStartTime = 0; 
SemaphoreHandle_t xGuiMutex; 

// 網絡對象
WebServer server(80);
DNSServer dnsServer;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;
unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL = 10000; 

enum GameState { 
    STATE_WIFI_CONNECTING, 
    STATE_WIFI_CONFIG_AP, 
    STATE_MENU_MAIN, 
    STATE_MENU_GAME_SELECT, 
    STATE_MENU_DIFFICULTY, 
    STATE_MENU_SETTINGS, 
    STATE_MENU_DATA,
    STATE_GUIDE_READY, STATE_GUIDE_OBSERVE, STATE_GUIDE_RECALL,
    STATE_MEM_SHOWING, STATE_MEM_INPUT, 
    STATE_CNT_PREPARE, STATE_CNT_WAITING, STATE_CNT_INPUT, 
    STATE_GAME_OVER
};
GameState currentState = STATE_WIFI_CONNECTING; 

std::vector<int> sequence; 
int inputIndex = 0;        
int targetCount = 0;       
int userTapCount = 0;      
unsigned long lastTapTime = 0; 
unsigned long showStartTime = 0; 
int currentScore = 0;
int currentSpeed = 1000; 
int settingsPage = 0; 

int startX = 160;   
int startY = 30;    
int btnWidth = 90;  
int btnHeight = 80; 
int gap = 15;       

// ==========================================
// 4. 硬件驅動
// ==========================================
Arduino_DataBus *bus = new Arduino_ESP32SPI(17, 5, 18, 23, -1);
Arduino_GFX *tft = new Arduino_ILI9488_18bit(bus, 16, 1, false);
#define TOUCH_CS_PIN  4  
#define TOUCH_IRQ_PIN 26 
XPT2046_Touchscreen ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
#define PIN_BUZZER 25
const int TONES[] = {262, 294, 330, 349, 392, 440, 494, 523, 587}; 
const byte ROWS = 3; 
const byte COLS = 3; 
char keys[ROWS][COLS] = { {'1','2','3'}, {'4','5','6'}, {'7','8','9'} }; 
byte rowPins[ROWS] = {32, 33, 27}; 
byte colPins[COLS] = {14, 12, 13}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ==========================================
// 5. 配色表
// ==========================================
#define BLACK         0x0000
#define WHITE         0xFFFF 

#define COLOR_BTN_NORMAL  0xDEFB  
#define COLOR_BTN_ACTIVE  0xFD60  
#define COLOR_TEXT_LIGHT  0x8410 

#define C_BG_LIGHT       0xFFDF 
#define C_BG_SHADOW      0x9CF3 
#define C_PRIMARY        0x43B2 
#define C_ACCENT         0xFD60 
#define C_SUCCESS        0x64AC
#define C_ERROR          0xF800 
#define C_SECONDARY      0xF9A8 

#define C_CB_BG          0x0000 
#define C_CB_PRIMARY     0x001F 
#define C_CB_ACCENT      0xFFE0 
#define C_CB_SUCCESS     0x0000 
#define C_CB_ERROR       0x0000 
#define CB_ACTIVE        0xFFFF 

uint16_t getThemeColor(int type) {
    if (!sysConfig.colorBlind) {
        switch(type) {
            case 0: return C_PRIMARY;
            case 1: return C_ACCENT;
            case 2: return C_SUCCESS;
            case 3: return C_ERROR;
            case 4: return C_BG_LIGHT;
            default: return 0;
        }
    } else {
        switch(type) {
            case 0: return C_CB_PRIMARY;
            case 1: return C_CB_ACCENT;
            case 2: return C_CB_SUCCESS;
            case 3: return C_CB_ERROR;
            case 4: return C_CB_BG;
            default: return 0;
        }
    }
}

uint16_t getBgColor() { return sysConfig.colorBlind ? C_CB_BG : C_BG_LIGHT; }
uint16_t getTextColor() { return sysConfig.colorBlind ? WHITE : BLACK; }

// ==========================================
// 6. 前向聲明
// ==========================================
void initWiFi();
void stopWiFi(); 
void startAPMode(); 
void handleRoot(); 
void handleSave(); 
void initFirebase();
void syncData();
bool uploadRecord(int index);
void updateStatistics(int score);
void saveHistory();
void loadHistory();
void clearHistory();
void drawDataHistoryScreen();
void redrawMainMenu();
void redrawSettingsMenu(); 
void drawDifficultyMenu(); 
void update_status(String text, uint16_t color);
void drawGameElement(int id, bool active);
void playTone(int freq, int duration);
void screen_msg(String l1, String l2, uint16_t color);
bool saveScoreToFirebase(int score, unsigned long duration);
char updateInput(); 
void uploadPendingRecords(); 
uint16_t getThemeColor(int type); 
uint16_t getBgColor();
uint16_t getTextColor();
void drawTextMenuItem(int slot, String label, String value, bool selected);
bool delayWithCheck(int ms); 

// ==========================================
// 7. 數據存儲核心
// ==========================================
void playTone(int freq, int duration) {
    tone(PIN_BUZZER, freq, duration);
}

void loadWiFiCreds() {
    wifiPrefs.begin("mb_net", true); 
    wifi_ssid = wifiPrefs.getString("ssid", "");
    wifi_pass = wifiPrefs.getString("pass", "");
    wifiPrefs.end();
}

void saveWiFiCreds(String s, String p) {
    s.trim(); 
    p.trim();
    wifiPrefs.begin("mb_net", false); 
    wifiPrefs.putString("ssid", s);
    wifiPrefs.putString("pass", p);
    wifiPrefs.end();
    wifi_ssid = s;
    wifi_pass = p;
}

void saveHistory() {
    if (gameHistory.size() > 10) {
        std::vector<GameSession> recentHistory;
        for (int i = gameHistory.size() - 10; i < gameHistory.size(); i++) {
            recentHistory.push_back(gameHistory[i]);
        }
        gameHistory = recentHistory;
    }
    preferences.begin("mb_v2", false);
    int count = gameHistory.size();
    preferences.putInt("count", count);
    
    preferences.putBool("wifi", sysConfig.wifiEnabled);
    preferences.putBool("cb", sysConfig.colorBlind);
    preferences.putInt("font", sysConfig.fontSize);
    
    for (int i = 0; i < count; i++) {
        String prefix = "g" + String(i);
        uint8_t buffer[17];
        memcpy(buffer, &gameHistory[i].modeType, 4);
        memcpy(buffer + 4, &gameHistory[i].score, 4);
        memcpy(buffer + 8, &gameHistory[i].durationSeconds, 4);
        unsigned long ts = (unsigned long)gameHistory[i].timestamp;
        memcpy(buffer + 12, &ts, 4);
        buffer[16] = gameHistory[i].uploaded ? 1 : 0;
        preferences.putBytes(prefix.c_str(), buffer, 17); 
    }
    preferences.end();
}

void loadHistory() {
    preferences.begin("mb_v2", true);
    sysConfig.wifiEnabled = preferences.getBool("wifi", true);
    sysConfig.colorBlind = preferences.getBool("cb", false);
    sysConfig.fontSize = preferences.getInt("font", 0);
    
    int count = preferences.getInt("count", 0);
    gameHistory.clear();
    for (int i = 0; i < count; i++) {
        String prefix = "g" + String(i);
        uint8_t buffer[17];
        if (preferences.getBytes(prefix.c_str(), buffer, 17) > 0) {
            GameSession s;
            memcpy(&s.modeType, buffer, 4);
            memcpy(&s.score, buffer + 4, 4);
            memcpy(&s.durationSeconds, buffer + 8, 4);
            unsigned long ts;
            memcpy(&ts, buffer + 12, 4);
            s.timestamp = (time_t)ts;
            s.uploaded = (buffer[16] == 1);
            String pfx = (s.modeType == 0) ? "MEM" : "CNT";
            s.sessionID = pfx + "_" + String(ts);
            gameHistory.push_back(s);
        }
    }
    preferences.end();
}

void clearHistory() {
    preferences.begin("mb_v2", false);
    preferences.remove("count"); 
    preferences.end();
    gameHistory.clear();
    playTone(500, 200);
}

bool saveScoreToFirebase(int score, unsigned long duration) {
    GameSession record;
    record.modeType = sysConfig.gameMode;
    record.score = score;
    record.durationSeconds = duration;
    record.timestamp = time(nullptr); 
    
    String suffix;
    if (record.timestamp > 1000000000) suffix = String((unsigned long)record.timestamp);
    else suffix = String(millis());
    String prefix = (sysConfig.gameMode == 0) ? "MEM" : "CNT";
    record.sessionID = prefix + "_" + suffix; 
    record.uploaded = false; 
    
    gameHistory.push_back(record);
    saveHistory(); 
    
    bool success = false;
    if (sysConfig.wifiEnabled) {
         if (!firebaseReady) initWiFi(); 
         syncData();
         if (!gameHistory.empty() && gameHistory.back().uploaded) {
             success = true;
             updateStatistics(score);
         }
    }
    return success;
}

// ==========================================
// 8. 網絡與配網 WebServer
// ==========================================
const char* html_page = 
"<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<style>body{font-family:sans-serif;text-align:center;padding:20px;background:#f0f8ff;}"
"input{padding:10px;margin:5px;width:100%;box-sizing:border-box;font-size:16px;}"
"input[type=submit]{background:#43B2F9;color:white;border:none;cursor:pointer;}"
"</style></head><body><h2>WiFi Setup</h2>"
"<form action='/save' method='POST'>"
"<input type='text' name='s' placeholder='SSID (WiFi Name)'><br>"
"<input type='password' name='p' placeholder='Password'><br>"
"<input type='submit' value='Save & Connect'></form>"
"</body></html>";

void handleRoot() {
    server.send(200, "text/html", html_page);
}

void handleSave() {
    String s = server.arg("s");
    String p = server.arg("p");
    if (s.length() > 0) {
        saveWiFiCreds(s, p);
        
        sysConfig.wifiEnabled = true;
        saveHistory(); 
        
        server.send(200, "text/html", "<h1>Saved!</h1><p>WiFi Enabled. Restarting...</p>");
        delay(2000);
        ESP.restart();
    } else {
        server.send(200, "text/html", "<h1>Error</h1><p>SSID empty</p><a href='/'>Back</a>");
    }
}

void startAPMode() {
    stopWiFi();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("MemoryBloom_Setup"); 
    
    dnsServer.start(53, "*", WiFi.softAPIP()); 
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.on("/generate_204", handleRoot); 
    server.on("/fwlink", handleRoot);
    server.begin();
    
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        tft->setTextColor(getThemeColor(1)); tft->setTextSize(3);
        tft->setCursor(20, 40); tft->print("WiFi Setup");
        
        tft->setTextColor(getTextColor()); tft->setTextSize(2);
        tft->setCursor(20, 100); tft->print("1. Phone Connect to:");
        tft->setTextColor(C_PRIMARY);
        tft->setCursor(20, 130); tft->print("MemoryBloom_Setup");
        
        tft->setTextColor(getTextColor());
        tft->setCursor(20, 180); tft->print("2. Browser visit:");
        tft->setTextColor(C_PRIMARY);
        tft->setCursor(20, 210); tft->print("192.168.4.1");
        
        tft->setTextColor(C_ERROR);
        tft->setCursor(20, 280); tft->print("9. Cancel/Restart");
        xSemaphoreGive(xGuiMutex);
    }
    
    currentState = STATE_WIFI_CONFIG_AP;
}

void initWiFi() {
    if (!sysConfig.wifiEnabled) return; 
    
    loadWiFiCreds();
    if (wifi_ssid == "") {
        return;
    }

    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        tft->setTextColor(getTextColor()); tft->setTextSize(3);
        tft->setCursor(20, 100); tft->println("Connecting");
        tft->setTextColor(C_ACCENT); tft->setTextSize(2);
        tft->setCursor(20, 140); tft->println(wifi_ssid); 
        xSemaphoreGive(xGuiMutex);
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 
    delay(100);

    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 25) { 
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
             tft->fillScreen(getBgColor());
             tft->setTextColor(C_ERROR); tft->setTextSize(3);
             tft->setCursor(20, 100); tft->println("Connect Fail");
             tft->setTextSize(2);
             tft->setTextColor(getTextColor());
             tft->setCursor(20, 150); tft->print("Err: "); tft->println(WiFi.status());
             xSemaphoreGive(xGuiMutex);
             delay(3000);
        }
    } else {
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        initFirebase();
    }
}

void stopWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    firebaseReady = false;
}

void initFirebase() {
    if (!sysConfig.wifiEnabled) return;

    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    config.timeout.wifiReconnect = 10 * 1000;
    
    Firebase.reconnectNetwork(true);
    fbdo.setBSSLBufferSize(4096, 1024);
    fbdo.setResponseSize(2048);
    
    Firebase.begin(&config, &auth);
    Firebase.setDoubleDigits(5);
    
    long start = millis();
    while (!Firebase.ready() && millis() - start < 5000) {
        delay(100);
    }
    
    if (Firebase.ready()) {
        firebaseReady = true;
        String devicePath = "/devices/" + WiFi.macAddress();
        Firebase.RTDB.setString(&fbdo, devicePath + "/lastSeen", String(millis()/1000));
        syncData();
    }
}

bool uploadRecord(int index) {
    if (!firebaseReady) return false;
    if (index < 0 || index >= gameHistory.size()) return false;
    GameSession &record = gameHistory[index];
    if (record.uploaded) return true; 
    
    String deviceID = WiFi.macAddress();
    String recordPath = "/devices/" + deviceID + "/sessions/" + record.sessionID;
    
    FirebaseJson json;
    json.set("mode", record.modeType == 0 ? "memory" : "counting");
    json.set("score", record.score);
    json.set("duration", record.durationSeconds);
    json.set("timestamp", (double)record.timestamp);
    
    if (Firebase.RTDB.setJSON(&fbdo, recordPath.c_str(), &json)) {
        record.uploaded = true; 
        return true;
    }
    return false;
}

void syncData() {
    if (!firebaseReady || !sysConfig.wifiEnabled) return;
    bool dataChanged = false;
    for (int i = 0; i < gameHistory.size(); i++) {
        if (!gameHistory[i].uploaded) {
            if (uploadRecord(i)) dataChanged = true;
            delay(50); 
        }
    }
    if (dataChanged) saveHistory();
}

void uploadPendingRecords() {
    syncData();
}

void updateStatistics(int score) {
    if (!firebaseReady) return;
    String deviceID = WiFi.macAddress();
    String statsPath = "/statistics/" + deviceID;
    
    int totalGames = 0, totalScore = 0, highScore = 0;
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/totalGames")) totalGames = fbdo.to<int>();
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/totalScore")) totalScore = fbdo.to<int>();
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/highScore")) highScore = fbdo.to<int>();
    
    totalGames++; totalScore += score;
    if (score > highScore) highScore = score;
    
    FirebaseJson statsJson;
    statsJson.set("totalGames", totalGames);
    statsJson.set("totalScore", totalScore);
    statsJson.set("highScore", highScore);
    Firebase.RTDB.setJSON(&fbdo, statsPath.c_str(), &statsJson);
}

// ==========================================
// 9. UI 繪製函數 (修復佈局重疊)
// ==========================================
void drawRoundedButton(int x, int y, int w, int h, uint16_t color, bool pressed = false) {
    uint16_t borderColor = sysConfig.colorBlind ? WHITE : C_BG_SHADOW;
    if (pressed) {
        tft->fillRoundRect(x + 2, y + 2, w, h, 12, color);
    } else {
        tft->fillRoundRect(x + 4, y + 4, w, h, 12, borderColor);
        tft->fillRoundRect(x, y, w, h, 12, color);
    }
    if (sysConfig.colorBlind) tft->drawRoundRect(x, y, w, h, 12, WHITE);
}

void drawMenuBtn(int slot, String text, uint16_t color, bool selected) {
    int y = 70 + (slot * 60); 
    int x = 60; int w = 360; int h = 50; 
    
    uint16_t btnColor = color;
    if (sysConfig.colorBlind) {
        if (slot == 0) btnColor = C_CB_PRIMARY;
        else if (slot == 1) btnColor = C_CB_ACCENT;
        else if (slot == 2) btnColor = C_CB_SUCCESS;
        else btnColor = C_CB_BG;
    }
    
    if (selected) btnColor = sysConfig.colorBlind ? CB_ACTIVE : C_ACCENT;

    drawRoundedButton(x, y, w, h, btnColor, false);
    
    uint16_t txtColor = WHITE;
    if (sysConfig.colorBlind && btnColor == C_CB_ACCENT) txtColor = BLACK;
    if (sysConfig.colorBlind && selected) txtColor = BLACK;
    
    tft->setTextColor(txtColor); 
    tft->setTextSize(3 + sysConfig.fontSize); 
    int txtX = x + (w - (text.length() * (18 + sysConfig.fontSize*6))) / 2;
    tft->setCursor(txtX, y + 14);
    tft->print(text);
}

void drawTextMenuItem(int slot, String label, String value, bool selected) {
    int y = 80 + (slot * 65);
    uint16_t color = selected ? C_ACCENT : COLOR_BTN_NORMAL;
    uint16_t txtColor = BLACK;
    
    if (sysConfig.colorBlind) {
        color = selected ? CB_ACTIVE : C_CB_BG;
        txtColor = selected ? BLACK : WHITE;
        tft->drawRoundRect(40, y, 400, 55, 12, WHITE);
        if (selected) tft->fillRoundRect(40, y, 400, 55, 12, WHITE);
    } else {
        drawRoundedButton(40, y, 400, 55, color, false);
        txtColor = selected ? WHITE : BLACK;
    }
    
    tft->setTextColor(txtColor); tft->setTextSize(3 + sysConfig.fontSize);
    tft->setCursor(60, y + 16); tft->print(label);
    if (value != "") { tft->setCursor(260, y + 16); tft->print(value); }
}

void drawGameElement(int id, bool active) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        int row = id / 3; int col = id % 3;
        int cx = startX + col * (btnWidth + gap) + (btnWidth / 2);
        int cy = startY + row * (btnHeight + gap) + (btnHeight / 2);
        
        tft->fillRect(cx - 45, cy - 40, 90, 80, getBgColor());

        if (sysConfig.gameMode == 0) { // Memory
            uint16_t color;
            if (sysConfig.colorBlind) {
                 color = active ? C_CB_ACCENT : C_CB_PRIMARY;
            } else {
                 color = active ? COLOR_BTN_ACTIVE : COLOR_BTN_NORMAL;
            }
            
            tft->fillRoundRect(cx - 40, cy - 35, 80, 70, 12, color);
            
            uint16_t txtColor = (sysConfig.colorBlind && active) ? BLACK : WHITE;
            if (!sysConfig.colorBlind && !active) txtColor = BLACK;
            
            tft->setTextColor(txtColor); tft->setTextSize(3 + sysConfig.fontSize);
            tft->setCursor(cx - 8, cy - 10); tft->print(id + 1);
        } else { // Counting
            if (sysConfig.colorBlind) {
                if (active) tft->fillRect(cx-20, cy-20, 40, 40, C_CB_ACCENT);
                else tft->drawRect(cx-20, cy-20, 40, 40, WHITE);
            } else {
                if (active) {
                    tft->fillCircle(cx, cy, 25, C_SECONDARY);
                    tft->fillCircle(cx, cy, 15, WHITE);
                } else {
                    tft->drawCircle(cx, cy, 25, C_PRIMARY);
                    tft->drawCircle(cx, cy, 24, C_PRIMARY);
                }
            }
        }
        xSemaphoreGive(xGuiMutex);
    }
}

void update_status(String text, uint16_t color) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        // 修復顯示重疊：清除高度擴大到 60px
        tft->fillRect(0, 0, 480, 60, getBgColor()); 
        
        tft->drawFastVLine(startX - 10, 20, 280, C_BG_SHADOW);
        tft->setTextColor(color); tft->setTextSize(3 + sysConfig.fontSize); tft->setCursor(20, 100); 
        int y = 100;
        for (int i = 0; i < text.length(); i += 8) {
            tft->setCursor(20, y);
            tft->println(text.substring(i, min((int)text.length(), i + 8)));
            y += 40;
        }
        if (currentState != STATE_MENU_MAIN && currentState != STATE_GAME_OVER && currentState != STATE_WIFI_CONNECTING) {
            tft->setTextSize(2); tft->setTextColor(getTextColor()); tft->setCursor(350, 10);
            tft->print("Score: "); tft->setTextColor(sysConfig.colorBlind ? C_CB_ACCENT : C_ACCENT); tft->print(currentScore);
        }
        xSemaphoreGive(xGuiMutex);
    }
}

void screen_msg(String l1, String l2, uint16_t color) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        uint16_t frameColor = sysConfig.colorBlind ? WHITE : C_BG_SHADOW;
        tft->drawRoundRect(20, 20, 440, 280, 20, frameColor);
        tft->setTextColor(color); tft->setTextSize(4 + sysConfig.fontSize);
        tft->setCursor(60, 100); tft->println(l1);
        tft->setTextColor(getTextColor()); tft->setTextSize(3);
        tft->setCursor(60, 160); tft->println(l2);
        xSemaphoreGive(xGuiMutex);
    }
}

void drawDataHistoryScreen() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        tft->setTextColor(sysConfig.colorBlind ? C_CB_ACCENT : C_PRIMARY); tft->setTextSize(3);
        tft->setCursor(20, 20); tft->print("GAME HISTORY");
        
        tft->setTextColor(getTextColor()); tft->setTextSize(2);
        tft->setCursor(30, 60); tft->print("Mode");
        tft->setCursor(150, 60); tft->print("Score");
        tft->setCursor(250, 60); tft->print("Time");
        tft->setCursor(380, 60); tft->print("Up"); 
        tft->drawFastHLine(20, 80, 440, C_BG_SHADOW);
        
        int y = 95;
        if (gameHistory.empty()) {
            tft->setCursor(150, 150); tft->print("No Records");
        } else {
            int count = 0;
            for (int i = gameHistory.size() - 1; i >= 0; i--) {
                if (count >= 6) break;
                GameSession s = gameHistory[i];
                tft->setTextColor(sysConfig.colorBlind ? WHITE : COLOR_TEXT_LIGHT);
                tft->setCursor(30, y); tft->print(s.modeType == 0 ? "Mem" : "Cnt");
                tft->setCursor(160, y); tft->print(s.score);
                tft->setCursor(260, y); tft->print(s.durationSeconds); tft->print("s");
                
                tft->setCursor(380, y);
                if (s.uploaded) {
                    tft->setTextColor(C_SUCCESS); tft->print("[ok]");
                } else {
                    tft->setTextColor(C_ERROR); tft->print("[!]");
                }
                y += 35; count++;
            }
        }
        tft->setTextColor(C_ERROR);
        tft->setCursor(20, 300); tft->print("5.Sync  9.Back"); 
        xSemaphoreGive(xGuiMutex);
    }
}

// 修改後的主菜單佈局
void redrawMainMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        
        // 1. 狀態顯示 (左上角)
        tft->setTextSize(2);
        if (sysConfig.wifiEnabled) {
            if (firebaseReady) {
                tft->setTextColor(C_SUCCESS); tft->setCursor(5, 5); tft->print("Cloud Online");
            } else if (WiFi.status() == WL_CONNECTED) {
                tft->setTextColor(C_ACCENT); tft->setCursor(5, 5); tft->print("WiFi Only");
            } else {
                tft->setTextColor(C_ERROR); tft->setCursor(5, 5); tft->print("No Connection");
            }
        } else {
            tft->setTextColor(BLACK); tft->setCursor(5, 5); tft->print("Offline Mode");
        }

        // 2. IP 顯示 (左上角, 狀態下方)
        tft->setTextColor(getTextColor()); tft->setTextSize(1);
        tft->setCursor(5, 25);
        if (WiFi.status() == WL_CONNECTED && sysConfig.wifiEnabled) {
             tft->print("IP: "); tft->print(WiFi.localIP());
        } else {
             tft->print("No IP");
        }

        // 3. 標題 (右移，避免遮擋左上角信息)
        tft->setTextColor(sysConfig.colorBlind ? C_CB_ACCENT : C_PRIMARY); tft->setTextSize(4);
        tft->setCursor(160, 15); tft->print("Memory Bloom");
        
        xSemaphoreGive(xGuiMutex);
    }
    
    // 繪製按鈕 (保持原位)
    drawMenuBtn(0, "1. Start Game", C_SECONDARY, false); 
    drawMenuBtn(1, "2. Settings", C_PRIMARY, false);     
    drawMenuBtn(2, "3. Data View", C_ACCENT, false);     
    drawMenuBtn(3, "4. Sync Data", firebaseReady ? C_SUCCESS : C_ERROR, false);
    
    // 4. 退出提示 (左下角)
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(getTextColor()); tft->setTextSize(2);
        tft->setCursor(5, 305); tft->print("Hold '9' Exit");
        xSemaphoreGive(xGuiMutex);
    }
}

void drawDifficultyMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        tft->setTextColor(getThemeColor(0)); tft->setTextSize(3);
        tft->setCursor(120, 30); tft->print("DIFFICULTY");
        xSemaphoreGive(xGuiMutex);
    }
    drawTextMenuItem(0, "1. Easy", "", false);
    drawTextMenuItem(1, "2. Hard", "", false);
    drawTextMenuItem(2, "3. Auto", "(Best)", false);
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(C_ERROR); tft->setTextSize(2);
        tft->setCursor(20, 300); tft->print("9. Back");
        xSemaphoreGive(xGuiMutex);
    }
}

void redrawSettingsMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(getBgColor());
        tft->setTextColor(getThemeColor(0)); tft->setTextSize(3);
        tft->setCursor(150, 30); tft->print("SETTINGS");
        tft->setTextSize(2);
        tft->setCursor(350, 35); tft->print("P"); tft->print(settingsPage + 1);
        xSemaphoreGive(xGuiMutex);
    }
    
    if (settingsPage == 0) {
        drawTextMenuItem(0, "1. WiFi Setup", "", false);
        drawTextMenuItem(1, "2. Color", sysConfig.colorBlind ? "Eye+" : "Norm", false);
        drawTextMenuItem(2, "3. Font", sysConfig.fontSize==0 ? "Big" : "Max", false);
    } else {
        drawTextMenuItem(0, "1. Clear Data", "", false); 
        drawTextMenuItem(1, "2. WiFi", sysConfig.wifiEnabled ? "ON" : "OFF", false);
        drawTextMenuItem(2, "3. (Empty)", "", false);
    }
    
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(BLACK); tft->setTextSize(2);
        tft->setCursor(20, 300); tft->print("9.Back  4.Prev  6.Next");
        xSemaphoreGive(xGuiMutex);
    }
}

// ==========================================
// 10. 系統任務 (核心邏輯)
// ==========================================

char updateInput() {
    char key = keypad.getKey(); 
    
    static unsigned long pressStart = 0;
    static bool isPressing = false;
    
    bool is9Held = false;
    if (keypad.getKeys()) {
        for (int i=0; i<LIST_MAX; i++) {
            if (keypad.key[i].kchar == '9' && 
               (keypad.key[i].kstate == PRESSED || keypad.key[i].kstate == HOLD)) {
                is9Held = true;
                break;
            }
        }
    }

    if (is9Held) {
        if (!isPressing) {
            isPressing = true;
            pressStart = millis();
        } else {
            if (millis() - pressStart > 800) {
                playTone(1000, 500); 
                currentState = STATE_MENU_MAIN;
                redrawMainMenu();
                isPressing = false;
                return 0; 
            }
        }
    } else {
        isPressing = false;
    }
    
    return key;
}

bool delayWithCheck(int ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        updateInput(); 
        if (currentState == STATE_MENU_MAIN) return true; 
        vTaskDelay(10);
    }
    return false; 
}

void task_system(void *pv) {
    loadHistory(); 
    initWiFi();
    currentState = STATE_MENU_MAIN; 
    redrawMainMenu();

    while(1) {
        if (currentState == STATE_WIFI_CONFIG_AP) {
            dnsServer.processNextRequest();
            server.handleClient();
             char key = updateInput();
             if (key == '9') {
                 WiFi.softAPdisconnect(true);
                 ESP.restart(); 
             }
            vTaskDelay(10);
            continue;
        }

        char key = updateInput();
        bool keyPressed = (key != NO_KEY);

        if (sysConfig.wifiEnabled && firebaseReady && !pendingUploads.empty() && 
            millis() - lastUploadTime > UPLOAD_INTERVAL) {
            uploadPendingRecords();
            lastUploadTime = millis();
        }

        if (keyPressed && key == '9') {
            if (currentState == STATE_MENU_GAME_SELECT || currentState == STATE_MENU_SETTINGS || currentState == STATE_MENU_DATA) {
                currentState = STATE_MENU_MAIN; redrawMainMenu(); continue;
            } else if (currentState == STATE_MENU_DIFFICULTY) {
                currentState = STATE_MENU_GAME_SELECT; screen_msg("Select Game", "1.Memory 2.Count", C_SECONDARY); continue;
            }
        }

        switch (currentState) {
            case STATE_MENU_MAIN:
                if (keyPressed) {
                    if (key == '1') { currentState = STATE_MENU_GAME_SELECT; screen_msg("Select Game", "1.Memory 2.Count", C_SECONDARY); }
                    else if (key == '2') { settingsPage = 0; currentState = STATE_MENU_SETTINGS; redrawSettingsMenu(); }
                    else if (key == '3') { currentState = STATE_MENU_DATA; drawDataHistoryScreen(); }
                    else if (key == '4') { 
                        if (sysConfig.wifiEnabled) {
                             if (!firebaseReady) { initWiFi(); } 
                             else { uploadPendingRecords(); }
                        }
                        redrawMainMenu();
                    }
                }
                break;
            
            case STATE_MENU_GAME_SELECT:
                if (keyPressed) {
                    if (key == '1') sysConfig.gameMode = 0;
                    if (key == '2') sysConfig.gameMode = 1;
                    if (key == '1' || key == '2') { currentState = STATE_MENU_DIFFICULTY; drawDifficultyMenu(); }
                }
                break;

            case STATE_MENU_DIFFICULTY:
                if (keyPressed) {
                    if (key == '1') sysConfig.difficulty = 0;
                    if (key == '2') sysConfig.difficulty = 1;
                    if (key == '3') sysConfig.difficulty = 2;
                    if (key >= '1' && key <= '3') {
                        currentScore = 0; gameStartTime = millis();
                        currentState = STATE_GUIDE_READY; screen_msg("Ready?", "Press 5", C_SUCCESS);
                    }
                }
                break;

            case STATE_MENU_SETTINGS:
                if (keyPressed) {
                    if (key == '4') { if(settingsPage>0) settingsPage--; redrawSettingsMenu(); }
                    if (key == '6') { if(settingsPage<1) settingsPage++; redrawSettingsMenu(); }
                    
                    if (settingsPage == 0) {
                        if (key == '1') { 
                             startAPMode(); 
                        }
                        if (key == '2') { 
                            sysConfig.colorBlind = !sysConfig.colorBlind;
                            saveHistory();
                            redrawSettingsMenu();
                        }
                        if (key == '3') { 
                            sysConfig.fontSize = !sysConfig.fontSize;
                            saveHistory();
                            redrawSettingsMenu();
                        }
                    } else {
                        if (key == '1') {
                             clearHistory(); screen_msg("Data Cleared", "", C_ERROR);
                             vTaskDelay(1000); redrawSettingsMenu();
                        }
                        if (key == '2') {
                             sysConfig.wifiEnabled = !sysConfig.wifiEnabled;
                             if(sysConfig.wifiEnabled) initWiFi(); else stopWiFi();
                             saveHistory();
                             redrawSettingsMenu();
                        }
                    }
                }
                break;
            
            case STATE_MENU_DATA: 
                if (keyPressed) {
                    if (key == '5') {
                         screen_msg("Syncing...", "Please Wait", C_ACCENT);
                         if (sysConfig.wifiEnabled) {
                             if (!firebaseReady) initWiFi();
                             syncData();
                         }
                         drawDataHistoryScreen(); 
                    }
                }
                break; 

            case STATE_GUIDE_READY:
                if (keyPressed && key == '5') { currentState = STATE_GUIDE_OBSERVE; screen_msg("Observe", "Press 5", C_ACCENT); }
                break;
            case STATE_GUIDE_OBSERVE:
                if (keyPressed && key == '5') {
                    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                        tft->fillScreen(getBgColor()); tft->drawFastVLine(startX - 10, 0, 320, C_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                    }
                    if (sysConfig.gameMode == 0) { 
                        for(int i=0; i<9; i++) drawGameElement(i, false); currentState = STATE_MEM_SHOWING; 
                    } else { currentState = STATE_CNT_PREPARE; }
                    vTaskDelay(500);
                }
                break;

            case STATE_MEM_SHOWING:
                if (sequence.empty() || currentScore >= sequence.size()) sequence.push_back(random(0, 9));
                update_status("Watch...", getThemeColor(0));
                for (int step : sequence) {
                    if (delayWithCheck(1000)) break; 
                    drawGameElement(step, true); playTone(TONES[step], 200);
                    if (delayWithCheck(500)) break;
                    drawGameElement(step, false);
                }
                if (currentState == STATE_MEM_SHOWING) {
                    currentState = STATE_GUIDE_RECALL; screen_msg("Recall", "Press 5", getThemeColor(0));
                }
                break;
            
            case STATE_GUIDE_RECALL:
                if (keyPressed && key == '5') {
                    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                        tft->fillScreen(getBgColor()); tft->drawFastVLine(startX - 10, 0, 320, C_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                    }
                    if (sysConfig.gameMode == 0) {
                        for(int i=0; i<9; i++) drawGameElement(i, false); currentState = STATE_MEM_INPUT; update_status("Input!", getThemeColor(2)); inputIndex = 0;
                    } else { currentState = STATE_CNT_INPUT; update_status("Tap Any!", getThemeColor(2)); userTapCount = 0; lastTapTime = millis(); }
                }
                break;

            case STATE_MEM_INPUT:
                 if (keyPressed) {
                    int userPress = key - '1';
                    if (userPress >= 0 && userPress <= 8) {
                        drawGameElement(userPress, true); playTone(TONES[userPress], 100);
                        vTaskDelay(100); drawGameElement(userPress, false);
                        if (userPress == sequence[inputIndex]) {
                            inputIndex++;
                            if (inputIndex >= sequence.size()) {
                                currentScore++; update_status("Good!", getThemeColor(2)); vTaskDelay(1000);
                                currentState = STATE_GUIDE_OBSERVE; screen_msg("Next", "Press 5", getThemeColor(2));
                            }
                        } else {
                            update_status("Wrong", getThemeColor(3)); playTone(100, 500); currentState = STATE_GAME_OVER; 
                        }
                    }
                 }
                 break;

            case STATE_CNT_PREPARE:
                 targetCount = random(1, 6); sequence.clear(); for(int i=0; i<9; i++) sequence.push_back(i);
                 for(int i=0; i<9; i++) { int r = random(i, 9); int t=sequence[i]; sequence[i]=sequence[r]; sequence[r]=t; }
                 if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                     tft->fillScreen(getBgColor()); tft->drawFastVLine(startX - 10, 0, 320, C_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                 }
                 for(int i=0; i<targetCount; i++) drawGameElement(sequence[i], true);
                 update_status("Count", getThemeColor(1)); showStartTime = millis(); currentState = STATE_CNT_WAITING;
                 break;
            case STATE_CNT_WAITING:
                 if ((millis() - showStartTime > 5000) || (keyPressed && key == '8')) {
                      currentState = STATE_GUIDE_RECALL; screen_msg("How Many?", "Press 5", getThemeColor(0));
                 }
                 break;
            case STATE_CNT_INPUT:
                 if (keyPressed) {
                     userTapCount++; lastTapTime = millis(); playTone(880, 50); update_status(String(userTapCount), getThemeColor(0));
                 }
                 if (userTapCount > 0 && millis() - lastTapTime > 2000) {
                     if (userTapCount == targetCount) {
                         currentScore++; update_status("Correct!", getThemeColor(2)); vTaskDelay(1000);
                         currentState = STATE_GUIDE_OBSERVE; screen_msg("Next", "Press 5", getThemeColor(2));
                     } else {
                         update_status("Wrong", getThemeColor(3)); currentState = STATE_GAME_OVER;
                     }
                 }
                 break;

            case STATE_GAME_OVER:
                if (keyPressed) {
                    if (key == '5') {
                        currentScore = 0; currentState = STATE_GUIDE_READY; screen_msg("Retry", "Press 5", getThemeColor(1));
                    } else if (key == '9') {
                        screen_msg("Saving...", "Please Wait", getThemeColor(1));
                        bool uploaded = saveScoreToFirebase(currentScore, (millis()-gameStartTime)/1000);
                        if (uploaded) screen_msg("UPLOAD", "SUCCESS!", getThemeColor(2));
                        else screen_msg("SAVED", "LOCALLY", getThemeColor(3));
                        vTaskDelay(2000); 
                        currentState = STATE_MENU_MAIN; redrawMainMenu();
                    }
                }
                break;
        }
        vTaskDelay(10);
    }
}

// ==========================================
// 11. 初始化與主循環
// ==========================================
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUZZER, OUTPUT);

    tft->begin();
    tft->setRotation(1); 
    tft->fillScreen(C_BG_LIGHT);
    if (!ts.begin()) Serial.println("Touch Fail");
    ts.setRotation(1); 

    xGuiMutex = xSemaphoreCreateMutex();
    loadHistory(); 
    randomSeed(analogRead(34));

    xTaskCreatePinnedToCore(task_system, "Sys", 8192, NULL, 1, NULL, 1);
}

void loop() { vTaskDelete(NULL); }
