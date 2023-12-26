#include <M5Stack.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <FastLED.h>

// ハードウェア関連
const int DATA_PIN = 21;
const int NUM_LEDS = 37;

// LED点灯データ配列
CRGB leds[NUM_LEDS];

// 切り替え表示関連
int numOfMap;
CRGB **ledsArray;
int ledsIndex = 0; 
long rotateInterval;

// 明るさ関連
const uint8_t BRIGHTNESS_INIT = 5;
// ライブラリの明るさの最大値は255だが明るすぎるので制限
const uint8_t BRIGHTNESS_MAX = 100; 
const uint8_t BRIGHTNESS_STEP = 20;
uint8_t brighitness = BRIGHTNESS_INIT;

// 回転処理関連
const int NUM_ROWS_LEDS = 7;
const int COLS_IN_ROW_LEDS[NUM_ROWS_LEDS] = {4, 5, 6, 7, 6, 5, 4};
const int START_INDEX_ROW_LEDS[NUM_ROWS_LEDS + 1] = {0, 4, 9, 15, 22, 28, 33, INT_MAX};
const int OFFSET_ROW[NUM_ROWS_LEDS] = {0, 0, 0, 0, 1, 2, 3};
long changeInterval;

// 回転モード用の列挙
enum rotateModeEnum {
  none,
  clockwise,
  counterClockwise
};
enum rotateModeEnum rotateMode = none;

// 回転処理用にLEDの座標を表す構造体
struct RowCol {
  int row;
  int col;
};

// 虹色関連
const long RAINBOW_INTERVAL = 20;
boolean rainbowFlag = false;
boolean **rainbowTargetArray;
uint8_t gHue = 0;

void setup() {
  // M5Stack初期化処理
  M5.begin();
  Serial.begin(115200);  
  M5.Lcd.setTextFont(2);   
  M5.Speaker.begin(); 
  M5.Speaker.setVolume(1);

  // microSDカードのファイルオープン
  File settingDataFile = SD.open("/settingData.json");
  if (!settingDataFile) {
    M5.Lcd.println("Failed to open file in microSD.");
    return;
  }
  M5.Lcd.println("Reading settingData.json...");

  // microSDカードのファイルから設定を読み出す
  readSetting(settingDataFile);

  // FastLEDの初期化（マイコンの種類、データピン番号、RGBの並び、データ配列、LED数）
  FastLED.addLeds<WS2811, DATA_PIN, GRB>(leds, NUM_LEDS);
  // 色補正の指定
  FastLED.setCorrection(TypicalLEDStrip);
  // 明るさの初期設定
  FastLED.setBrightness(brighitness);

  // 最初のLEDデータ設定(LED点灯データ配列に設定データ最初のマップをコピー)
  memcpy(leds, ledsArray[ledsIndex], sizeof(ledsArray[ledsIndex][0]) * NUM_LEDS);
  FastLED.show();

  M5.Lcd.println("Push button C to change brighitness.");
}

void loop() {
  boolean changed = false;
  M5.update();


  EVERY_N_MILLISECONDS(changeInterval) {
    // 点灯データ切り替え
    // FastLEDのプリプロセッサマクロEVERY_N_MILLISECONDSにより一定時間ごとに実行される
    ledsIndex++;
    // 一周した場合は最初のマップに戻る
    ledsIndex %= numOfMap; 

    memcpy(leds, ledsArray[ledsIndex], sizeof(ledsArray[ledsIndex][0]) * NUM_LEDS);

    FastLED.show();
    changed = true;
  } 

  EVERY_N_MILLISECONDS(rotateInterval) {
    // 回転処理
    if (!changed) {
      if (rotateMode == clockwise) {
        rotateClockwiseLeds();
        FastLED.show();
      } else if (rotateMode == counterClockwise) {
        rotateCounterClockwiseLeds();
        FastLED.show();
      }
    }
  }

  if (rainbowFlag) {
    // 虹色点灯処理
    fillRainbow(leds, rainbowTargetArray[ledsIndex], NUM_LEDS, gHue, 7);
    FastLED.show();
    EVERY_N_MILLISECONDS(RAINBOW_INTERVAL) {
      // 時間でHue（色相）を変化することで点灯色を変化させる
      gHue++;
    }
  }

  // ボタンCが押された場合に明るさ切り替え
  if (M5.BtnC.wasReleased()) {
    if ((int)brighitness + BRIGHTNESS_STEP > BRIGHTNESS_MAX) {
      // 明るさ最大値を超えた場合は初期値に戻す
      brighitness = BRIGHTNESS_INIT;
    } else {
      brighitness += BRIGHTNESS_STEP;
    }
    FastLED.setBrightness(brighitness);
    FastLED.show();

    // 変更後の明るさの値をディスプレイに表示
    M5.Lcd.setCursor(0, M5.Lcd.fontHeight() * 3 + 1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("brighitness:%u  \n", brighitness);

    // ビープ音を鳴らす
    // 次のloop関数実行時のM5.update()にて音が止まる
    M5.Speaker.beep();
  }
}

// 設定の読み出し
void readSetting(File settingDataFile) {
  // ファイル内容を読み出す
  size_t size = settingDataFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  settingDataFile.readBytes(buf.get(), size);

  // ファイル内容のJSONをデシリアライズ化
  DynamicJsonDocument settingData(size * 2);
  DeserializationError error = deserializeJson(settingData, buf.get());
  if (error) {
    M5.Lcd.println("Failed to parse JSON.");
    M5.Lcd.println(error.c_str());
    return;
  }

  // 設定変数への代入
  numOfMap = settingData["numOfMap"];
  changeInterval = settingData["changeInterval"];
  String lightLotate = settingData["lightLotate"];
  rotateInterval = settingData["rotateInterval"];
  JsonArray dataArray = settingData["dataArray"];

  if (lightLotate.equals("clockwiseLightLotate")) {
    rotateMode = clockwise;
  } else if (lightLotate.equals("counterClockwiseLightLotate")){
    rotateMode = counterClockwise;
  } else {
    rotateMode = none;
  }

  // LEDデータの二次元配列作成
  // LEDデータ枚数分の配列一次元目領域確保
  ledsArray = new CRGB*[numOfMap];

  // マップ一枚ごとにLEDデータの設定
  int i = 0;
  for (JsonVariant data : dataArray) {
    JsonArray colorArray = data.as<JsonArray>();
    // 配列二次元目目領域確保
    ledsArray[i] = new CRGB[NUM_LEDS];

    int j = 0;
    for (JsonVariant colorV : colorArray) {
      // 色名の文字列から色値に変換して設定
      String colorName = colorV.as<String>();
      ledsArray[i][j] = colorFromName(colorName);
      j++;
    }
    i++;
  }

  // 虹色指定の二次元配列作成
  i = 0;
  rainbowTargetArray = new boolean*[numOfMap];
  for (JsonVariant data : dataArray) {
    JsonArray colorArray = data.as<JsonArray>();
    rainbowTargetArray[i] = new boolean[NUM_LEDS];

    int j = 0;
    // 色名が"Rainbow"になっている箇所のみtrue値の二次元配列を作成
    for (JsonVariant colorV : colorArray) {
      String colorName = colorV.as<String>();
      if (colorName.equals("Rainbow")) {
        rainbowTargetArray[i][j] = true;
        rainbowFlag = true;
      } else {
        rainbowTargetArray[i][j] = false;
      }
      j++;
    }
    i++;
  }
}

// 時計回りにデータを回転
void rotateClockwiseLeds() {
  CRGB newLeds[NUM_LEDS];
  for (int i = 0; i < NUM_LEDS; i++) {
    RowCol fromRowcol = convertIndexToRowCol(i);
    int toRow = fromRowcol.col + OFFSET_ROW[fromRowcol.row];
    int toCol = COLS_IN_ROW_LEDS[toRow] - fromRowcol.row - 1 + OFFSET_ROW[toRow];
    RowCol toRowcol = {toRow, toCol};
    int toIndex = convertRowColToIndex(toRowcol);
    newLeds[toIndex] = leds[i];
  }

  memcpy(leds, newLeds, sizeof(newLeds));
}

// 反時計回りにデータを回転
void rotateCounterClockwiseLeds() {
  CRGB newLeds[NUM_LEDS];
  for (int i = 0; i < NUM_LEDS; i++) {
    RowCol fromRowcol = convertIndexToRowCol(i);
    int toRow = COLS_IN_ROW_LEDS[fromRowcol.row] - fromRowcol.col - 1 + OFFSET_ROW[fromRowcol.row];
    int toCol = fromRowcol.row - OFFSET_ROW[toRow];
    RowCol toRowcol = {toRow, toCol};
    int toIndex = convertRowColToIndex(toRowcol);
    newLeds[toIndex] = leds[i];
  }

  memcpy(leds, newLeds, sizeof(newLeds));
}

// LEDデータのインデックスから行,列の座標に変換
RowCol convertIndexToRowCol(int index) {
  int row;
  for (row = 0; row < NUM_ROWS_LEDS; row++) {
    if (index < START_INDEX_ROW_LEDS[row + 1]) {
      break;
    }
  }
  return {row, index - START_INDEX_ROW_LEDS[row]};
}

// LEDデータの行,列の座標からインデックスに変換
int convertRowColToIndex(RowCol rowcol) {
  return START_INDEX_ROW_LEDS[rowcol.row] + rowcol.col;
}

// 色名の文字列から色値に変換
CRGB colorFromName(String name) {
  static const char* const colorNames[] = {
    "Black", "White", "Red", "Green", "Blue",
    "Yellow", "Orange", "Pink", "Purple", "Gray"
  };

  static const CRGB colors[] = {
    CRGB::Black, CRGB::White, CRGB::Red, CRGB::Green, CRGB::Blue,
    CRGB::Yellow, CRGB::Orange, CRGB::Pink, CRGB::Purple, CRGB::Gray
  };

  for (int i = 0; i < sizeof(colors) / sizeof(CRGB); i++) {
    if (name.equals(colorNames[i])) {
      return colors[i];
    }
  }

  return CRGB::Black;
}

// 虹色のデータ設定
// fastLEDのfill_rainbow関数を参考に作成
void fillRainbow(CRGB *targetArray,
                  boolean *rainbowTarget,
                  int numToFill,
                  uint8_t initialhue,
                  uint8_t deltahue)
{
  // HSVによる色指定でHue（色相）を変化させることで虹色を表現
  CHSV hsv;
  hsv.hue = initialhue;
  hsv.val = 255;
  hsv.sat = 240;
  for(int i = 0; i < numToFill; i++) {
    if (rainbowTarget[i]) {
      targetArray[i] = hsv;
      hsv.hue += deltahue;
    }
  }
}
