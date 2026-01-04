/*
 * Memory Bloom - V15.5 (Partition Config & Slim)
 * ------------------------------------------------
 * 🛑 必讀：解決 "Sketch too big" 編譯錯誤
 * 您的程序太大 (約 1.5MB)，默認分區 (1.2MB) 放不下。
 * 請在 Arduino IDE 修改分區方案：
 * ➡️ Tools (工具) -> Partition Scheme (分區方案) -> "Huge APP (3MB No OTA)"
 * ------------------------------------------------
 * 更新內容：
 * 1. 移除未使用的 Firebase 輔助庫 (節省編譯時間和空間)
 * 2. 保持所有核心功能 (WiFi, Firebase, GUI, 存儲)
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

// 移除了不使用的 TokenHelper 和 RTDBHelper 以節省空間
// #include <addons/TokenHelper.h> 
// #include <addons/RTDBHelper.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==========================================
// 1. 配置與憑證 (⚠️ 請修改這裡)
// ==========================================
const char* WIFI_SSID = "Ivan";         
const char* WIFI_PASSWORD = "awc121091"; 

// 這裡必須填寫您的 Firebase 真實信息
// Host 例子: "memory-bloom-12345-default-rtdb.firebaseio.com" (不要帶 https://)
#define FIREBASE_HOST "your-project-id.firebaseio.com" 
#define FIREBASE_AUTH "your_database_secret_key"

// ==========================================
// 2. 結構體定義
// ==========================================
struct SystemConfig {
    int difficulty;  // 0: Easy, 1: Hard, 2: Auto
    bool soundOn;    // true: On
    int gameMode;    // 0: Memory, 1: Counting
};
SystemConfig sysConfig = {2, true, 0}; 

struct GameSession {
    int modeType; 
    int score;
    unsigned long durationSeconds;
    time_t timestamp;      
    String sessionID;      
};

// ==========================================
// 3. 全局變量
// ==========================================
Preferences preferences;
std::vector<GameSession> gameHistory;    
std::vector<GameSession> pendingUploads; 
unsigned long gameStartTime = 0; 
SemaphoreHandle_t xGuiMutex; 

// Firebase 變量
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;
unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL = 5000; 

// 狀態機
enum GameState { 
    STATE_WIFI_CONNECTING, 
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
#define COLOR_BG_LIGHT    0xFFDF 
#define COLOR_BG_SHADOW   0x9CF3 
#define COLOR_PRIMARY     0x43B2 
#define COLOR_SECONDARY   0xF9A8 
#define COLOR_ACCENT      0xFD60 
#define COLOR_SUCCESS     0x07E0 
#define COLOR_ERROR       0xF800 
#define COLOR_BTN_NORMAL  0xDEFB  
#define COLOR_BTN_ACTIVE  0xFD60  
#define COLOR_TEXT_LIGHT  0x8410  

// ==========================================
// 6. 前向聲明
// ==========================================
void initWiFi();
void initFirebase();
void uploadPendingRecords();
void uploadRecord(GameSession record);
void updateStatistics(int score);
void saveHistory();
void loadHistory();
void clearHistory();
void drawDataHistoryScreen();
void redrawMainMenu();
void update_status(String text, uint16_t color);
void drawGameElement(int id, bool active);
void playTone(int freq, int duration);

// ==========================================
// 7. 輔助函數與數據存儲
// ==========================================
void playTone(int freq, int duration) {
    if (sysConfig.soundOn) tone(PIN_BUZZER, freq, duration);
}

void saveHistory() {
    if (gameHistory.size() > 10) {
        std::vector<GameSession> recentHistory;
        for (int i = gameHistory.size() - 10; i < gameHistory.size(); i++) {
            recentHistory.push_back(gameHistory[i]);
        }
        gameHistory = recentHistory;
    }
    preferences.begin("mb_data", false);
    int count = gameHistory.size();
    preferences.putInt("count", count);
    for (int i = 0; i < count; i++) {
        String prefix = "g" + String(i);
        uint8_t buffer[12];
        memcpy(buffer, &gameHistory[i].modeType, sizeof(int));
        memcpy(buffer + 4, &gameHistory[i].score, sizeof(int));
        memcpy(buffer + 8, &gameHistory[i].durationSeconds, sizeof(unsigned long));
        preferences.putBytes(prefix.c_str(), buffer, 12); 
    }
    preferences.end();
}

void loadHistory() {
    preferences.begin("mb_data", true);
    int count = preferences.getInt("count", 0);
    gameHistory.clear();
    for (int i = 0; i < count; i++) {
        String prefix = "g" + String(i);
        uint8_t buffer[12];
        if (preferences.getBytes(prefix.c_str(), buffer, 12) > 0) {
            GameSession s;
            memcpy(&s.modeType, buffer, sizeof(int));
            memcpy(&s.score, buffer + 4, sizeof(int));
            memcpy(&s.durationSeconds, buffer + 8, sizeof(unsigned long));
            s.sessionID = "-"; 
            gameHistory.push_back(s);
        }
    }
    preferences.end();
}

void clearHistory() {
    preferences.begin("mb_data", false);
    preferences.clear();
    preferences.end();
    gameHistory.clear();
    playTone(500, 200);
}

void saveScoreToFirebase(int score, unsigned long duration) {
    GameSession record;
    record.modeType = sysConfig.gameMode;
    record.score = score;
    record.durationSeconds = duration;
    record.timestamp = time(nullptr); 
    record.sessionID = String(millis());
    
    gameHistory.push_back(record);
    saveHistory();
    
    if (firebaseReady && WiFi.status() == WL_CONNECTED) {
        uploadRecord(record);
    } else {
        pendingUploads.push_back(record);
    }
    updateStatistics(score);
}

// ==========================================
// 8. 網絡功能
// ==========================================
void initWiFi() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        tft->setTextColor(COLOR_PRIMARY); tft->setTextSize(3);
        tft->setCursor(20, 100); tft->println("Connecting");
        tft->setCursor(20, 140); tft->println("to WiFi...");
        xSemaphoreGive(xGuiMutex);
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { 
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        initFirebase();
    }
}

void initFirebase() {
    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    
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
        // 語法修正: .RTDB
        Firebase.RTDB.setString(&fbdo, devicePath + "/lastSeen", String(millis()/1000));
        uploadPendingRecords();
    }
}

void uploadRecord(GameSession record) {
    if (!firebaseReady) return;
    String deviceID = WiFi.macAddress();
    String recordPath = "/devices/" + deviceID + "/sessions/" + record.sessionID;
    
    FirebaseJson json;
    json.set("mode", record.modeType == 0 ? "memory" : "counting");
    json.set("score", record.score);
    json.set("duration", record.durationSeconds);
    json.set("timestamp", (double)record.timestamp);
    
    // 語法修正: .RTDB
    if (!Firebase.RTDB.setJSON(&fbdo, recordPath.c_str(), &json)) {
        pendingUploads.push_back(record);
    }
}

void uploadPendingRecords() {
    if (pendingUploads.empty() || !firebaseReady) return;
    std::vector<GameSession> retryQueue = pendingUploads;
    pendingUploads.clear();
    for (auto& rec : retryQueue) {
        uploadRecord(rec);
        delay(50);
    }
}

void updateStatistics(int score) {
    if (!firebaseReady) return;
    String deviceID = WiFi.macAddress();
    String statsPath = "/statistics/" + deviceID;
    
    int totalGames = 0, totalScore = 0, highScore = 0;
    
    // 語法修正: .RTDB
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/totalGames")) totalGames = fbdo.to<int>();
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/totalScore")) totalScore = fbdo.to<int>();
    if (Firebase.RTDB.getInt(&fbdo, statsPath + "/highScore")) highScore = fbdo.to<int>();
    
    totalGames++;
    totalScore += score;
    if (score > highScore) highScore = score;
    
    FirebaseJson statsJson;
    statsJson.set("totalGames", totalGames);
    statsJson.set("totalScore", totalScore);
    statsJson.set("highScore", highScore);
    
    // 語法修正: .RTDB
    Firebase.RTDB.setJSON(&fbdo, statsPath.c_str(), &statsJson);
}

// ==========================================
// 9. UI 繪製
// ==========================================
void drawRoundedButton(int x, int y, int w, int h, uint16_t color, bool pressed = false) {
    if (pressed) {
        tft->fillRoundRect(x + 2, y + 2, w, h, 12, color);
    } else {
        tft->fillRoundRect(x + 4, y + 4, w, h, 12, COLOR_BG_SHADOW);
        tft->fillRoundRect(x, y, w, h, 12, color);
    }
}

void drawMenuBtn(int slot, String text, uint16_t color, bool selected) {
    int y = 90 + (slot * 70);
    int x = 60; int w = 360; int h = 55;
    if (selected) color = COLOR_ACCENT;
    drawRoundedButton(x, y, w, h, color, false);
    tft->setTextColor(WHITE); tft->setTextSize(3);
    int txtX = x + (w - (text.length() * 18)) / 2;
    tft->setCursor(txtX, y + 16);
    tft->print(text);
}

void drawTextMenuItem(int slot, String label, String value, bool selected) {
    int y = 80 + (slot * 65);
    uint16_t color = selected ? COLOR_ACCENT : COLOR_BTN_NORMAL;
    uint16_t txtColor = selected ? WHITE : BLACK;
    drawRoundedButton(40, y, 400, 55, color, false);
    tft->setTextColor(txtColor); tft->setTextSize(3);
    tft->setCursor(60, y + 16); tft->print(label);
    if (value != "") { tft->setCursor(280, y + 16); tft->print(value); }
}

void drawGameElement(int id, bool active) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        int row = id / 3; int col = id % 3;
        int cx = startX + col * (btnWidth + gap) + (btnWidth / 2);
        int cy = startY + row * (btnHeight + gap) + (btnHeight / 2);
        tft->fillRect(cx - 45, cy - 40, 90, 80, COLOR_BG_LIGHT);

        if (sysConfig.gameMode == 0) { 
            uint16_t color = active ? COLOR_BTN_ACTIVE : COLOR_BTN_NORMAL;
            tft->fillRoundRect(cx - 40, cy - 35, 80, 70, 12, color);
            if (!active) tft->drawRoundRect(cx - 40, cy - 35, 80, 70, 12, COLOR_BG_SHADOW);
            tft->setTextColor(active ? WHITE : BLACK); tft->setTextSize(3);
            tft->setCursor(cx - 8, cy - 10); tft->print(id + 1);
        } else { 
            if (active) {
                tft->fillCircle(cx, cy, 25, COLOR_SECONDARY);
                tft->fillCircle(cx, cy, 15, WHITE);
            } else {
                tft->drawCircle(cx, cy, 25, COLOR_PRIMARY);
                tft->drawCircle(cx, cy, 24, COLOR_PRIMARY);
            }
        }
        xSemaphoreGive(xGuiMutex);
    }
}

void update_status(String text, uint16_t color = COLOR_PRIMARY) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillRect(0, 0, 480, 30, COLOR_BG_LIGHT); 
        tft->drawFastVLine(startX - 10, 20, 280, COLOR_BG_SHADOW);
        tft->setTextColor(color); tft->setTextSize(3); tft->setCursor(20, 100); 
        int y = 100;
        for (int i = 0; i < text.length(); i += 8) {
            tft->setCursor(20, y);
            tft->println(text.substring(i, min((int)text.length(), i + 8)));
            y += 40;
        }
        if (currentState != STATE_MENU_MAIN && currentState != STATE_GAME_OVER && currentState != STATE_WIFI_CONNECTING) {
            tft->setTextSize(2); tft->setTextColor(BLACK); tft->setCursor(350, 10);
            tft->print("Score: "); tft->setTextColor(COLOR_ACCENT); tft->print(currentScore);
        }
        xSemaphoreGive(xGuiMutex);
    }
}

void screen_msg(String l1, String l2, uint16_t color = COLOR_PRIMARY) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        tft->drawRoundRect(20, 20, 440, 280, 20, COLOR_BG_SHADOW);
        tft->setTextColor(color); tft->setTextSize(4);
        tft->setCursor(60, 100); tft->println(l1);
        tft->setTextColor(BLACK); tft->setTextSize(3);
        tft->setCursor(60, 160); tft->println(l2);
        xSemaphoreGive(xGuiMutex);
    }
}

void drawDataHistoryScreen() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        tft->setTextColor(COLOR_PRIMARY); tft->setTextSize(3);
        tft->setCursor(20, 20); tft->print("GAME HISTORY");
        
        tft->setTextColor(BLACK); tft->setTextSize(2);
        tft->setCursor(30, 60); tft->print("Mode");
        tft->setCursor(180, 60); tft->print("Score");
        tft->setCursor(320, 60); tft->print("Time");
        tft->drawFastHLine(20, 80, 440, COLOR_BG_SHADOW);
        
        int y = 95;
        if (gameHistory.empty()) {
            tft->setCursor(150, 150); tft->print("No Records");
        } else {
            int count = 0;
            for (int i = gameHistory.size() - 1; i >= 0; i--) {
                if (count >= 6) break;
                
                GameSession s = gameHistory[i];
                tft->setTextColor(COLOR_TEXT_LIGHT);
                
                tft->setCursor(30, y); tft->print(s.modeType == 0 ? "Mem" : "Cnt");
                tft->setCursor(190, y); tft->print(s.score);
                tft->setCursor(320, y); tft->print(s.durationSeconds); tft->print("s");
                y += 35;
                count++;
            }
        }
        tft->setTextColor(COLOR_ERROR);
        tft->setCursor(20, 300); tft->print("9. Back"); 
        xSemaphoreGive(xGuiMutex);
    }
}

void redrawMainMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        
        tft->setTextSize(2);
        if (firebaseReady) {
            tft->setTextColor(COLOR_SUCCESS); tft->setCursor(20, 20); tft->print("Cloud Online");
        } else if (WiFi.status() == WL_CONNECTED) {
            tft->setTextColor(COLOR_ACCENT); tft->setCursor(20, 20); tft->print("WiFi Only");
        } else {
            tft->setTextColor(COLOR_ERROR); tft->setCursor(20, 20); tft->print("Offline");
        }

        tft->setTextColor(COLOR_PRIMARY); tft->setTextSize(4);
        tft->setCursor(80, 60); tft->print("Memory Bloom");
        xSemaphoreGive(xGuiMutex);
    }
    drawMenuBtn(0, "1. Start Game", COLOR_SECONDARY, false);
    drawMenuBtn(1, "2. Settings", COLOR_PRIMARY, false);
    drawMenuBtn(2, "3. Data View", COLOR_ACCENT, false);
    
    // 手動同步按鈕
    drawMenuBtn(3, "4. Sync Data", firebaseReady ? COLOR_SUCCESS : COLOR_ERROR, false);
    
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(BLACK); tft->setTextSize(2);
        tft->setCursor(100, 300); tft->print("Hold '9' to Exit");
        xSemaphoreGive(xGuiMutex);
    }
}

void drawDifficultyMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        tft->setTextColor(COLOR_PRIMARY); tft->setTextSize(3);
        tft->setCursor(120, 30); tft->print("DIFFICULTY");
        xSemaphoreGive(xGuiMutex);
    }
    drawTextMenuItem(0, "1. Easy", "", false);
    drawTextMenuItem(1, "2. Hard", "", false);
    drawTextMenuItem(2, "3. Auto", "(Best)", false);
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(COLOR_ERROR); tft->setTextSize(2);
        tft->setCursor(20, 300); tft->print("9. Back");
        xSemaphoreGive(xGuiMutex);
    }
}

void redrawSettingsMenu() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG_LIGHT);
        tft->setTextColor(COLOR_PRIMARY); tft->setTextSize(3);
        tft->setCursor(150, 30); tft->print("SETTINGS");
        xSemaphoreGive(xGuiMutex);
    }
    drawMenuBtn(0, "1. Sound: " + String(sysConfig.soundOn ? "ON" : "OFF"), COLOR_BTN_NORMAL, false);
    drawMenuBtn(1, "2. Clear Data", COLOR_ERROR, false);
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->setTextColor(COLOR_ERROR); tft->setTextSize(2);
        tft->setCursor(20, 300); tft->print("9. Back");
        xSemaphoreGive(xGuiMutex);
    }
}

// ==========================================
// 10. 系統任務
// ==========================================
void task_system(void *pv) {
    initWiFi();
    currentState = STATE_MENU_MAIN; 
    redrawMainMenu();

    while(1) {
        char key = keypad.getKey();
        bool keyPressed = (key != NO_KEY && keypad.getState() == PRESSED);

        // 後台任務
        if (firebaseReady && !pendingUploads.empty() && millis() - lastUploadTime > UPLOAD_INTERVAL) {
            uploadPendingRecords();
            lastUploadTime = millis();
        }

        // 全局操作: 長按 9 退出
        if (key == '9' && keypad.getState() == HOLD) {
             if (currentState != STATE_MENU_MAIN) {
                 playTone(1000, 500); 
                 currentState = STATE_MENU_MAIN;
                 redrawMainMenu();
                 vTaskDelay(500); 
                 continue; 
             }
        }
        // 全局操作: 單按 9 返回
        if (keyPressed && key == '9') {
            if (currentState == STATE_MENU_GAME_SELECT || currentState == STATE_MENU_SETTINGS || currentState == STATE_MENU_DATA) {
                currentState = STATE_MENU_MAIN; redrawMainMenu(); continue;
            } else if (currentState == STATE_MENU_DIFFICULTY) {
                currentState = STATE_MENU_GAME_SELECT; screen_msg("Select Game", "1.Memory 2.Count"); continue;
            }
        }

        switch (currentState) {
            case STATE_MENU_MAIN:
                if (keyPressed) {
                    if (key == '1') { currentState = STATE_MENU_GAME_SELECT; screen_msg("Select Game", "1.Memory 2.Count"); }
                    else if (key == '2') { currentState = STATE_MENU_SETTINGS; redrawSettingsMenu(); }
                    else if (key == '3') { currentState = STATE_MENU_DATA; drawDataHistoryScreen(); }
                    else if (key == '4') { // 手動同步
                        if (!firebaseReady) { initWiFi(); } 
                        else { uploadPendingRecords(); }
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
                        currentState = STATE_GUIDE_READY; screen_msg("Ready?", "Press 5", COLOR_SUCCESS);
                    }
                }
                break;

            case STATE_MENU_SETTINGS:
                if (keyPressed) {
                    if (key == '1') { sysConfig.soundOn = !sysConfig.soundOn; redrawSettingsMenu(); }
                    else if (key == '2') { clearHistory(); screen_msg("Data Cleared", "", COLOR_ERROR); vTaskDelay(1000); redrawSettingsMenu(); }
                }
                break;
            
            case STATE_MENU_DATA: break; // 等待 9 返回

            case STATE_GUIDE_READY:
                if (keyPressed && key == '5') { currentState = STATE_GUIDE_OBSERVE; screen_msg("Observe", "Press 5", COLOR_ACCENT); }
                break;
            case STATE_GUIDE_OBSERVE:
                if (keyPressed && key == '5') {
                    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                        tft->fillScreen(COLOR_BG_LIGHT); tft->drawFastVLine(startX - 10, 0, 320, COLOR_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                    }
                    if (sysConfig.gameMode == 0) { 
                        for(int i=0; i<9; i++) drawGameElement(i, false); currentState = STATE_MEM_SHOWING; 
                    } else { currentState = STATE_CNT_PREPARE; }
                    vTaskDelay(500);
                }
                break;

            case STATE_MEM_SHOWING:
                if (sequence.empty() || currentScore >= sequence.size()) sequence.push_back(random(0, 9));
                update_status("Watch...");
                for (int step : sequence) {
                    vTaskDelay(1000); drawGameElement(step, true); playTone(TONES[step], 200);
                    vTaskDelay(500); drawGameElement(step, false);
                }
                currentState = STATE_GUIDE_RECALL; screen_msg("Recall", "Press 5", COLOR_PRIMARY);
                break;
            
            case STATE_GUIDE_RECALL:
                if (keyPressed && key == '5') {
                    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                        tft->fillScreen(COLOR_BG_LIGHT); tft->drawFastVLine(startX - 10, 0, 320, COLOR_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                    }
                    if (sysConfig.gameMode == 0) {
                        for(int i=0; i<9; i++) drawGameElement(i, false); currentState = STATE_MEM_INPUT; update_status("Input!", COLOR_SUCCESS); inputIndex = 0;
                    } else { currentState = STATE_CNT_INPUT; update_status("Tap Any!", COLOR_SUCCESS); userTapCount = 0; lastTapTime = millis(); }
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
                                currentScore++; update_status("Good!", COLOR_SUCCESS); vTaskDelay(1000);
                                currentState = STATE_GUIDE_OBSERVE; screen_msg("Next", "Press 5", COLOR_SUCCESS);
                            }
                        } else {
                            update_status("Wrong", COLOR_ERROR); playTone(100, 500); currentState = STATE_GAME_OVER; 
                        }
                    }
                 }
                 break;

            case STATE_CNT_PREPARE:
                 targetCount = random(1, 6); sequence.clear(); for(int i=0; i<9; i++) sequence.push_back(i);
                 for(int i=0; i<9; i++) { int r = random(i, 9); int t=sequence[i]; sequence[i]=sequence[r]; sequence[r]=t; }
                 if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
                     tft->fillScreen(COLOR_BG_LIGHT); tft->drawFastVLine(startX - 10, 0, 320, COLOR_BG_SHADOW); xSemaphoreGive(xGuiMutex);
                 }
                 for(int i=0; i<targetCount; i++) drawGameElement(sequence[i], true);
                 update_status("Count"); showStartTime = millis(); currentState = STATE_CNT_WAITING;
                 break;
            case STATE_CNT_WAITING:
                 if ((millis() - showStartTime > 5000) || (keyPressed && key == '8')) {
                      currentState = STATE_GUIDE_RECALL; screen_msg("How Many?", "Press 5", COLOR_PRIMARY);
                 }
                 break;
            case STATE_CNT_INPUT:
                 if (keyPressed) {
                     userTapCount++; lastTapTime = millis(); playTone(880, 50); update_status(String(userTapCount));
                 }
                 if (userTapCount > 0 && millis() - lastTapTime > 2000) {
                     if (userTapCount == targetCount) {
                         currentScore++; update_status("Correct!", COLOR_SUCCESS); vTaskDelay(1000);
                         currentState = STATE_GUIDE_OBSERVE; screen_msg("Next", "Press 5", COLOR_SUCCESS);
                     } else {
                         update_status("Wrong", COLOR_ERROR); currentState = STATE_GAME_OVER;
                     }
                 }
                 break;

            case STATE_GAME_OVER:
                if (keyPressed) {
                    if (key == '5') {
                        currentScore = 0; currentState = STATE_GUIDE_READY; screen_msg("Retry", "Press 5", COLOR_ACCENT);
                    } else if (key == '9') {
                        saveScoreToFirebase(currentScore, (millis()-gameStartTime)/1000);
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
    tft->fillScreen(COLOR_BG_LIGHT);
    if (!ts.begin()) Serial.println("Touch Fail");
    ts.setRotation(1); 

    xGuiMutex = xSemaphoreCreateMutex();
    loadHistory(); 
    randomSeed(analogRead(34));

    xTaskCreatePinnedToCore(task_system, "Sys", 8192, NULL, 1, NULL, 1);
}

void loop() { 
    vTaskDelete(NULL); 
}
