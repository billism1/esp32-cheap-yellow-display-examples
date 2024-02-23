#include <math.h>
#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// The following min/max axis values were gathered through empirical data gathering on the specific board I have.
#define TS_MINX 280
#define TS_MAXX 3750
#define TS_MINY 280
#define TS_MAXY 3750

// Params you can easily play with
unsigned long colorChangeIntervalMs = 250;
uint8_t percentInputFill = 15;
uint8_t inputWidth = 8;
int8_t drawPixelWidth = 2;

static const int16_t NATIVE_ROWS = 240;
static const int16_t NATIVE_COLS = 320;

static const int16_t SCALED_ROWS = NATIVE_ROWS / drawPixelWidth;
static const int16_t SCALED_COLS = NATIVE_COLS / drawPixelWidth;

int16_t BACKGROUND_COLOR = TFT_BLACK;

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

byte red = 31;
byte green = 0;
byte blue = 0;
byte colorState = 0;
uint16_t color = red << 11;
unsigned long colorChangeTime = 0;

int16_t inputX = -1;
int16_t inputY = -1;

bool withinScaledCols(uint16_t value)
{
  return value >= 0 && value <= SCALED_COLS - 1;
}

bool withinScaledRows(uint16_t value)
{
  return value >= 0 && value <= SCALED_ROWS - 1;
}

// Color changing state machine
void setNextColor()
{
  switch (colorState)
  {
  case 0:
    green += 2;
    if (green == 64)
    {
      green = 63;
      colorState = 1;
    }
    break;
  case 1:
    red--;
    if (red == 255)
    {
      red = 0;
      colorState = 2;
    }
    break;
  case 2:
    blue++;
    if (blue == 32)
    {
      blue = 31;
      colorState = 3;
    }
    break;
  case 3:
    green -= 2;
    if (green == 255)
    {
      green = 0;
      colorState = 4;
    }
    break;
  case 4:
    red++;
    if (red == 32)
    {
      red = 31;
      colorState = 5;
    }
    break;
  case 5:
    blue--;
    if (blue == 255)
    {
      blue = 0;
      colorState = 0;
    }
    break;
  }

  color = red << 11 | green << 5 | blue;

  if (color == BACKGROUND_COLOR)
    color++;
}

void drawScaledPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (!withinScaledCols(x) || !withinScaledRows(y))
    return;

  uint16_t scaledXCol = x * drawPixelWidth;
  uint16_t scaledYRow = y * drawPixelWidth;

  // TODO: The below is hard-coded for a drawPixelWidth of 4. Make dynaminc.
  tft.drawPixel(scaledXCol, scaledYRow, color);
  tft.drawPixel(scaledXCol, scaledYRow + 1, color);
  tft.drawPixel(scaledXCol, scaledYRow + 2, color);
  tft.drawPixel(scaledXCol, scaledYRow + 3, color);
  tft.drawPixel(scaledXCol + 1, scaledYRow, color);
  tft.drawPixel(scaledXCol + 1, scaledYRow + 1, color);
  tft.drawPixel(scaledXCol + 1, scaledYRow + 2, color);
  tft.drawPixel(scaledXCol + 1, scaledYRow + 3, color);
  tft.drawPixel(scaledXCol + 2, scaledYRow, color);
  tft.drawPixel(scaledXCol + 2, scaledYRow + 1, color);
  tft.drawPixel(scaledXCol + 2, scaledYRow + 2, color);
  tft.drawPixel(scaledXCol + 2, scaledYRow + 3, color);
  tft.drawPixel(scaledXCol + 3, scaledYRow, color);
  tft.drawPixel(scaledXCol + 3, scaledYRow + 1, color);
  tft.drawPixel(scaledXCol + 3, scaledYRow + 2, color);
  tft.drawPixel(scaledXCol + 3, scaledYRow + 3, color);
}

// Maximum frames per second.
unsigned long maxFps = 30;
long lastMillis = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println("Hello, starting...");

  // Start the SPI for the touch screen and init the TS library
  Serial.println("Init display...");
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Start the tft display and set it to black
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BACKGROUND_COLOR);

  colorChangeTime = millis() + 1000;

  delay(500);
}

void loop()
{
  unsigned long currentMillis = millis();

  // Throttle FPS
  unsigned long diffMillis = currentMillis - lastMillis;
  if ((1000 / maxFps) > diffMillis)
  {
    return;
  }

  lastMillis = currentMillis;

  // Handle touch.
  if (ts.tirqTouched() && ts.touched())
  {
    TS_Point p = ts.getPoint();

    inputX = map(p.x, TS_MINX, TS_MAXX, 0, SCALED_COLS);
    inputY = map(p.y, TS_MINY, TS_MAXY, 0, SCALED_ROWS); // Needs flipping this axis for some reason.
    // tft.drawCircle(inputY, inputX, 6, TFT_SKYBLUE);
  }
  else
  {
    inputX = -1;
    inputY = -1;
  }

  if (!withinScaledCols(inputX) || !withinScaledRows(inputY))
    return;

  // Randomly add an area of pixels, centered on the input point.
  int16_t halfInputWidth = inputWidth / 2;
  for (int16_t i = -halfInputWidth; i <= halfInputWidth; ++i)
  {
    for (int16_t j = -halfInputWidth; j <= halfInputWidth; ++j)
    {
      if (random(100) < percentInputFill)
      {
        uint16_t xCol = inputX + i;
        uint16_t yRow = inputY + j;

        drawScaledPixel(xCol, yRow, color);
      }
    }
  }

  // Change the color of the pixels over time
  if (colorChangeTime < millis())
  {
    colorChangeTime = millis() + colorChangeIntervalMs;
    setNextColor();
  }
}
