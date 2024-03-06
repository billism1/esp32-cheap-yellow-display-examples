#include <Arduino.h>

static const uint8_t GRID_STATE_NEW = 1;
static const uint8_t GRID_STATE_FALLING = 2;
static const uint8_t GRID_STATE_LANDED = 3;

struct PointState
{
  uint32_t XCol;
  uint32_t YRow;
  uint8_t State;
  uint8_t ColorState;
  uint8_t Velocity;
  uint8_t Shape;

  uint8_t RgbValues[3];

  PointState() {}

  PointState(uint32_t x, uint32_t y, uint8_t state, uint8_t colorState, uint8_t velocity, uint8_t shape)
  {
    XCol = x;
    YRow = y;
    State = state;
    ColorState = colorState;
    Velocity = velocity;
    Shape = shape;
  }
};

struct Point
{
  uint32_t XCol;
  uint32_t YRow;

  Point() {}

  Point(uint32_t x, uint32_t y)
  {
    XCol = x;
    YRow = y;
  }
};
