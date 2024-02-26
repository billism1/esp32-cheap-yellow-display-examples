#include <Arduino.h>

static const uint8_t GRID_STATE_NEW = 1;
static const uint8_t GRID_STATE_FALLING = 2;

struct PointState
{
  uint16_t XCol;
  uint16_t YRow;
  uint16_t State;
  uint16_t Color;
  uint8_t Velocity;

  PointState() {}

  PointState(uint16_t x, uint16_t y, uint16_t state, uint16_t color, uint8_t velocity)
  {
    XCol = x;
    YRow = y;
    State = state;
    Color = color;
    Velocity = velocity;
  }
};

struct Point
{
  uint16_t XCol;
  uint16_t YRow;

  Point() {}

  Point(uint16_t x, uint16_t y)
  {
    XCol = x;
    YRow = y;
  }
};
