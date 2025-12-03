/*
 * Memory Bloom - 3.5" ILI9488 Real Hardware Version
 * Board: ESP32 DevKit V1
 * Screen: 3.5 inch ILI9488 SPI (480x320)
 */

#include <Arduino_GFX_Library.h> // 這是新庫，專門治白屏
#include <Keypad.h>
#include <vector>

// FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==========================================
// 1. 屏幕驅動配置 (針對你的新屏幕)
// ==========================================
// 創建數據總線
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    17 /* DC */, 5 /* CS */, 18 /* SCK */, 23 /* MOSI */, 19 /* MISO */);

// 創建屏幕對象 (ILI9488)
Arduino_GFX *tft = new Arduino_ILI9488_18bit(bus, 16 /* RST */, 0 /* rotation */, false /* IPS */);

// ==========================================
// 2. 硬件定義
// ==========================================
#define PIN_BUZZER 25

// 鍵盤配置 (和 Wokwi 一樣)
const byte ROWS = 3; 
const byte COLS = 3; 
char keys[ROWS][COLS] = { {'1','2','3'}, {'4','5','6'}, {'7','8','9'} };
byte rowPins[ROWS] = {32, 33, 27}; 
byte colPins[COLS] = {14, 12, 13}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ==========================================
// 3. 全局變量 (適配了 480x320 大分辨率)
// ==========================================
SemaphoreHandle_t xGuiMutex; 
enum GameState { STATE_IDLE, STATE_SHOWING, STATE_INPUT, STATE_GAMEOVER };
GameState currentState = STATE_IDLE;
std::vector<int> sequence; 
int inputIndex = 0;       
int currentScore = 0;

// 顏色定義
#define COLOR_BG      0x2104 
#define COLOR_BTN     0x4208 
#define COLOR_ACTIVE  0xFFE0 
#define COLOR_TEXT    0xFFFF 

// --- 針對 3.5寸大屏的佈局優化 ---
int startX = 160;   // 整體右移
int startY = 30;    // 整體下移
int btnWidth = 90;  // 按鈕變大！
int btnHeight = 80; // 按鈕變大！
int gap = 10;       

// ==========================================
// 4. UI 函數
// ==========================================
void drawButton(int id, uint16_t color) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        int row = id / 3;
        int col = id % 3;
        int x = startX + col * (btnWidth + gap);
        int y = startY + row * (btnHeight + gap);
        
        // 注意：這裡是 tft-> 而不是 tft.
        tft->fillRoundRect(x, y, btnWidth, btnHeight, 10, color);
        tft->setCursor(x + 35, y + 30);
        tft->setTextColor(0x0000); // 直接用 0x0000 代表黑色
        tft->setTextSize(4);      // 4號超大字體，老人家看得清
        tft->print(id + 1);
        xSemaphoreGive(xGuiMutex);
    }
}

void update_status(String line1, String line2, uint16_t color) {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillRect(0, 0, startX - 10, 320, COLOR_BG); // 清除左側
        
        tft->setTextColor(color);
        tft->setTextSize(3); // 3號大字體
        
        tft->setCursor(10, 100);
        tft->println(line1);
        tft->setCursor(10, 140);
        tft->println(line2);
        xSemaphoreGive(xGuiMutex);
    }
}

void build_ui() {
    if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
        tft->fillScreen(COLOR_BG);
        
        tft->setCursor(10, 30); 
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(3);
        tft->println("MEMORY");
        tft->setCursor(10, 60);
        tft->println("BLOOM");
        
        tft->drawFastVLine(startX - 15, 0, 320, 0x528A);
        xSemaphoreGive(xGuiMutex);
    }
    for(int i=0; i<9; i++) drawButton(i, COLOR_BTN);
    update_status("Press '5'", "to Start", 0x07E0);
}

// ==========================================
// 5. 遊戲邏輯任務
// ==========================================
void task_game(void *pv) {
    while(1) {
        switch (currentState) {
            case STATE_IDLE: {
                char key = keypad.getKey();
                if (key == '5') { 
                    currentScore = 0;
                    sequence.clear();
                    update_status("Watch", "Carefully", 0xFFE0);
                    vTaskDelay(1000);
                    currentState = STATE_SHOWING;
                }
                break;
            }
            case STATE_SHOWING: {
                sequence.push_back(random(0, 9));
                for (int step : sequence) {
                    vTaskDelay(500);
                    drawButton(step, COLOR_ACTIVE);
                    tone(PIN_BUZZER, 500 + (step * 100), 200);
                    vTaskDelay(300); 
                    drawButton(step, COLOR_BTN);
                }
                update_status("Repeat", "Sequence!", 0x07FF);
                inputIndex = 0;
                currentState = STATE_INPUT;
                break;
            }
            case STATE_INPUT: {
                char key = keypad.getKey();
                if (key) {
                    int userPress = key - '1'; 
                    if (userPress >= 0 && userPress <= 8) {
                        drawButton(userPress, COLOR_ACTIVE);
                        tone(PIN_BUZZER, 500 + (userPress * 100), 100);
                        vTaskDelay(150);
                        drawButton(userPress, COLOR_BTN);

                        if (userPress == sequence[inputIndex]) {
                            inputIndex++;
                            if (inputIndex >= sequence.size()) {
                                currentScore++;
                                update_status("Good!", "Score: " + String(currentScore), 0xFFFF);
                                vTaskDelay(800);
                                currentState = STATE_SHOWING; 
                            }
                        } else {
                            currentState = STATE_GAMEOVER;
                        }
                    }
                }
                break;
            }
            case STATE_GAMEOVER: {
                tone(PIN_BUZZER, 100, 1000);
                update_status("WRONG!", "Score: " + String(currentScore), 0xF800);
                vTaskDelay(2000);
                update_status("Press '5'", "Try Again", 0x07E0);
                currentState = STATE_IDLE;
                break;
            }
        }
        vTaskDelay(10);
    }
}

// ==========================================
// 6. Setup
// ==========================================
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUZZER, OUTPUT);

    // 初始化屏幕 (使用新庫)
    tft->begin();
    tft->fillScreen(COLOR_BG);
    
    xGuiMutex = xSemaphoreCreateMutex();
    build_ui();

    // 隨機數種子
    randomSeed(analogRead(34));

    xTaskCreatePinnedToCore(task_game, "Game", 4096, NULL, 1, NULL, 1);
}

void loop() { vTaskDelete(NULL); }