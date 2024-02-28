#include <unordered_map>
#include <unordered_set>
#include <math.h>
#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include "PixelState.h"

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
// static const int8_t PIXEL_WIDTH = 3;
// uint8_t percentInputFill = 20;
// uint8_t inputWidth = 6;
static const int8_t PIXEL_WIDTH = 2;
static const uint8_t percentInputFill = 10;
static const uint8_t inputWidth = 10;
static const uint8_t gravity = 1;
static const unsigned long maxFps = 30;
static const unsigned long colorChangeFrequencyMs = 250;
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

// 16-bit color representation:
//------------------------------------
//    Red    ⏐    Green    ⏐   Blue
// 1 1 1 1 1 ⏐ 1 1 1 1 1 0 ⏐ 0 0 0 0 0
//      = 31 ⏐        = 62 ⏐       = 0
//
// ((31 << 11) | (62 << 5) | 0) = 65472 = 0xffc0

byte red = 31;
byte green = 0;
byte blue = 0;
byte colorState = 0;
uint16_t color = red << 11;

unsigned long colorChangeTime = 0;

int16_t inputX = -1;
int16_t inputY = -1;

// pixelStates key is a concat of the 16 bit x/y values into a single 32 bit value for more efficient storage.
std::unordered_map<uint32_t, PointState> pixelStates;               // [X,Y]:[STATE_DATA]
std::unordered_map<uint16_t, uint16_t> landedPixelsColumnTops; // [X]:[Y]  // For each column (X), what is the highest row (Y) where a pixel stopped.

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

Point getXYIndividualValues(uint32_t xy)
{
  // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
  uint16_t pixelXCol = (uint16_t)((xy & 0xFFFF0000) >> 16);
  uint16_t pixelYRow = (uint16_t)(xy & 0x0000FFFF);

  return Point(pixelXCol, pixelYRow);
}

uint32_t getXYCombinedValue(uint16_t x, uint16_t y)
{
  return ((uint32_t)x << 16) | (uint32_t)(y);
}

// Convert scaled pixel to native pixel area and then draw it.
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

bool isPixelSlotAvailable(uint32_t xy)
{
  auto point = getXYIndividualValues(xy);
  return withinScaledRows(point.YRow) && withinScaledCols(point.XCol) && pixelStates.find(xy) == pixelStates.end() && point.YRow < landedPixelsColumnTops[point.XCol];
}

void updateLandedPixelsColumnTops(uint32_t xyLanded)
{
  // landedPixelsColumnTops stores the highest row (Y) where a pixel stopped for each column (X).

  auto point = getXYIndividualValues(xyLanded);

  landedPixelsColumnTops[point.XCol] = std::min(landedPixelsColumnTops[point.XCol], point.YRow);
}

bool canPixelFall(uint32_t xyKey)
{
  // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
  uint16_t pixelYRow = (uint16_t)(xyKey & 0x0000FFFF);

  uint16_t pixelYRowNext = pixelYRow + 1;

  if (!withinScaledRows(pixelYRowNext))
    return false;

  uint16_t pixelXCol = (uint16_t)((xyKey & 0xFFFF0000) >> 16);
  uint16_t pixelXColMinus = pixelXCol - 1;
  uint16_t pixelXColPlus = pixelXCol + 1;

  return (withinScaledCols(pixelXColMinus) && landedPixelsColumnTops[pixelXColMinus] > pixelYRowNext) ||
         (withinScaledCols(pixelXCol) && landedPixelsColumnTops[pixelXCol] > pixelYRowNext) ||
         (withinScaledCols(pixelXColPlus) && landedPixelsColumnTops[pixelXColPlus] > pixelYRowNext);
}

void setup()
{
  // Serial.begin(115200);

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

  // Set the tallest pixel column to be 1 slot bellow the "bottom" (0,0 coordinate being top left.)
  for (int16_t xCol = 0; xCol < SCALED_COLS; xCol++)
    landedPixelsColumnTops[xCol] = SCALED_ROWS;

  auto viewportWidth = tft.getViewportWidth();
  auto viewportHeight = tft.getViewportHeight();

  Serial.printf("Viewport dimensions (W x H): %i x %i\n", viewportWidth, viewportHeight);

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
  fps = 1000 / max(currentMillis - lastMillis, 1UL);
  sprintf(fpsStringBuffer, "fps:%4lu", fps);

  // Display frame rate
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(fpsStringBuffer, 0, 0);

  // uint32_t psramSize = ESP.getPsramSize();
  // uint32_t freePsram = ESP.getFreePsram();
  // sprintf(fpsStringBuffer, "psram: %6u / %6u", freePsram, psramSize);
  // tft.drawString(fpsStringBuffer, 0, 10);

  // uint32_t heapSize = ESP.getHeapSize();
  // uint32_t freeHeap = ESP.getFreeHeap();
  // sprintf(fpsStringBuffer, "heap: %6u / %6u", freeHeap, heapSize);
  // tft.drawString(fpsStringBuffer, 0, 20);

  // uint32_t minFreeHeap = ESP.getMinFreeHeap();
  // sprintf(fpsStringBuffer, "min free heap: %6u", minFreeHeap);
  // tft.drawString(fpsStringBuffer, 0, 30);

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

          // Concat the 16 bit x/y values into a single 32 bit value for more efficient storage.
          uint32_t xy = ((uint32_t)xCol << 16) | (uint32_t)yRow;

          if (withinNativeCols(xCol) && withinNativeRows(yRow) && pixelStates.find(xy) == pixelStates.end())
          {
            pixelStates[xy].Color = color;
            pixelStates[xy].State = GRID_STATE_NEW;
            pixelStates[xy].Velocity = (uint8_t)1;

            drawScaledPixel(xCol, yRow, color);
          }
        }
      }
    }
  }

  // Change the color of the pixels over time
  if (colorChangeTime < millis())
  {
    colorChangeTime = millis() + colorChangeFrequencyMs;
    setNextColor();
  }

  std::unordered_set<uint32_t> pixelsToErase;           // [X,Y]
  std::unordered_map<uint32_t, PointState> pixelStatesToAdd; // [X,Y]:[STATE_DATA]

  // Iterate moving pixels and move them as needed.
  for (const auto &keyVal : pixelStates)
  {
    auto pixelKey = keyVal.first;

    // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
    auto pixelPoint = getXYIndividualValues(pixelKey);
    uint16_t pixelXCol = pixelPoint.XCol;
    uint16_t pixelYRow = pixelPoint.YRow;

    auto pixelState = keyVal.second.State;
    if (pixelState == GRID_STATE_NEW)
    {
      pixelStates[pixelKey].State = GRID_STATE_FALLING;
      continue;
    }

    auto pixelColor = pixelStates[pixelKey].Color;
    auto pixelVelocity = pixelStates[pixelKey].Velocity;

    bool moved = false;

    uint16_t newMaxYRowPos = uint16_t(pixelYRow + pixelVelocity);
    for (int16_t yRowPos = newMaxYRowPos; yRowPos > pixelYRow; yRowPos--)
    {
      if (!withinScaledRows(yRowPos))
      {
        continue;
      }

      uint32_t belowXY = getXYCombinedValue(pixelXCol, yRowPos);

      int16_t direction = 1;
      if (random(100) < 50)
      {
        direction *= -1;
      }

      uint32_t belowXY_A = -1;
      uint16_t belowXY_A_XCol = pixelXCol + direction;
      uint32_t belowXY_B = -1;
      uint16_t belowXY_B_XCol = pixelXCol - direction;

      if (withinScaledCols(belowXY_A_XCol))
      {
        belowXY_A = getXYCombinedValue(belowXY_A_XCol, yRowPos);
      }
      if (withinScaledCols(belowXY_B_XCol))
      {
        belowXY_B = getXYCombinedValue(belowXY_B_XCol, yRowPos);
      }

      if (isPixelSlotAvailable(belowXY) && pixelStatesToAdd.find(belowXY) == pixelStatesToAdd.end())
      {
        //  This pixel will go straight down.
        pixelStatesToAdd[belowXY] = PointState(pixelXCol, yRowPos, GRID_STATE_FALLING, pixelColor, pixelVelocity + gravity);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR); // Out with the old.
        drawScaledPixel(pixelXCol, yRowPos, pixelColor);         // In with the new.

        pixelsToErase.insert(pixelKey);
        pixelsToErase.erase(belowXY);

        moved = true;
        break;
      }
      else if (isPixelSlotAvailable(belowXY_A) && pixelStatesToAdd.find(belowXY_A) == pixelStatesToAdd.end())
      {
        //  This pixel will fall to side A (right)
        pixelStatesToAdd[belowXY_A] = PointState(belowXY_A_XCol, yRowPos, GRID_STATE_FALLING, pixelColor, pixelVelocity + gravity);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR); // Out with the old.
        drawScaledPixel(belowXY_A_XCol, yRowPos, pixelColor);    // In with the new.

        pixelsToErase.insert(pixelKey);
        pixelsToErase.erase(belowXY_A);

        moved = true;
        break;
      }
      else if (isPixelSlotAvailable(belowXY_B) && pixelStatesToAdd.find(belowXY_B) == pixelStatesToAdd.end())
      {
        //  This pixel will fall to side B (left)
        pixelStatesToAdd[belowXY_B] = PointState(belowXY_B_XCol, yRowPos, GRID_STATE_FALLING, pixelColor, pixelVelocity + gravity);

        drawScaledPixel(pixelXCol, pixelYRow, BACKGROUND_COLOR); // Out with the old.
        drawScaledPixel(belowXY_B_XCol, yRowPos, pixelColor);    // In with the new.

        pixelsToErase.insert(pixelKey);
        pixelsToErase.erase(belowXY_B);

        moved = true;
        break;
      }
    }

    if (!moved)
    {
      // Give new pixels a chance to fall.
      pixelStates[pixelKey].Velocity += gravity;
    }

    if (!moved && !canPixelFall(pixelKey))
    {
      pixelsToErase.insert(pixelKey);
      updateLandedPixelsColumnTops(pixelKey);
    }
  }

  for (const auto &key : pixelsToErase)
  {
    if (pixelStatesToAdd.find(key) != pixelStatesToAdd.end())
      continue; // Do not erase pixel if it is being back-filled.

    pixelStates.erase(key);
  }

  for (const auto &keyVal : pixelStatesToAdd)
  {
    pixelStates[keyVal.first] = PointState(keyVal.second.XCol, keyVal.second.YRow, keyVal.second.State, keyVal.second.Color, keyVal.second.Velocity);
  }
}
