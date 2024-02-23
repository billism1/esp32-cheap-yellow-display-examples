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

static const int8_t PIXEL_WIDTH = 4;
static const int16_t ROWS = 240 / PIXEL_WIDTH;
static const int16_t COLS = 320 / PIXEL_WIDTH;

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

uint8_t gravity = 1;
uint8_t percentInputFill = 10;
uint8_t adjacentVelocityResetValue = 3;

uint8_t inputWidth = 5;
uint16_t inputX = COLS / 2;
uint16_t inputY = 10;

std::unordered_map<uint32_t, uint16_t> pixelColor;   // [X,Y]:[COLOR]
std::unordered_map<uint32_t, uint8_t> pixelState;    // [X,Y]:[STATE]
std::unordered_map<uint32_t, uint8_t> pixelVelocity; // [X,Y]:[VELOCITY]

// static const int8_t GRID_STATE_NONE = 0;
static const uint8_t GRID_STATE_NEW = 1;
static const uint8_t GRID_STATE_FALLING = 2;
static const uint8_t GRID_STATE_COMPLETE = 3;

// Maximum frames per second.
unsigned long maxFps = 30;
long lastMillis = 0;
int fps = 0;
char fpsStringBuffer[16];

bool withinCols(uint16_t value)
{
  return value >= 0 && value <= COLS - 1;
}

bool withinRows(uint16_t value)
{
  return value >= 0 && value <= ROWS - 1;
}

// bool isWithinInputArea(int16_t x, int16_t y)
// {
//   long halfInputWidthPlusThree = (inputWidth / 2) + 3;
//   return x > (inputX - halfInputWidthPlusThree) && x < (inputX + halfInputWidthPlusThree) && y > (inputY - halfInputWidthPlusThree) && y < (inputY + halfInputWidthPlusThree);
// }

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

// void resetAdjacentPixels(int16_t x, int16_t y)
// {
//   int16_t xPlus = x + 1;
//   int16_t xMinus = x - 1;
//   int16_t yPlus = y + 1;
//   int16_t yMinus = y - 1;

//   // Row above
//   if (withinRows(yMinus))
//   {
//     if (nextStateGrid[yMinus][xMinus] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yMinus][xMinus] = GRID_STATE_FALLING;
//       nextVelocityGrid[yMinus][xMinus] = adjacentVelocityResetValue;
//     }
//     if (nextStateGrid[yMinus][x] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yMinus][x] = GRID_STATE_FALLING;
//       nextVelocityGrid[yMinus][x] = adjacentVelocityResetValue;
//     }
//     if (nextStateGrid[yMinus][xPlus] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yMinus][xPlus] = GRID_STATE_FALLING;
//       nextVelocityGrid[yMinus][xPlus] = adjacentVelocityResetValue;
//     }
//   }

//   // Current row
//   if (nextStateGrid[y][xMinus] == GRID_STATE_COMPLETE)
//   {
//     nextStateGrid[y][xMinus] = GRID_STATE_FALLING;
//     nextVelocityGrid[y][xMinus] = adjacentVelocityResetValue;
//   }
//   if (nextStateGrid[y][xPlus] == GRID_STATE_COMPLETE)
//   {
//     nextStateGrid[y][xPlus] = GRID_STATE_FALLING;
//     nextVelocityGrid[y][xPlus] = adjacentVelocityResetValue;
//   }

//   // Row below
//   if (withinRows(yPlus))
//   {
//     if (nextStateGrid[yPlus][xMinus] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yPlus][xMinus] = GRID_STATE_FALLING;
//       nextVelocityGrid[yPlus][xMinus] = adjacentVelocityResetValue;
//     }
//     if (nextStateGrid[yPlus][x] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yPlus][x] = GRID_STATE_FALLING;
//       nextVelocityGrid[yPlus][x] = adjacentVelocityResetValue;
//     }
//     if (nextStateGrid[yPlus][xPlus] == GRID_STATE_COMPLETE)
//     {
//       nextStateGrid[yPlus][xPlus] = GRID_STATE_FALLING;
//       nextVelocityGrid[yPlus][xPlus] = adjacentVelocityResetValue;
//     }
//   }
// }

void setup()
{
  Serial.begin(115200);
  Serial.println("Hello, starting...");

  Serial.println("Reserving pixel state memory...");
  pixelColor.reserve(COLS * ROWS);

  // // Serial.println("Creating grids...");
  // grid = new int16_t *[ROWS];
  // nextGrid = new int16_t *[ROWS];
  // lastGrid = new int16_t *[ROWS];

  // velocityGrid = new int16_t *[ROWS];
  // nextVelocityGrid = new int16_t *[ROWS];
  // lastVelocityGrid = new int16_t *[ROWS];

  // stateGrid = new int16_t *[ROWS];
  // nextStateGrid = new int16_t *[ROWS];
  // lastStateGrid = new int16_t *[ROWS];

  // // Serial.println("Clearing grids...");
  // for (int16_t i = 0; i < ROWS; ++i)
  // {
  //   grid[i] = new int16_t[COLS];
  //   nextGrid[i] = new int16_t[COLS];
  //   lastGrid[i] = new int16_t[COLS];

  //   velocityGrid[i] = new int16_t[COLS];
  //   nextVelocityGrid[i] = new int16_t[COLS];
  //   lastVelocityGrid[i] = new int16_t[COLS];

  //   stateGrid[i] = new int16_t[COLS];
  //   nextStateGrid[i] = new int16_t[COLS];
  //   lastStateGrid[i] = new int16_t[COLS];

  //   for (int16_t j = 0; j < COLS; ++j)
  //   {
  //     grid[i][j] = BACKGROUND_COLOR;
  //     nextGrid[i][j] = BACKGROUND_COLOR;
  //     lastGrid[i][j] = BACKGROUND_COLOR;

  //     velocityGrid[i][j] = 0;
  //     nextVelocityGrid[i][j] = 0;
  //     lastVelocityGrid[i][j] = 0;

  //     stateGrid[i][j] = GRID_STATE_NONE;
  //     nextStateGrid[i][j] = GRID_STATE_NONE;
  //     lastStateGrid[i][j] = GRID_STATE_NONE;
  //   }
  // }

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

  delay(2000);
}

void loop()
{
  Serial.println("Looping...");

  // delay(50);

  unsigned long currentMillis = millis();

  // Get frame rate.
  fps = 1000 / max(currentMillis - lastMillis, (unsigned long)1);
  sprintf(fpsStringBuffer, "fps: %lu", fps);

  // Display frame rate
  tft.fillRect(1, 1, 75, 10, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(fpsStringBuffer, 1, 1);

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

    inputX = map(p.x, TS_MINX, TS_MAXX, 0, COLS);
    inputY = map(p.y, TS_MINY, TS_MAXY, 0, ROWS); // Needs flipping this axis for some reason.

    //tft.drawCircle(inputY, inputX, 6, TFT_SKYBLUE);
  }

  // Randomly add an area of pixels
  Serial.println("Adding pixels...");
  int16_t halfInputWidth = inputWidth / 2;
  for (int16_t i = -halfInputWidth; i <= halfInputWidth; ++i)
  {
    for (int16_t j = -halfInputWidth; j <= halfInputWidth; ++j)
    {
      if (random(100) < percentInputFill)
      {
        uint16_t col = inputX + i;
        uint16_t row = inputY + j;

        uint32_t xy = ((uint32_t)col << 16) | (uint32_t)row;
        // Serial.print("col: ");
        // Serial.println(col);
        // Serial.print("row: ");
        // Serial.println(row);
        // Serial.print("xy: ");
        // Serial.println(xy);

        if (withinCols(col) && withinRows(row) && pixelState.find(xy) == pixelState.end())
        {
          pixelColor[xy] = color;
          pixelState[xy] = GRID_STATE_NEW;
          pixelVelocity[xy] = (uint8_t)1;
        }
      }
    }
  }

  // Change the color of the pixels over time
  if (colorChangeTime < millis())
  {
    //Serial.println("Changing color...");
    colorChangeTime = millis() + 750;
    setNextColor();
  }

  // DEBUG
  // DEBUG
  // Serial.println("Array Values:");
  // for (int16_t i = 0; i < ROWS; ++i)
  // {
  //   for (int16_t j = 0; j < COLS; ++j)
  //   {
  //     Serial.printf("|%u|", stateGrid[i][j]);
  //   }
  //   Serial.println("");
  // }
  // DEBUG
  // DEBUG

  // unsigned long beforeDraw = millis();
  // unsigned long pixelsDrawn = 0;
  // Draw the pixels
  // Serial.println("Drawing pixels...");
  // Serial.print(pixelColor.size());
  // Serial.println(" pixels to draw...");
  //for (std::unordered_map<uint32_t, uint16_t>::iterator iter = pixelColor.begin(); iter != pixelColor.end(); iter++)
  for (const auto& keyVal: pixelColor)
  {
    uint16_t xCol = (uint16_t)((keyVal.first & 0xFFFF0000) >> 16);
    uint16_t yRow = (uint16_t)(keyVal.first & 0x0000FFFF);
    uint16_t color = keyVal.second;

    // Serial.print("Drawing pixel: ");
    // Serial.print(xCol);
    // Serial.print(", ");
    // Serial.print(yRow);
    // Serial.print(": ");
    // Serial.println(color);

    uint16_t scaledXCol = xCol * PIXEL_WIDTH;
    uint16_t scaledYRow = yRow * PIXEL_WIDTH;

    // TODO: The below is hard-coded for a PIXEL_WIDTH of 4. Make dynaminc.
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

  // for (int16_t i = 0; i < ROWS; i = ++i)
  // {
  //   for (int16_t j = 0; j < COLS; j = ++j)
  //   {
  //     if (lastGrid == NULL || lastGrid[i][j] != grid[i][j])
  //     {
  //       tft.drawPixel(j, i, grid[i][j]);
  //       // pixelsDrawn++;
  //     }
  //   }
  // }
  // unsigned long afterDraw = millis();
  // unsigned long drawTime = afterDraw - beforeDraw;
  // Serial.printf("%lu pixels drawn in %lu ms.\r\n", pixelsDrawn, drawTime);

  /*

  // Serial.println("Clearing next grids...");
  // Clear the grids for the next frame of animation
  for (int16_t i = 0; i < ROWS; ++i)
  {
    for (int16_t j = 0; j < COLS; ++j)
    {
      nextGrid[i][j] = BACKGROUND_COLOR;
      nextVelocityGrid[i][j] = 0;
      nextStateGrid[i][j] = GRID_STATE_NONE;
    }
  }

  // Serial.println("Checking every cell...");
  // unsigned long beforeCheckEveryCell = millis();
  // unsigned long pixelsChecked = 0;
  // Check every pixel to see which need moving, and move them.
  for (int16_t i = 0; i < ROWS; ++i)
  {
    for (int16_t j = 0; j < COLS; ++j)
    {
      // This nexted loop is where the bulk of the computations occur.
      // Tread lightly in here, and check as few pixels as needed.

      // Get the state of the current pixel.
      int16_t pixelColor = grid[i][j];
      int16_t pixelVelocity = velocityGrid[i][j];
      int16_t pixelState = stateGrid[i][j];

      bool moved = false;

      // If the current pixel has landed, no need to keep checking for its next move.
      if (pixelState != GRID_STATE_NONE && pixelState != GRID_STATE_COMPLETE)
      {
        // pixelsChecked++;

        int16_t newPos = int16_t(i + pixelVelocity);
        for (int16_t y = newPos; y > i; y--)
        {
          if (!withinRows(y))
          {
            continue;
          }

          int16_t belowState = stateGrid[y][j];
          int16_t belowNextState = nextStateGrid[y][j];

          int16_t direction = 1;
          if (random(100) < 50)
          {
            direction *= -1;
          }

          int16_t belowStateA = -1;
          int16_t belowNextStateA = -1;
          int16_t belowStateB = -1;
          int16_t belowNextStateB = -1;

          if (withinCols(j + direction))
          {
            belowStateA = stateGrid[y][j + direction];
            belowNextStateA = nextStateGrid[y][j + direction];
          }
          if (withinCols(j - direction))
          {
            belowStateB = stateGrid[y][j - direction];
            belowNextStateB = nextStateGrid[y][j - direction];
          }

          if (belowState == GRID_STATE_NONE && belowNextState == GRID_STATE_NONE)
          {
            // This pixel will go straight down.
            nextGrid[y][j] = pixelColor;
            nextVelocityGrid[y][j] = pixelVelocity + gravity;
            nextStateGrid[y][j] = GRID_STATE_FALLING;
            moved = true;
            break;
          }
          else if (belowStateA == GRID_STATE_NONE && belowNextStateA == GRID_STATE_NONE)
          {
            // This pixel will fall to side A (right)
            nextGrid[y][j + direction] = pixelColor;
            nextVelocityGrid[y][j + direction] = pixelVelocity + gravity;
            nextStateGrid[y][j + direction] = GRID_STATE_FALLING;
            moved = true;
            break;
          }
          else if (belowStateB == GRID_STATE_NONE && belowNextStateB == GRID_STATE_NONE)
          {
            // This pixel will fall to side B (left)
            nextGrid[y][j - direction] = pixelColor;
            nextVelocityGrid[y][j - direction] = pixelVelocity + gravity;
            nextStateGrid[y][j - direction] = GRID_STATE_FALLING;
            moved = true;
            break;
          }
        }
      }

      if (moved)
        resetAdjacentPixels(j, i);

      if (pixelState != GRID_STATE_NONE && !moved)
      {
        nextGrid[i][j] = grid[i][j];
        nextVelocityGrid[i][j] = velocityGrid[i][j] + gravity;
        if (pixelState == GRID_STATE_NEW)
          nextStateGrid[i][j] = GRID_STATE_FALLING;
        else if (pixelState == GRID_STATE_FALLING && pixelVelocity > 2)
          nextStateGrid[i][j] = GRID_STATE_COMPLETE;
        else
          nextStateGrid[i][j] = pixelState; // should be GRID_STATE_COMPLETE
      }
    }
  }
  // unsigned long afterCheckEveryCell = millis();
  // unsigned long checkCellTime = afterCheckEveryCell - beforeCheckEveryCell;
  // Serial.printf("%lu pixels checked in %lu ms.\r\n", pixelsChecked, checkCellTime);

  // Swap the grid pointers.

  lastGrid = grid;
  lastVelocityGrid = velocityGrid;
  lastStateGrid = stateGrid;

  grid = nextGrid;
  velocityGrid = nextVelocityGrid;
  stateGrid = nextStateGrid;

  nextGrid = lastGrid;
  nextVelocityGrid = lastVelocityGrid;
  nextStateGrid = lastStateGrid;

  */
}
