/*
 * 3x3 Matrix Keypad Test
 * 用于验证 9 个按钮的接线是否正确
 */
#include <Keypad.h>

const byte ROWS = 3; // 3行
const byte COLS = 3; // 3列

// 定义键盘布局
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'}
};

// 这里填你刚才接的引脚
// 行: 32, 33, 27
byte rowPins[ROWS] = {32, 33, 27}; 
// 列: 14, 12, 13
byte colPins[COLS] = {14, 12, 13}; 

// 初始化键盘库
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

void setup(){
  Serial.begin(115200);
  Serial.println("Keypad Test Start!");
  Serial.println("Please press any button...");
}
  
void loop(){
  // 监听按键
  char key = keypad.getKey();
  
  if (key){
    Serial.print("Key Pressed: ");
    Serial.println(key);
  }
}