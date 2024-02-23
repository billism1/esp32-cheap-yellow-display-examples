#include <map>
#include <unordered_map>
#include <unordered_set>
#include <math.h>
#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include "pin_config.h"

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

/////////////////////////////////////////////////////
// You can adjust the following "subjective" params:
static const int8_t PIXEL_WIDTH = 3;
uint8_t percentInputFill = 20;
uint8_t inputWidth = 6;
uint8_t gravity = 1;
unsigned long maxFps = 30;
unsigned long colorChangeTime = 0;
// End "subjective" params.
/////////////////////////////////////////////////////

static const int16_t NATIVE_ROWS = 240;
static const int16_t NATIVE_COLS = 320;
static const int16_t SCALED_ROWS = NATIVE_ROWS / PIXEL_WIDTH;
static const int16_t SCALED_COLS = NATIVE_COLS / PIXEL_WIDTH;

int16_t BACKGROUND_COLOR = TFT_BLACK;

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

byte red = 31;
byte green = 0;
byte blue = 0;
byte colorState = 0;
uint16_t color = red << 11;

int16_t inputX = -1;
int16_t inputY = -1;

std::unordered_map<uint32_t, uint16_t> pixelColors;    // [X,Y]:[COLOR]
std::unordered_map<uint32_t, uint8_t> pixelStates;     // [X,Y]:[STATE]
std::unordered_map<uint32_t, uint8_t> pixelVelocities; // [X,Y]:[VELOCITY]
std::unordered_set<uint32_t> landedPixels;             // [X,Y]

static const uint8_t GRID_STATE_NEW = 1;
static const uint8_t GRID_STATE_FALLING = 2;

long lastMillis = 0;
int fps = 0;
char fpsStringBuffer[32];

bool withinNativeCols(int16_t value)
{
  return value >= 0 && value <= NATIVE_COLS - 1;
}

bool withinNativeRows(int16_t value)
{
  return value >= 0 && value <= NATIVE_ROWS - 1;
}

bool withinScaledCols(int16_t value)
{
  return value >= 0 && value <= SCALED_COLS - 1;
}

bool withinScaledRows(int16_t value)
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

// Scale pixel and then draw it.
void drawScaledPixel(int16_t x, int16_t y, uint16_t color)
{
  // Scale
  int16_t scaledXCol = x * PIXEL_WIDTH;
  int16_t scaledYRow = y * PIXEL_WIDTH;

  for (uint16_t i = 0; i < PIXEL_WIDTH; i++)
  {
    for (uint16_t j = 0; j < PIXEL_WIDTH; j++)
    {
      tft.drawPixel(scaledXCol + i, scaledYRow + j, color);
    }
  }
}

void setup()
{
  // Start the SPI for the touch screen and init the TS library
  Serial.println("Init display...");
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Start the tft display and set it to black
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

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

  // Get frame rate.
  fps = 1000 / max(currentMillis - lastMillis, (unsigned long)1);
  sprintf(fpsStringBuffer, "fps: %lu", fps);

  // Display frame rate
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  //tft.fillRect(0, 0, 65, 10, BACKGROUND_COLOR);
  tft.drawString(fpsStringBuffer, 0, 0);

  memset(fpsStringBuffer, 0x00, sizeof(fpsStringBuffer));
  uint32_t psramSize = ESP.getPsramSize();
  uint32_t freePsram = ESP.getFreePsram();
  sprintf(fpsStringBuffer, "psram: %u / %u", freePsram, psramSize);
  tft.drawString(fpsStringBuffer, 0, 10);

  uint32_t heapSize = ESP.getHeapSize();
  uint32_t freeHeap = ESP.getFreeHeap();
  sprintf(fpsStringBuffer, "heap: %u / %u", freeHeap, heapSize);
  tft.drawString(fpsStringBuffer, 0, 20);

  uint32_t minFreeHeap = ESP.getMinFreeHeap();
  sprintf(fpsStringBuffer, "min free heap: %u", minFreeHeap);
  tft.drawString(fpsStringBuffer, 0, 30);

  lastMillis = currentMillis;

  // Handle touch.
  if (ts.tirqTouched() && ts.touched())
  {
    TS_Point p = ts.getPoint();

    inputX = map(p.x, TS_MINX, TS_MAXX, 0, SCALED_COLS);
    inputY = map(p.y, TS_MINY, TS_MAXY, 0, SCALED_ROWS);
  }
  else
  {
    inputX = -1;
    inputY = -1;
  }

  if (withinNativeCols(inputX) && withinNativeRows(inputY))
  {
    // Randomly add an area of pixels
    int16_t halfInputWidth = inputWidth / 2;
    for (int16_t i = -halfInputWidth; i <= halfInputWidth; ++i)
    {
      for (int16_t j = -halfInputWidth; j <= halfInputWidth; ++j)
      {
        if (random(100) < percentInputFill)
        {
          uint16_t xCol = inputX + i;
          uint16_t yRow = inputY + j;

          uint32_t xy = ((uint32_t)xCol << 16) | (uint32_t)yRow;
          if (withinNativeCols(xCol) && withinNativeRows(yRow) && pixelStates.find(xy) == pixelStates.end())
          {
            pixelColors[xy] = color;
            pixelStates[xy] = GRID_STATE_NEW;
            pixelVelocities[xy] = (uint8_t)1;

            drawScaledPixel(xCol, yRow, color);
          }
        }
      }
    }
  }

  // Change the color of the pixels over time
  if (colorChangeTime < millis())
  {
    colorChangeTime = millis() + 250;
    setNextColor();
  }

  std::unordered_set<uint32_t> pixelsToErase;
  std::unordered_map<uint32_t, uint16_t> pixelColorsToAdd;    // [X,Y]:[COLOR]
  std::unordered_map<uint32_t, uint8_t> pixelStatesToAdd;     // [X,Y]:[STATE]
  std::unordered_map<uint32_t, uint8_t> pixelVelocitiesToAdd; // [X,Y]:[VELOCITY]

  // Iterate moving pixels and move them as needed.
  std::unordered_map<uint32_t, uint8_t>::iterator stateIter = pixelStates.begin();
  while (stateIter != pixelStates.end())
  {
    auto keyVal = *stateIter;
    auto pixelKey = keyVal.first;
    uint16_t pixelXCol = (uint16_t)((pixelKey & 0xFFFF0000) >> 16);
    uint16_t pixelYRow = (uint16_t)(pixelKey & 0x0000FFFF);

    auto pixelState = keyVal.second;
    if (pixelState == GRID_STATE_NEW)
    {
      pixelStates[pixelKey] = GRID_STATE_FALLING;
      continue;
    }

    auto pixelColor = pixelColors[pixelKey];
    auto pixelVelocity = pixelVelocities[pixelKey];

    bool moved = false;

    uint16_t newMaxYRowPos = uint16_t(pixelYRow + pixelVelocity);
    for (int16_t yRowPos = newMaxYRowPos; yRowPos > pixelYRow; yRowPos--)
    {
      if (!withinScaledRows(yRowPos))
      {
        continue;
      }

      uint32_t belowXY = ((uint32_t)pixelXCol << 16) | (uint32_t)yRowPos;

      int16_t direction = 1;
      if (random(100) < 50)
      {
        direction *= -1;
      }

      uint32_t belowXY_A = -1;
      uint32_t belowXY_B = -1;

      if (withinScaledCols(pixelXCol + direction))
      {
        // belowStateA = stateGrid[y][j + direction];
        belowXY_A = ((uint32_t)(pixelXCol + direction) << 16) | (uint32_t)yRowPos;
      }
      if (withinScaledCols(pixelXCol - direction))
      {
        // belowStateB = stateGrid[y][j - direction];
        belowXY_B = ((uint32_t)(pixelXCol - direction) << 16) | (uint32_t)yRowPos;
      }

      if (withinScaledRows(yRowPos) &&
          pixelStates.find(belowXY) == pixelStates.end() && landedPixels.find(belowXY) == landedPixels.end())
      {
        //  This pixel will go straight down.
        pixelColorsToAdd[belowXY] = pixelColor;
        pixelVelocitiesToAdd[belowXY] = pixelVelocity + gravity;
        pixelStatesToAdd[belowXY] = GRID_STATE_FALLING;

        pixelsToErase.insert(pixelKey);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR);
        drawScaledPixel(pixelXCol, yRowPos, pixelColor);

        moved = true;
        break;
      }
      else if (withinScaledRows(yRowPos) &&
               pixelStates.find(belowXY_A) == pixelStates.end() && landedPixels.find(belowXY_A) == landedPixels.end())
      {
        //  This pixel will fall to side A (right)
        pixelColorsToAdd[belowXY_A] = pixelColor;
        pixelVelocitiesToAdd[belowXY_A] = pixelVelocity + gravity;
        pixelStatesToAdd[belowXY_A] = GRID_STATE_FALLING;

        pixelsToErase.insert(pixelKey);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR);
        drawScaledPixel(pixelXCol + direction, yRowPos, pixelColor);

        moved = true;
        break;
      }
      else if (withinScaledRows(yRowPos) &&
               pixelStates.find(belowXY_B) == pixelStates.end() && landedPixels.find(belowXY_B) == landedPixels.end())
      {
        //  This pixel will fall to side B (left)
        pixelColorsToAdd[belowXY_B] = pixelColor;
        pixelVelocitiesToAdd[belowXY_B] = pixelVelocity + gravity;
        pixelStatesToAdd[belowXY_B] = GRID_STATE_FALLING;

        pixelsToErase.insert(pixelKey);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR);
        drawScaledPixel(pixelXCol - direction, yRowPos, pixelColor);

        moved = true;
        break;
      }
    }

    if (!moved && pixelVelocity <= 2)
    {
      // Give new pixels a chance to fall.
      pixelVelocities[pixelKey] += gravity;
    }
    else if (!moved)
    {
      pixelsToErase.insert(pixelKey);
      landedPixels.insert(pixelKey);
    }

    stateIter++;
  }

  for (const auto &key : pixelsToErase)
  {
    pixelColors.erase(key);
    pixelVelocities.erase(key);
    pixelStates.erase(key);
  }

  for (const auto &keyVal : pixelColorsToAdd)
  {
    pixelColors[keyVal.first] = keyVal.second;
  }

  for (const auto &keyVal : pixelStatesToAdd)
  {
    pixelStates[keyVal.first] = keyVal.second;
  }

  for (const auto &keyVal : pixelVelocitiesToAdd)
  {
    pixelVelocities[keyVal.first] = keyVal.second;
  }
}
