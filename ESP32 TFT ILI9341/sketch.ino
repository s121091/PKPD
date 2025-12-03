/*
  TFT LCD CONNECTION
  CS   : 5
  MISO : 19
  MOSI : 23
  SCK  : 18
  CD   : 17
  RST  : 16
*/
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_DC 17
#define TFT_CS 5
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

//color definition
#define BLACK 0x0000       ///<   0,   0,   0
#define NAVY 0x000F        ///<   0,   0, 123
#define DARKGREEN 0x03E0   ///<   0, 125,   0
#define DARKCYAN 0x03EF    ///<   0, 125, 123
#define MAROON 0x7800      ///< 123,   0,   0
#define PURPLE 0x780F      ///< 123,   0, 123
#define OLIVE 0x7BE0       ///< 123, 125,   0
#define LIGHTGREY 0xC618   ///< 198, 195, 198
#define DARKGREY 0x7BEF    ///< 123, 125, 123
#define BLUE 0x001F        ///<   0,   0, 255
#define GREEN 0x07E0       ///<   0, 255,   0
#define CYAN 0x07FF        ///<   0, 255, 255
#define RED 0xF800         ///< 255,   0,   0
#define MAGENTA 0xF81F     ///< 255,   0, 255
#define YELLOW 0xFFE0      ///< 255, 255,   0
#define WHITE 0xFFFF       ///< 255, 255, 255
#define ORANGE 0xFD20      ///< 255, 165,   0
#define GREENYELLOW 0xAFE5 ///< 173, 255,  41
#define PINK 0xFC18        ///< 255, 130, 198

//global variable
String text;
unsigned int i, j, w, h;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("ESP32 TFT ILI9341");
  //start tft
  tft.begin();

  //set rotation to landscape
  tft.setRotation(1);
  //create a box to the border of TFT
  tft.drawRect(0, 0, 320, 240, GREEN);

  //setting text parameter
  tft.setTextColor(WHITE); // set text color
  tft.setTextSize(3); // set text size
  tft.setCursor(0, 0); // set text cursor
  tft.print("Hollanda Academy"); //print the text

  //set the text to center
  tft.setTextColor(YELLOW); // set text color
  tft.setTextSize(3);
  text = "TFT LCD Example";
  getTextBounds(text, 3);
  tft.setCursor((320 - w) / 2, 50);
  tft.print(text);

  //create a fill rectangle
  tft.fillRect(0, 100, 320, 50, DARKCYAN);

  delay(1000);

  //erase all
  tft.fillScreen(BLACK);

  //make a border again
  tft.drawRect(0, 0, 320, 240, GREEN);

  //let make a circle in the center of TFT
  tft.drawCircle(320 / 2, 240 / 2, 100, PINK);
  delay(1000);
  //lets bold the circle with other color
  tft.drawCircle(320 / 2, 240 / 2, 101, GREEN);
  delay(1000);
  tft.drawCircle(320 / 2, 240/2, 102, BLUE);
  delay(1000);

  //lets fill the circle with different color
  tft.fillCircle(320 / 2, 240 / 2, 99, CYAN);
  delay(1000);

  //lets erase the circle without fillScreen
  tft.fillRect(10, 10, 300, 220, BLACK);

  //make a subscribe text in the center
  text = "SUBSRIBE";
  getTextBounds("SUBSRIBE", 4);
  tft.fillRoundRect(10, 240 / 2-h/2, 300, h, 4, RED);
  tft.setCursor((320-w)/2,(240-h)/2);
  tft.print(text);
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10); // this speeds up the simulation
}


//function to get text bound
void getTextBounds(String Str, byte sizes) {
  int widths = 6;
  int heights = 8;

  unsigned int jumlahKarakter = Str.length();
  w = (jumlahKarakter-2) * widths * sizes;
  h = heights * sizes;
}