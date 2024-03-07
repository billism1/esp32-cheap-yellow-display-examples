#include <unordered_map>
#include <unordered_set>
#include <math.h>
#include <Arduino.h>
#include "lgfx_8048S043C.h"
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

int16_t BACKGROUND_COLOR = TFT_BLACK;

static LGFX display; // Instance of LGFX

// 16-bit color representation:
//------------------------------------
//    Red    ⏐    Green    ⏐   Blue
// 1 1 1 1 1 ⏐ 1 1 1 1 1 0 ⏐ 0 0 0 0 0
//      = 31 ⏐        = 62 ⏐       = 0
//
// ((31 << 11) | (62 << 5) | 0) = 65472 = 0xffc0

// uint8_t newRgbValues[3] = { 0x31, 0x00, 0x00 }; // red, green, blue
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

SemaphoreHandle_t xStateMutex = NULL;
SemaphoreHandle_t xSemaphore1 = NULL;
SemaphoreHandle_t xSemaphore2 = NULL;

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
  rgbValues[0] = _red;   /// * `rgbValues[0]` is the red value
  rgbValues[1] = _green; /// * `rgbValues[1]` is the green value
  rgbValues[2] = _blue;  /// * `rgbValues[2]` is the blue value
}

Point getXYIndividualValues(uint64_t xy)
{
  // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
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
  // Scale
  int32_t scaledXCol = x * PIXEL_WIDTH;
  int32_t scaledYRow = y * PIXEL_WIDTH;

  display.fillRect(scaledXCol, scaledYRow, PIXEL_WIDTH, PIXEL_WIDTH, BACKGROUND_COLOR);
}

uint8_t getRandomShape()
{
  return random(0, 7);
}

uint8_t getPixelShape()
{
  return randomShapesAsPixels ? getRandomShape() : 0;
}

// Convert scaled pixel to native pixel area and then draw it.
void drawScaledPixel(int32_t x, int32_t y, uint8_t *rgbValues, uint8_t shape)
{
  // Serial.printf("drawScaledPixel: x: %d, y: %d, shape: %d, rgbValues: %02hhX%02hhX%02hhX", x, y, shape, rgbValues[0], rgbValues[1], rgbValues[2]);
  // Serial.println();

  // Scale
  int32_t scaledXCol = x * PIXEL_WIDTH;
  int32_t scaledYRow = y * PIXEL_WIDTH;

  // long map(long x, long in_min, long in_max, long out_min, long out_max)
  uint8_t r = map(rgbValues[0], 0, 31, 0, 255);
  uint8_t g = map(rgbValues[1], 0, 63, 0, 255);
  uint8_t b = map(rgbValues[2], 0, 31, 0, 255);

  display.setColor(r, g, b);

  // Serial.printf("drawScaledPixel: color:  %02hhX %02hhX %02hhX", r, g, b);
  // Serial.println();

  // Prevent edges of shapres overlapping.
  auto pixelWidthAdjustment = max(1, PIXEL_WIDTH - pixelPadding);

  if (shape == 0)
  {
    // Filled (square) rectangle.
    display.fillRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment);
    // display.fillRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment, color);
  }
  else if (shape == 1)
  {
    // Rectangle (square) outline.
    display.drawRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment);
    // display.drawRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment, color);
  }
  else if (shape == 2)
  {
    // Circle outline.
    display.drawCircle(scaledXCol + (pixelWidthAdjustment / 2), scaledYRow + (pixelWidthAdjustment / 2), (pixelWidthAdjustment / 2));
  }
  else if (shape == 3)
  {
    // Filled circle.
    display.fillCircle(scaledXCol + (pixelWidthAdjustment / 2), scaledYRow + (pixelWidthAdjustment / 2), pixelWidthAdjustment / 2);
  }
  else if (shape == 4)
  {
    // Triangle outline "pointing" down.
    display.drawTriangle(scaledXCol, scaledYRow,
                         scaledXCol + pixelWidthAdjustment, scaledYRow,
                         scaledXCol + (pixelWidthAdjustment / 2),
                         scaledYRow + pixelWidthAdjustment);
  }
  else if (shape == 5)
  {
    // Triangle outline "pointing" up.
    display.drawTriangle(scaledXCol, scaledYRow + pixelWidthAdjustment,
                         scaledXCol + pixelWidthAdjustment, scaledYRow + pixelWidthAdjustment,
                         scaledXCol + (pixelWidthAdjustment / 2),
                         scaledYRow);
  }
  else if (shape == 6)
  {
    // Filled triangle "pointing" down.
    display.fillTriangle(scaledXCol, scaledYRow,
                         scaledXCol + pixelWidthAdjustment, scaledYRow,
                         scaledXCol + (pixelWidthAdjustment / 2), scaledYRow + pixelWidthAdjustment);
  }
  else if (shape == 7)
  {
    // Filled triangle "pointing" up.
    display.fillTriangle(scaledXCol, scaledYRow + pixelWidthAdjustment,
                         scaledXCol + pixelWidthAdjustment, scaledYRow + pixelWidthAdjustment,
                         scaledXCol + (pixelWidthAdjustment / 2), scaledYRow);
  }
  else
  {
    // Filled (square) rectangle.
    display.fillRect(scaledXCol, scaledYRow, pixelWidthAdjustment, pixelWidthAdjustment);
  }
}

void setNextColorAll()
{
  for (auto &keyValue : landedPixelStates)
  {
    setNextColor(keyValue.second.RgbValues, keyValue.second.ColorState);
    drawScaledPixel(keyValue.second.XCol, keyValue.second.YRow, keyValue.second.RgbValues, keyValue.second.Shape);
  }

  for (auto &keyValue : pixelStates)
  {
    setNextColor(keyValue.second.RgbValues, keyValue.second.ColorState);
    drawScaledPixel(keyValue.second.XCol, keyValue.second.YRow, keyValue.second.RgbValues, keyValue.second.Shape);
  }
}

bool isPixelSlotAvailable(uint64_t xy)
{
  auto point = getXYIndividualValues(xy);
  return withinScaledRows(point.YRow) && withinScaledCols(point.XCol) && pixelStates.find(xy) == pixelStates.end() && point.YRow < landedPixelsColumnTops[point.XCol];
}

void updateLandedPixelsColumnTops(uint64_t xyLanded)
{
  // landedPixelsColumnTops stores the highest row (Y) where a pixel stopped for each column (X).

  auto point = getXYIndividualValues(xyLanded);

  landedPixelsColumnTops[point.XCol] = std::min(landedPixelsColumnTops[point.XCol], point.YRow);
}

bool canPixelFall(uint64_t xyKey)
{
  // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
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
  // Iterate moving pixels and move them as needed.
  for (auto iter = _pixelStatesIteratorBegin; iter != _pixelStatesIteratorEnd; iter++)
  {
    auto keyVal = *iter;
    auto pixelKey = keyVal.first;
    auto thisPixelState = keyVal.second;

    // The 16 bit x/y values are stored as one 32 bit concatenation. Get the individual x/y values.
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
          //    This pixel will go straight down.
          pixelStatesToAdd[belowXY] = PointState(pixelXCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);                              // Out with the old.
          drawScaledPixel(pixelXCol, yRowPos, thisPixelRgbValues, pixelShape); // In with the new.

          pixelsToErase.insert(pixelKey);
          pixelsToErase.erase(belowXY);

          moved = true;
          xSemaphoreGive(xStateMutex);
          break;
        }
        else if (isPixelSlotAvailable(belowXY_A) && pixelStatesToAdd.find(belowXY_A) == pixelStatesToAdd.end())
        {
          //  This pixel will fall to side A (right)
          pixelStatesToAdd[belowXY_A] = PointState(belowXY_A_XCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY_A].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);                                   // Out with the old.
          drawScaledPixel(belowXY_A_XCol, yRowPos, thisPixelRgbValues, pixelShape); // In with the new.

          pixelsToErase.insert(pixelKey);
          pixelsToErase.erase(belowXY_A);

          moved = true;
          xSemaphoreGive(xStateMutex);
          break;
        }
        else if (isPixelSlotAvailable(belowXY_B) && pixelStatesToAdd.find(belowXY_B) == pixelStatesToAdd.end())
        {
          //  This pixel will fall to side B (left)
          pixelStatesToAdd[belowXY_B] = PointState(belowXY_B_XCol, yRowPos, GRID_STATE_FALLING, pixelColorState, pixelVelocity + gravity, pixelShape);
          setColor(&(pixelStatesToAdd[belowXY_B].RgbValues[0]), thisPixelRgbValues[0], thisPixelRgbValues[1], thisPixelRgbValues[2]);

          clearScaledPixel(pixelXCol, pixelYRow);                                   // Out with the old.
          drawScaledPixel(belowXY_B_XCol, yRowPos, thisPixelRgbValues, pixelShape); // In with the new.

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

void task1(void *pvParameters)
{
  auto coreId = xPortGetCoreID();

  while (1)
  {
    if (xSemaphoreTake(xSemaphore1, portMAX_DELAY))
    {
      auto pixelStatesBegin = pixelStates.begin();
      auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));
      auto pixelStatesEnd = pixelStates.end();

      movePixels(pixelStatesMid, pixelStatesEnd);

      xSemaphoreGive(xSemaphore2); // release the mutex
    }
  }
}

void setup()
{
  // Serial.begin(115200);

  Serial.println("Init display...");
  display.init();

  if (display.touch())
  {
    if (display.width() < display.height())
      display.setRotation(display.getRotation() ^ 1);

    // display.setTextDatum(textdatum_t::middle_center);
    // display.drawString("touch the arrow marker.", display.width()>>1, display.height() >> 1);
    // display.setTextDatum(textdatum_t::top_left);

    // std::uint16_t fg = TFT_WHITE;
    // std::uint16_t bg = TFT_BLACK;
    // if (display.isEPD()) std::swap(fg, bg);
    // uint16_t calibrationData[8];
    // display.calibrateTouch(calibrationData, fg, bg, std::max(display.width(), display.height()) >> 3);

    // for (short i=0;i<8;i++) {
    //   Serial.print('Calibration data: ');
    //   Serial.print(calibrationData[i]);
    //   Serial.print(', ');
    // }
  }

  colorChangeTime = millis() + 1000;

  // Set the tallest pixel column to be 1 slot bellow the "bottom" (0,0 coordinate being top left.)
  for (int32_t xCol = 0; xCol < SCALED_COLS; xCol++)
    landedPixelsColumnTops[xCol] = SCALED_ROWS;

  auto viewportWidth = display.width();
  auto viewportHeight = display.height();

  // Serial.printf("Viewport dimensions (W x H): %i x %i\n", viewportWidth, viewportHeight);

  xStateMutex = xSemaphoreCreateMutex();
  xSemaphore1 = xSemaphoreCreateCounting(1, 0);
  xSemaphore2 = xSemaphoreCreateCounting(1, 0);

  xTaskCreatePinnedToCore(
      task1,   // Function to implement the task
      "task1", // Name of the task
      4096,    // Stack size in words
      NULL,    // Task input parameter
      1,       // Priority of the task
      NULL,    // Task handle.
      0        // Core where the task should run
  );

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

  // Serial.println("Looping...");

  // Get frame rate.
  fps = 1000 / max(currentMillis - lastMillis, 1UL);
  sprintf(fpsStringBuffer, "fps:%4lu", fps);

  // Display frame rate
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(fpsStringBuffer, 0, 0);

  // uint32_t psramSize = ESP.getPsramSize();
  // uint32_t freePsram = ESP.getFreePsram();
  // sprintf(fpsStringBuffer, "psram: %6u / %6u", freePsram, psramSize);
  // display.drawString(fpsStringBuffer, 0, 10);

  // uint32_t heapSize = ESP.getHeapSize();
  // uint32_t freeHeap = ESP.getFreeHeap();
  // sprintf(fpsStringBuffer, "heap: %6u / %6u", freeHeap, heapSize);
  // display.drawString(fpsStringBuffer, 0, 20);

  // uint32_t minFreeHeap = ESP.getMinFreeHeap();
  // sprintf(fpsStringBuffer, "min free heap: %6u", minFreeHeap);
  // display.drawString(fpsStringBuffer, 0, 30);

  lastMillis = currentMillis;

  // Change the color of the fallen pixels over time
  if (changeFallenPixelColors && allColorChangeTime < millis())
  {
    allColorChangeTime = millis() + millisToChangeAllColors;
    setNextColorAll();
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
  if (display.getTouch(&touchX, &touchY))
  {
    // Scale
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
    // Randomly add an area of pixels
    int32_t halfInputWidth = inputWidth / 2;
    for (int32_t i = -halfInputWidth; i <= halfInputWidth; ++i)
    {
      for (int32_t j = -halfInputWidth; j <= halfInputWidth; ++j)
      {
        if (random(100) < percentInputFill)
        {
          uint32_t xCol = inputX + i;
          uint32_t yRow = inputY + j;

          // Concat the 16 bit x/y values into a single 32 bit value for more efficient storage.
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

  // Split up the work.

  auto pixelStatesBegin = pixelStates.begin();
  auto pixelStatesMid = std::next(pixelStatesBegin, (pixelStates.size() / 2));

  pixelStatesToAdd.clear();
  pixelsToErase.clear();

  // Start task and proceed.
  xSemaphoreGive(xSemaphore1);

  movePixels(pixelStatesBegin, pixelStatesMid);

  // Wait for task to complete.
  xSemaphoreTake(xSemaphore2, portMAX_DELAY);

  for (const auto &key : pixelsToErase)
  {
    if (pixelStatesToAdd.find(key) != pixelStatesToAdd.end())
      continue; // Do not erase pixel if it is being back-filled.

    pixelStates.erase(key);
  }

  for (const auto &keyVal : pixelStatesToAdd)
  {
    pixelStates[keyVal.first] = PointState(keyVal.second.XCol, keyVal.second.YRow, keyVal.second.State, keyVal.second.ColorState, keyVal.second.Velocity, keyVal.second.Shape);
    setColor(pixelStates[keyVal.first].RgbValues, keyVal.second.RgbValues[0], keyVal.second.RgbValues[1], keyVal.second.RgbValues[2]);
  }
}
