#include <unordered_map>
#include <unordered_set>
#include <math.h>
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include "pin_config.h"
#include "PixelState.h"
#include "colorChangeRoutine.h"

/////////////////////////////////////////////////////
// You can adjust the following "subjective" params:
// static const int8_t PIXEL_WIDTH = 3;
// uint8_t percentInputFill = 20;
// uint8_t inputWidth = 6;
static const int32_t PIXEL_WIDTH = 8;
static const bool randomShapesAsPixels = true;
static const uint8_t pixelPadding = 1;
static const uint8_t percentInputFill = 15;
static const int32_t inputWidth = 5;
static const uint8_t gravity = 1;
static const bool changeFallenPixelColors = true;
static const unsigned long maxFps = 30;
static const unsigned long millisToChangeInputColor = 60;
static const unsigned long millisToChangeAllColors = 30;
// End "subjective" params.
/////////////////////////////////////////////////////

static const int32_t NATIVE_ROWS = LCD_HEIGHT;
static const int32_t NATIVE_COLS = LCD_WIDTH;
static const int32_t SCALED_ROWS = NATIVE_ROWS / PIXEL_WIDTH;
static const int32_t SCALED_COLS = NATIVE_COLS / PIXEL_WIDTH;

uint16_t BACKGROUND_COLOR = RGB565_BLACK;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// CO5300 RAM is 480x480; panel is 466x466 centered with col offset 6.
Arduino_CO5300 *panel = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

// Offscreen framebuffer; one bulk flush per frame instead of per-grain QSPI writes.
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
int16_t touchPointsX[5];
int16_t touchPointsY[5];

// 16-bit color representation (5-6-5):
//------------------------------------
//    Red    ⏐    Green    ⏐   Blue
// 1 1 1 1 1 ⏐ 1 1 1 1 1 0 ⏐ 0 0 0 0 0
//      = 31 ⏐        = 62 ⏐       = 0
//
// ((31 << 11) | (62 << 5) | 0) = 65472 = 0xffc0

uint8_t newRgbValues[3] = {0x1F, 0x00, 0x00}; // red, green, blue
uint8_t newKValue = 4;

unsigned long colorChangeTime = 0;
unsigned long allColorChangeTime = 0;

int32_t inputX = -1;
int32_t inputY = -1;

// pixelStates key is a concat of the 16 bit x/y values into a single 32 bit value for more efficient storage.
std::unordered_map<uint64_t, PointState> pixelStates;          // [X,Y]:[STATE_DATA]
std::unordered_map<uint64_t, PointState> landedPixelStates;    // [X,Y]:[STATE_DATA]
std::unordered_map<uint32_t, uint32_t> landedPixelsColumnTops; // [X]:[Y]  // For each column (X), what is the highest row (Y) where a pixel stopped.

static std::unordered_map<uint64_t, PointState> pixelStatesToAdd;
static std::unordered_set<uint64_t> pixelsToErase;

SemaphoreHandle_t xDisplayMutex = NULL;
SemaphoreHandle_t xStateMutex = NULL;
SemaphoreHandle_t xSemaphore1 = NULL;
SemaphoreHandle_t xSemaphore2 = NULL;
SemaphoreHandle_t xSemaphore3 = NULL;
SemaphoreHandle_t xSemaphore4 = NULL;

long lastMillis = 0;
int fps = 0;
char fpsStringBuffer[32];

bool withinNativeCols(int32_t value)
{
  return value >= 0 && value <= NATIVE_COLS - 1;
}

bool withinNativeRows(int32_t value)
{
  return value >= 0 && value <= NATIVE_ROWS - 1;
}

bool withinScaledCols(int32_t value)
{
  return value >= 0 && value <= SCALED_COLS - 1;
}

bool withinScaledRows(int32_t value)
{
  return value >= 0 && value <= SCALED_ROWS - 1;
}

void setColor(uint8_t *rgbValues, uint8_t _red, uint8_t _green, uint8_t _blue)
{
  rgbValues[0] = _red;
  rgbValues[1] = _green;
  rgbValues[2] = _blue;
}

uint16_t rgbValuesTo565(uint8_t *rgbValues)
{
  // rgbValues[0] is 0-31 red, rgbValues[1] is 0-63 green, rgbValues[2] is 0-31 blue.
  return ((uint16_t)(rgbValues[0] & 0x1F) << 11) |
         ((uint16_t)(rgbValues[1] & 0x3F) << 5) |
         (uint16_t)(rgbValues[2] & 0x1F);
}

Point getXYIndividualValues(uint64_t xy)
{
  uint32_t pixelXCol = (uint32_t)((xy & 0xFFFF0000) >> 16);
  uint32_t pixelYRow = (uint32_t)(xy & 0x0000FFFF);

  return Point(pixelXCol, pixelYRow);
}

uint64_t getXYCombinedValue(uint32_t x, uint32_t y)
{
  return ((uint64_t)x << 16) | (uint64_t)(y);
}

void clearScaledPixel(int32_t x, int32_t y)
{
  int32_t scaledXCol = x * PIXEL_WIDTH;
  int32_t scaledYRow = y * PIXEL_WIDTH;

  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY))
  {
    gfx->fillRect(scaledXCol, scaledYRow, PIXEL_WIDTH, PIXEL_WIDTH, BACKGROUND_COLOR);
    xSemaphoreGive(xDisplayMutex);
  }
}

uint8_t getRandomShape()
{
  return random(0, 8);
}

uint8_t getPixelShape()
{
  return randomShapesAsPixels ? getRandomShape() : 0;
}

// Convert scaled pixel to native pixel area and then draw it.
void drawScaledPixel(int32_t x, int32_t y, uint8_t *rgbValues, uint8_t shape)
{
  int32_t scaledXCol = x * PIXEL_WIDTH;
  int32_t scaledYRow = y * PIXEL_WIDTH;

  uint16_t color = rgbValuesTo565(rgbValues);

  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY))
  {
    // Prevent edges of shapes overlapping.
    auto pixelWidthAdjustment = max((int32_t)1, PIXEL_WIDTH - pixelPadding);

    if (shape == 0)
    {
      gfx->fillRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment, color);
    }
    else if (shape == 1)
    {
      gfx->drawRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment, color);
    }
    else if (shape == 2)
    {
      gfx->drawCircle(scaledXCol + (pixelWidthAdjustment / 2), scaledYRow + (pixelWidthAdjustment / 2), pixelWidthAdjustment / 2, color);
    }
    else if (shape == 3)
    {
      gfx->fillCircle(scaledXCol + (pixelWidthAdjustment / 2), scaledYRow + (pixelWidthAdjustment / 2), pixelWidthAdjustment / 2, color);
    }
    else if (shape == 4)
    {
      // Triangle outline "pointing" down.
      gfx->drawTriangle(scaledXCol, scaledYRow,
                        scaledXCol + pixelWidthAdjustment, scaledYRow,
                        scaledXCol + (pixelWidthAdjustment / 2),
                        scaledYRow + pixelWidthAdjustment, color);
    }
    else if (shape == 5)
    {
      // Filled triangle "pointing" down.
      gfx->fillTriangle(scaledXCol, scaledYRow,
                        scaledXCol + pixelWidthAdjustment, scaledYRow,
                        scaledXCol + (pixelWidthAdjustment / 2),
                        scaledYRow + pixelWidthAdjustment, color);
    }
    else if (shape == 6)
    {
      // Triangle outline "pointing" up.
      gfx->drawTriangle(scaledXCol, scaledYRow + pixelWidthAdjustment,
                        scaledXCol + pixelWidthAdjustment, scaledYRow + pixelWidthAdjustment,
                        scaledXCol + (pixelWidthAdjustment / 2),
                        scaledYRow, color);
    }
    else if (shape == 7)
    {
      // Filled triangle "pointing" up.
      gfx->fillTriangle(scaledXCol, scaledYRow + pixelWidthAdjustment,
                        scaledXCol + pixelWidthAdjustment, scaledYRow + pixelWidthAdjustment,
                        scaledXCol + (pixelWidthAdjustment / 2),
                        scaledYRow, color);
    }
    else
    {
      gfx->fillRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment, color);
    }

    xSemaphoreGive(xDisplayMutex);
  }
}

void setNextColorAll(std::unordered_map<uint64_t, PointState>::iterator landedPixelStatesBegin,
                     std::unordered_map<uint64_t, PointState>::iterator landedPixelStatesEnd,
                     std::unordered_map<uint64_t, PointState>::iterator pixelStatesBegin,
                     std::unordered_map<uint64_t, PointState>::iterator pixelStatesEnd)
{
  for (auto iter = landedPixelStatesBegin; iter != landedPixelStatesEnd; iter++)
  {
    setNextColor(iter->second.RgbValues, iter->second.ColorState);
    drawScaledPixel(iter->second.XCol, iter->second.YRow, iter->second.RgbValues, iter->second.Shape);
  }

  for (auto iter = pixelStatesBegin; iter != pixelStatesEnd; iter++)
  {
    setNextColor(iter->second.RgbValues, iter->second.ColorState);
    drawScaledPixel(iter->second.XCol, iter->second.YRow, iter->second.RgbValues, iter->second.Shape);
  }
}

bool isPixelSlotAvailable(uint64_t xy)
{
  auto point = getXYIndividualValues(xy);
  return withinScaledRows(point.YRow) && withinScaledCols(point.XCol) && pixelStates.find(xy) == pixelStates.end() && point.YRow < landedPixelsColumnTops[point.XCol];
}

void updateLandedPixelsColumnTops(uint64_t xyLanded)
{
  auto point = getXYIndividualValues(xyLanded);

  landedPixelsColumnTops[point.XCol] = std::min(landedPixelsColumnTops[point.XCol], point.YRow);
}

bool canPixelFall(uint64_t xyKey)
{
  uint32_t pixelYRow = (uint32_t)(xyKey & 0x0000FFFF);

  uint32_t pixelYRowNext = pixelYRow + 1;

  if (!withinScaledRows(pixelYRowNext))
    return false;

  uint32_t pixelXCol = (uint32_t)((xyKey & 0xFFFF0000) >> 16);
  uint32_t pixelXColMinus = pixelXCol - 1;
  uint32_t pixelXColPlus = pixelXCol + 1;

  return (withinScaledCols(pixelXColMinus) && landedPixelsColumnTops[pixelXColMinus] > pixelYRowNext) ||
         (withinScaledCols(pixelXCol) && landedPixelsColumnTops[pixelXCol] > pixelYRowNext) ||
         (withinScaledCols(pixelXColPlus) && landedPixelsColumnTops[pixelXColPlus] > pixelYRowNext);
}

void movePixels(std::unordered_map<uint64_t, PointState>::iterator _pixelStatesIteratorBegin, std::unordered_map<uint64_t, PointState>::iterator _pixelStatesIteratorEnd)
{
  for (auto iter = _pixelStatesIteratorBegin; iter != _pixelStatesIteratorEnd; iter++)
  {
    auto keyVal = *iter;
    auto pixelKey = keyVal.first;
    auto thisPixelState = keyVal.second;

    auto pixelPoint = getXYIndividualValues(pixelKey);
    uint32_t pixelXCol = pixelPoint.XCol;
    uint32_t pixelYRow = pixelPoint.YRow;

    auto pixelState = keyVal.second.State;
    if (pixelState == GRID_STATE_NEW)
    {
      pixelStates[pixelKey].State = GRID_STATE_FALLING;
      continue;
    }

    uint8_t *thisPixelRgbValues = thisPixelState.RgbValues;

    auto pixelColorState = thisPixelState.ColorState;
    auto pixelVelocity = thisPixelState.Velocity;
    auto pixelShape = thisPixelState.Shape;

    bool moved = false;

    uint32_t newMaxYRowPos = uint32_t(pixelYRow + pixelVelocity);
    for (int32_t yRowPos = newMaxYRowPos; yRowPos > pixelYRow; yRowPos--)
    {
      if (!withinScaledRows(yRowPos))
      {
        continue;
      }

      uint64_t belowXY = getXYCombinedValue(pixelXCol, yRowPos);

      int32_t direction = 1;
      if (random(100) < 50)
      {
        direction *= -1;
      }

      uint32_t belowXY_A = -1;
      uint32_t belowXY_A_XCol = pixelXCol + direction;
      uint32_t belowXY_B = -1;
      uint32_t belowXY_B_XCol = pixelXCol - direction;

      if (withinScaledCols(belowXY_A_XCol))
      {
        belowXY_A = getXYCombinedValue(belowXY_A_XCol, yRowPos);
      }
      if (withinScaledCols(belowXY_B_XCol))
      {
        belowXY_B = getXYCombinedValue(belowXY_B_XCol, yRowPos);
      }

      if (xSemaphoreTake(xStateMutex, portMAX_DELAY))
      {
        if (isPixelSlotAvailable(belowXY) && pixelStatesToAdd.find(belowXY) == pixelStatesToAdd.end())
        {
          pixelStatesToAdd[belowXY] = PointState(pixelXCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);
          drawScaledPixel(pixelXCol, yRowPos, thisPixelRgbValues, pixelShape);

          pixelsToErase.insert(pixelKey);
          pixelsToErase.erase(belowXY);

          moved = true;
          xSemaphoreGive(xStateMutex);
          break;
        }
        else if (isPixelSlotAvailable(belowXY_A) && pixelStatesToAdd.find(belowXY_A) == pixelStatesToAdd.end())
        {
          pixelStatesToAdd[belowXY_A] = PointState(belowXY_A_XCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY_A].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);
          drawScaledPixel(belowXY_A_XCol, yRowPos, thisPixelRgbValues, pixelShape);

          pixelsToErase.insert(pixelKey);
          pixelsToErase.erase(belowXY_A);

          moved = true;
          xSemaphoreGive(xStateMutex);
          break;
        }
        else if (isPixelSlotAvailable(belowXY_B) && pixelStatesToAdd.find(belowXY_B) == pixelStatesToAdd.end())
        {
          pixelStatesToAdd[belowXY_B] = PointState(belowXY_B_XCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY_B].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);
          drawScaledPixel(belowXY_B_XCol, yRowPos, thisPixelRgbValues, pixelShape);

          pixelsToErase.insert(pixelKey);
          pixelsToErase.erase(belowXY_B);

          moved = true;
          xSemaphoreGive(xStateMutex);
          break;
        }

        xSemaphoreGive(xStateMutex);
      }
    }

    if (!moved)
    {
      thisPixelState.Velocity += gravity;
    }

    if (xSemaphoreTake(xStateMutex, portMAX_DELAY))
    {
      if (!moved && !canPixelFall(pixelKey))
      {
        thisPixelState.State = GRID_STATE_LANDED;
        landedPixelStates[pixelKey] = thisPixelState;

        pixelsToErase.insert(pixelKey);
        updateLandedPixelsColumnTops(pixelKey);
      }

      xSemaphoreGive(xStateMutex);
    }
  }
}

// Task for moving the falling "pixels".
void task1(void *pvParameters)
{
  while (1)
  {
    if (xSemaphoreTake(xSemaphore1, portMAX_DELAY))
    {
      auto pixelStatesBegin = pixelStates.begin();
      auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));
      auto pixelStatesEnd = pixelStates.end();

      movePixels(pixelStatesMid, pixelStatesEnd);

      xSemaphoreGive(xSemaphore2);
    }
  }
}

// Task for changing all "pixel" colors.
void task2(void *pvParameters)
{
  while (1)
  {
    if (xSemaphoreTake(xSemaphore3, portMAX_DELAY))
    {
      auto landedPixelStatesBegin = landedPixelStates.begin();
      auto landedPixelStatesMid = std::next(landedPixelStatesBegin, (landedPixelStates.size() / 2));
      auto landedPixelStatesEnd = landedPixelStates.end();

      auto pixelStatesBegin = pixelStates.begin();
      auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));
      auto pixelStatesEnd = pixelStates.end();

      setNextColorAll(landedPixelStatesMid, landedPixelStatesEnd, pixelStatesMid, pixelStatesEnd);

      xSemaphoreGive(xSemaphore4);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(IIC_SDA, IIC_SCL);

  // TP_RST is shared with LCD_RESET on this board, so pulse before display init.
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(30);
  digitalWrite(TP_RST, HIGH);
  delay(50);
  delay(500);

  touch.setPins(TP_RST, TP_INT);
  if (!touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL))
  {
    Serial.println("touch is not online...");
  }
  else
  {
    Serial.print("Touch Model :");
    Serial.println(touch.getModelName());
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(true, false);
  }

  Serial.println("Init display...");
  if (!gfx->begin())
  {
    Serial.println("Canvas/panel init failed (framebuffer alloc likely OOM — needs PSRAM).");
  }
  panel->setBrightness(200);
  // MADCTL: orient panel to match touch (matches Waveshare official Arduino examples).
  bus->writeC8D8(0x36, 0xA0);
  gfx->fillScreen(BACKGROUND_COLOR);
  gfx->flush();

  colorChangeTime = millis() + 1000;

  for (int32_t xCol = 0; xCol < SCALED_COLS; xCol++)
    landedPixelsColumnTops[xCol] = SCALED_ROWS;

  xDisplayMutex = xSemaphoreCreateMutex();
  xStateMutex = xSemaphoreCreateMutex();
  xSemaphore1 = xSemaphoreCreateCounting(1, 0);
  xSemaphore2 = xSemaphoreCreateCounting(1, 0);
  xSemaphore3 = xSemaphoreCreateCounting(1, 0);
  xSemaphore4 = xSemaphoreCreateCounting(1, 0);

  xTaskCreatePinnedToCore(
      task1,
      "task1",
      4096,
      NULL,
      1,
      NULL,
      0);

  xTaskCreatePinnedToCore(
      task2,
      "task2",
      4096,
      NULL,
      1,
      NULL,
      0);

  delay(500);
}

bool readTouch(int32_t &outX, int32_t &outY)
{
  uint8_t touched = touch.getPoint(touchPointsX, touchPointsY, 1);
  if (touched == 0)
    return false;

  outX = touchPointsX[0];
  outY = touchPointsY[0];
  return true;
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
  sprintf(fpsStringBuffer, "fps:%4lu", (unsigned long)fps);

  // Display frame rate
  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY))
  {
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setCursor(0, 0);
    gfx->print(fpsStringBuffer);
    xSemaphoreGive(xDisplayMutex);
  }

  lastMillis = currentMillis;

  // Change the color of the fallen pixels over time
  if (changeFallenPixelColors && allColorChangeTime < millis())
  {
    allColorChangeTime = millis() + millisToChangeAllColors;

    auto landedPixelStatesBegin = landedPixelStates.begin();
    auto landedPixelStatesMid = std::next(landedPixelStatesBegin, (landedPixelStates.size() / 2));

    auto pixelStatesBegin = pixelStates.begin();
    auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));

    xSemaphoreGive(xSemaphore3);

    setNextColorAll(landedPixelStatesBegin, landedPixelStatesMid, pixelStatesBegin, pixelStatesMid);

    xSemaphoreTake(xSemaphore4, portMAX_DELAY);

    setNextColor(newRgbValues, newKValue);
  }

  // Change the color of the pixels over time
  if (colorChangeTime < millis())
  {
    colorChangeTime = millis() + millisToChangeInputColor;
    setNextColor(newRgbValues, newKValue);
  }

  // Handle touch.
  int32_t touchX, touchY;
  if (readTouch(touchX, touchY))
  {
    inputX = map(touchX, 0, NATIVE_COLS, 0, SCALED_COLS);
    inputY = map(touchY, 0, NATIVE_ROWS, 0, SCALED_ROWS);
  }
  else
  {
    inputX = -1;
    inputY = -1;
  }

  if (withinNativeCols(inputX) && withinNativeRows(inputY))
  {
    int32_t halfInputWidth = inputWidth / 2;
    for (int32_t i = -halfInputWidth; i <= halfInputWidth; ++i)
    {
      for (int32_t j = -halfInputWidth; j <= halfInputWidth; ++j)
      {
        if (random(100) < percentInputFill)
        {
          uint32_t xCol = inputX + i;
          uint32_t yRow = inputY + j;

          uint64_t xy = ((uint64_t)xCol << 16) | (uint64_t)yRow;

          if (withinNativeCols(xCol) && withinNativeRows(yRow) && pixelStates.find(xy) == pixelStates.end())
          {
            pixelStates[xy] = PointState(xCol, yRow, GRID_STATE_NEW, newKValue, 1, getPixelShape());
            setColor(pixelStates[xy].RgbValues, newRgbValues[0], newRgbValues[1], newRgbValues[2]);
            drawScaledPixel(xCol, yRow, pixelStates[xy].RgbValues, pixelStates[xy].Shape);
          }
        }
      }
    }
  }

  auto pixelStatesBegin = pixelStates.begin();
  auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));

  pixelStatesToAdd.clear();
  pixelsToErase.clear();

  xSemaphoreGive(xSemaphore1);

  movePixels(pixelStatesBegin, pixelStatesMid);

  xSemaphoreTake(xSemaphore2, portMAX_DELAY);

  for (const auto &key : pixelsToErase)
  {
    if (pixelStatesToAdd.find(key) != pixelStatesToAdd.end())
      continue;

    pixelStates.erase(key);
  }

  for (const auto &keyVal : pixelStatesToAdd)
  {
    pixelStates[keyVal.first] = PointState(keyVal.second.XCol, keyVal.second.YRow, keyVal.second.State, keyVal.second.ColorState, keyVal.second.Velocity, keyVal.second.Shape);
    setColor(pixelStates[keyVal.first].RgbValues, keyVal.second.RgbValues[0], keyVal.second.RgbValues[1], keyVal.second.RgbValues[2]);
  }

  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY))
  {
    gfx->flush();
    xSemaphoreGive(xDisplayMutex);
  }
}
