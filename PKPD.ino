/*
 * Memory Bloom - V5.0 (Garden Therapy Edition)
 * ------------------------------------------------
 * 專為長者設計的記憶訓練遊戲 - 花園主題版
 * * 硬件要求：
 * 1. ⚠️ 必須拔掉屏幕的 SDO (MISO) 線 (Pin 9) 以免干擾觸摸屏
 * 2. 屏幕驅動: ILI9488 (SPI)
 * 3. 觸摸驅動: XPT2046
 */

#include <Arduino_GFX_Library.h> 
#include <Keypad.h>
#include <XPT2046_Touchscreen.h> 
#include <vector>
#include <SPI.h>

// FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==========================================
// 1. 硬件定義與驅動配置
// ==========================================

// --- 屏幕設置 ---
// MISO 設為 -1 (不使用)，因為我們只需要寫入屏幕
Arduino_DataBus *bus = new Arduino_ESP32SPI(17 /* DC */, 5 /* CS */, 18 /* SCK */, 23 /* MOSI */, -1 /* MISO */);
// 初始化 ILI9488 (IPS 屏幕通常效果較好)
Arduino_GFX *tft = new Arduino_ILI9488_18bit(bus, 16 /* RST */, 1, false);

// --- 觸摸屏設置 ---
#define TOUCH_CS_PIN  4  
#define TOUCH_IRQ_PIN 26 
XPT2046_Touchscreen ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// --- 蜂鳴器 ---
#define PIN_BUZZER 25
// 音階頻率 (C4 到 D5)，讓聲音聽起來像音樂而不是噪音
const int TONES[] = {262, 294, 330, 349, 392, 440, 494, 523, 587}; 

// --- 矩陣鍵盤 ---
const byte ROWS = 3; 
const byte COLS = 3; 
char keys[ROWS][COLS] = { {'1','2','3'}, {'4','5','6'}, {'7','8','9'} };
byte rowPins[ROWS] = {32, 33, 27}; 
byte colPins[COLS] = {14, 12, 13}; 

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ==========================================
// 2. 配色表 (老年人友善配色)
// ==========================================
// 使用 RGB565 格式
#define C_BG          0xFFF4 // 米黃色背景 (Cream) - 護眼
#define C_STEM        0x2589 // 莖葉綠 (Forest Green)
#define C_FLOWER_OFF  0xE71C // 淺粉色/含苞 (Light Pink) - 待機狀態
#define C_FLOWER_ON   0xFBC0 // 盛開亮橘色 (Vibrant Orange) - 激活狀態
#define C_TEXT_DARK   0x2104 // 深灰色文字 (High Contrast) - 易讀
#define C_TEXT_LIGHT  0xFFFF // 白色文字
#define C_PANEL       0xDEFB // 狀態欄淺灰
#define C_SHADOW      0x9492 // 陰影色 (用於增加立體感)
#define C_WHITE       0xFFFF

// ==========================================
// 3. 全局變量
// ==========================================
SemaphoreHandle_t xGuiMutex; // 互斥鎖，防止繪圖衝突
enum GameState { STATE_IDLE, STATE_SHOWING, STATE_INPUT, STATE_GAMEOVER, STATE_TEST };
GameState currentState = STATE_IDLE;

std::vector<int> sequence; // 存儲題目序列
int inputIndex = 0;        // 當前輸入進度
int currentScore = 0;      // 當前分數

// 用於長按 "9" 進入測試模式
unsigned long key9_press_time = 0;
bool is_key9_pressing = false;

// 佈局參數
int startX = 160;   // 花園區域起始 X (屏幕右側)
int startY = 30;    // 花園區域起始 Y
int btnWidth = 90;  // 花朵佔用寬度
int btnHeight = 80; // 花朵佔用高度
int gap = 15;       // 間距

// ==========================================
// 4. UI 繪圖函數 (核心修改部分)
// ==========================================

// 輔助：畫圓角矩形框（帶陰影，增加物體辨識度）
void drawPanel(int x, int y, int w, int h, uint16_t color) {
    tft->fillRoundRect(x + 4, y + 4, w, h, 10, C_SHADOW); // 陰影
    tft->fillRoundRect(x, y, w, h, 10, color);            // 本體
}

// 畫一朵花 (代替原本的方塊按鈕)
// id: 0-8, active: true(亮/盛開), false(暗/含苞)
void drawFlower(int id, bool active) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        int row = id / 3;
        int col = id % 3;
        // 計算中心點
        int cx = startX + col * (btnWidth + gap) + (btnWidth / 2);
        int cy = startY + row * (btnHeight + gap) + (btnHeight / 2);
        
        uint16_t petalColor = active ? C_FLOWER_ON : C_FLOWER_OFF;

        // 1. 清除該區域背景
        tft->fillRect(cx - btnWidth/2, cy - btnHeight/2, btnWidth, btnHeight, C_BG);

        // 2. 畫莖葉 (裝飾)
        tft->drawLine(cx, cy + 10, cx, cy + 35, C_STEM);
        // 畫葉子 (用兩個小圓模擬)
        tft->fillCircle(cx - 12, cy + 25, 6, C_STEM);
        tft->fillCircle(cx + 12, cy + 25, 6, C_STEM);

        // 3. 畫花瓣 (由4個圓組成，模擬繡球花或幸運草形狀)
        int r = 14; 
        tft->fillCircle(cx - 9, cy - 9, r, petalColor);
        tft->fillCircle(cx + 9, cy - 9, r, petalColor);
        tft->fillCircle(cx - 9, cy + 9, r, petalColor);
        tft->fillCircle(cx + 9, cy + 9, r, petalColor);

        // 4. 畫花蕊 (顯示數字，方便對應鍵盤)
        tft->fillCircle(cx, cy, 13, C_TEXT_LIGHT); // 白底
        tft->drawCircle(cx, cy, 13, C_SHADOW);     // 邊框

        tft->setCursor(cx - 6, cy - 8); 
        tft->setTextColor(C_TEXT_DARK); 
        tft->setTextSize(3);      
        tft->print(id + 1);

        xSemaphoreGive(xGuiMutex);
    }
}

// 更新左側狀態欄信息
// line1: 大標題, line2: 次要信息, color: 標題顏色
void update_status(String line1, String line2, uint16_t color) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        // 清除左側區域 (保留右側花園)
        tft->fillRect(0, 80, startX - 5, 240, C_BG); 
        
        // 畫一個信息板
        drawPanel(10, 90, 135, 140, C_PANEL);

        // 第一行 (狀態/標題)
        tft->setTextColor(color);
        tft->setTextSize(3); 
        // 簡單的自動換行處理 (如果文字太長手動換行)
        tft->setCursor(20, 110);
        tft->println(line1);
        
        // 第二行 (說明/分數)
        tft->setTextColor(C_TEXT_DARK); 
        tft->setTextSize(2);
        tft->setCursor(20, 160);
        tft->println(line2);

        xSemaphoreGive(xGuiMutex);
    }
}

// 初始化整個界面
void build_ui() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(C_BG); // 刷上米色背景

        // 左上角標題區
        tft->setCursor(15, 30); 
        tft->setTextColor(C_STEM); // 綠色標題
        tft->setTextSize(3);
        tft->println("Memory");
        tft->setCursor(15, 60);
        tft->println("Bloom");
        
        // 畫一條分隔線
        tft->drawFastVLine(startX - 15, 20, 280, C_SHADOW);
        xSemaphoreGive(xGuiMutex);
    }
    
    // 畫出初始的 9 朵花 (未激活狀態)
    for(int i=0; i<9; i++) drawFlower(i, false);
    
    // 初始提示
    update_status("Welcome", "Press '5'\nTo Start", C_TEXT_DARK);
}

// 進入硬件測試模式 (維護用)
void enter_test_mode() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(C_WHITE); 
        tft->setTextColor(C_TEXT_DARK);
        tft->setTextSize(2);
        tft->setCursor(10, 10);
        tft->print("TEST MODE: Draw to test touch");
        tft->setCursor(10, 40);
        tft->print("Press Keypad to Exit");
        xSemaphoreGive(xGuiMutex);
    }
    currentState = STATE_TEST;
    is_key9_pressing = false; 
}

// ==========================================
// 5. 遊戲主邏輯任務
// ==========================================
void task_game(void *pv) {
    while(1) {
        char key = keypad.getKey(); 
        
        // --- 特殊功能：長按 9 進入測試模式 ---
        if (key == '9') {
            if (keypad.getState() == PRESSED) {
                is_key9_pressing = true;
                key9_press_time = millis();
            } else if (keypad.getState() == RELEASED) {
                is_key9_pressing = false;
            }
        }
        if (currentState == STATE_IDLE && is_key9_pressing) {
            if (millis() - key9_press_time > 5000) {
                tone(PIN_BUZZER, 3000, 500); 
                enter_test_mode();
                is_key9_pressing = false; // 重置防止重複觸發
            }
        }

        // --- 測試模式繪圖邏輯 ---
        if (currentState == STATE_TEST && ts.touched()) {
            TS_Point p = ts.getPoint();
            if (xSemaphoreTake(xGuiMutex, 10) == pdTRUE) {
                // XPT2046 坐標映射 (根據實際情況可能需要微調)
                int x_map = map(p.x, 200, 3800, 0, 480); 
                int y_map = map(p.y, 200, 3800, 320, 0); 
                tft->fillCircle(x_map, y_map, 2, C_STEM); 
                xSemaphoreGive(xGuiMutex);
            }
        }

        // --- 狀態機邏輯 ---
        switch (currentState) {
            case STATE_IDLE:
                // 等待按 '5' 開始
                if (key == '5' && keypad.getState() == PRESSED) { 
                    currentScore = 0;
                    sequence.clear();
                    // 播放開始音效
                    tone(PIN_BUZZER, 523, 150); delay(150);
                    tone(PIN_BUZZER, 659, 150); delay(150);
                    tone(PIN_BUZZER, 784, 300);
                    
                    update_status("Watch", "Carefully...", C_FLOWER_ON);
                    vTaskDelay(1500);
                    currentState = STATE_SHOWING;
                }
                break;

            case STATE_TEST:
                // 任意鍵退出測試模式
                if (key && keypad.getState() == PRESSED) {
                    build_ui(); 
                    currentState = STATE_IDLE;
                }
                break;

            case STATE_SHOWING:
                // 生成新題目
                sequence.push_back(random(0, 9));
                
                // 播放序列
                for (int step : sequence) {
                    vTaskDelay(600); // 稍微慢一點，適合老年人
                    drawFlower(step, true); // 花開
                    tone(PIN_BUZZER, TONES[step], 400); // 播放對應音階
                    vTaskDelay(500); 
                    drawFlower(step, false); // 花謝
                }
                
                update_status("Your Turn", "Repeat it!", C_STEM);
                inputIndex = 0;
                currentState = STATE_INPUT;
                break;

            case STATE_INPUT:
                if (key && keypad.getState() == PRESSED) { 
                    int userPress = key - '1'; // 將 char '1' 轉為 int 0
                    
                    if (userPress >= 0 && userPress <= 8) {
                        // 按鍵反饋
                        drawFlower(userPress, true);
                        tone(PIN_BUZZER, TONES[userPress], 200);
                        vTaskDelay(200);
                        drawFlower(userPress, false);

                        // 檢查答案
                        if (userPress == sequence[inputIndex]) {
                            inputIndex++;
                            // 如果序列全部輸入正確
                            if (inputIndex >= sequence.size()) {
                                currentScore++;
                                update_status("Good!", "Score: " + String(currentScore), C_FLOWER_ON);
                                vTaskDelay(1000); // 稍作休息
                                currentState = STATE_SHOWING; // 下一關
                            }
                        } else {
                            // 輸入錯誤
                            currentState = STATE_GAMEOVER;
                        }
                    }
                }
                break;

            case STATE_GAMEOVER:
                // 失敗音效 (低沈溫和，不要太刺耳)
                tone(PIN_BUZZER, 392, 300); vTaskDelay(300);
                tone(PIN_BUZZER, 349, 300); vTaskDelay(300);
                tone(PIN_BUZZER, 330, 800);
                
                update_status("Game Over", "Score: " + String(currentScore), C_SHADOW);
                vTaskDelay(3000); // 給予足夠時間閱讀分數
                
                update_status("Ready?", "Press '5'\nTo Play", C_TEXT_DARK);
                currentState = STATE_IDLE;
                break;
        }
        
        vTaskDelay(10); // 讓出 CPU 資源，防止看門狗重置
    }
}

// ==========================================
// 6. 系統初始化
// ==========================================
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUZZER, OUTPUT);

    // 初始化屏幕
    if (!tft->begin()) {
        Serial.println("TFT Init Fail");
    }
    tft->fillScreen(C_BG);
    
    // 初始化觸摸
    if (!ts.begin()) {
        Serial.println("Touch Init Failed");
    }
    ts.setRotation(1); // 確保觸摸方向與屏幕方向一致

    // 創建互斥鎖
    xGuiMutex = xSemaphoreCreateMutex();
    
    // 繪製初始界面
    build_ui(); 

    // 初始化隨機數種子 (利用懸空引腳的噪聲)
    randomSeed(analogRead(34));

    // 啟動遊戲任務 (運行在 Core 1)
    // 堆棧大小 4096，優先級 1
    xTaskCreatePinnedToCore(task_game, "Game", 4096, NULL, 1, NULL, 1);
}

void loop() { 
    // Arduino 的 loop 在 FreeRTOS 下通常留空或刪除任務
    vTaskDelete(NULL); 
}
