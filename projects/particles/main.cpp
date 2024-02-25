#include <Arduino.h>
#include <math.h>
#include <SPI.h>
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

// Maximum frames per second.
unsigned long maxFps = 30;

char fpsStringBuffer[16];
unsigned long lastMillis = 0;
int fps = 0;

static const uint16_t NUM_PARTICLES = 2000;
// static const float PARTICLE_MASS = 3;
static const float PARTICLE_MASS = 5;
// static const float GRAVITY = 0.5;
static const float GRAVITY = 2;
static const uint16_t ROWS = 240;
static const uint16_t COLS = 320;

float inputX = COLS / 2;
float inputY = ROWS / 2;

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();
boolean loadingflag = true;

template <class T>
class vec2
{
public:
  T x, y;

  vec2() : x(0), y(0) {}
  vec2(T x, T y) : x(x), y(y) {}
  vec2(const vec2 &v) : x(v.x), y(v.y) {}

  vec2 &operator=(const vec2 &v)
  {
    x = v.x;
    y = v.y;
    return *this;
  }

  bool operator==(vec2 &v)
  {
    return x == v.x && y == v.y;
  }

  bool operator!=(vec2 &v)
  {
    return !(x == y);
  }

  vec2 operator+(vec2 &v)
  {
    return vec2(x + v.x, y + v.y);
  }

  vec2 operator-(vec2 &v)
  {
    return vec2(x - v.x, y - v.y);
  }

  vec2 &operator+=(vec2 &v)
  {
    x += v.x;
    y += v.y;
    return *this;
  }

  vec2 &operator-=(vec2 &v)
  {
    x -= v.x;
    y -= v.y;
    return *this;
  }

  vec2 operator+(double s)
  {
    return vec2(x + s, y + s);
  }

  vec2 operator-(double s)
  {
    return vec2(x - s, y - s);
  }

  vec2 operator*(double s)
  {
    return vec2(x * s, y * s);
  }

  vec2 operator/(double s)
  {
    return vec2(x / s, y / s);
  }

  vec2 &operator+=(double s)
  {
    x += s;
    y += s;
    return *this;
  }

  vec2 &operator-=(double s)
  {
    x -= s;
    y -= s;
    return *this;
  }

  vec2 &operator*=(double s)
  {
    x *= s;
    y *= s;
    return *this;
  }

  vec2 &operator/=(double s)
  {
    x /= s;
    y /= s;
    return *this;
  }

  void set(T x, T y)
  {
    this->x = x;
    this->y = y;
  }

  void rotate(double deg)
  {
    double theta = deg / 180.0 * M_PI;
    double c = cos(theta);
    double s = sin(theta);
    double tx = x * c - y * s;
    double ty = x * s + y * c;
    x = tx;
    y = ty;
  }

  vec2 &normalize()
  {
    if (length() == 0)
      return *this;
    *this *= (1.0 / length());
    return *this;
  }

  float dist(vec2 v) const
  {
    // vec2 d(v.x - x, v.y - y);
    // return d.length();
    return sqrt(v.x * x + v.y * y);
  }

  float length() const
  {
    return sqrt(x * x + y * y);
  }

  void truncate(double length)
  {
    double angle = atan2f(y, x);
    x = length * cos(angle);
    y = length * sin(angle);
  }

  vec2 ortho() const
  {
    return vec2(y, -x);
  }

  static float dot(vec2 v1, vec2 v2)
  {
    return v1.x * v2.x + v1.y * v2.y;
  }

  static float cross(vec2 v1, vec2 v2)
  {
    return (v1.x * v2.y) - (v1.y * v2.x);
  }

  float mag() const
  {
    return length();
  }

  float magSq()
  {
    return (x * x + y * y);
  }

  void limit(float max)
  {
    if (magSq() > max * max)
    {
      normalize();
      *this *= max;
    }
  }
};

typedef vec2<float> PVector;
typedef vec2<double> vec2d;

class Boid
{
public:
  PVector location;
  PVector velocity;
  PVector acceleration;
  int hue;

  float mass;
  boolean enabled = true;

  Boid() {}

  Boid(float x, float y)
  {
    acceleration = PVector(0, 0);
    velocity = PVector(randomf(), randomf());
    location = PVector(x, y);
    hue = random(0x0A0A0A0A, 0xFFFFFFFF);
  }

  static float randomf()
  {
    return mapfloat(random(0, 255), 0, 255, -.5, .5);
  }

  static float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
  {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  }

  void run(Boid boids[], uint8_t boidCount)
  {
    update();
  }

  // Method to update location
  void update()
  {
    velocity += acceleration;
    location += velocity;
    acceleration *= 0;
  }

  void applyForce(PVector force)
  {
    // We could add mass here if we want A = F / M
    acceleration += force;
  }
};

class Attractor
{
public:
  float mass;
  float G;
  PVector location;

  Attractor()
  {
    resetAttractor();
  }

  void resetAttractor()
  {
    location = PVector(inputX, inputY);
    mass = PARTICLE_MASS;
    G = GRAVITY;
  }

  PVector attract(Boid m)
  {
    PVector force = location - m.location; // Calculate direction of force
    float d = force.mag();                 // Distance between objects
    d = constrain(d, 6.0, 9.0);            // Limiting the distance to eliminate "extreme" results for very close or very far objects
    force.normalize();
    float strength = (G * mass * 2) / (d * d); // Calculate gravitional force magnitude
    force *= strength;                         // Get force vector --> magnitude * direction
    return force;
  }
};

Attractor attractor;
bool loadingFlag = true;

Boid boidss[NUM_PARTICLES]; // this makes the boids
uint16_t x;
uint16_t y;
uint16_t huecounter = 1;

void start()
{
  int direction = random(0, 2);
  if (direction == 0)
    direction = -1;

  uint16_t ROW_CENTER = ROWS / 2;
  uint16_t COL_CENTER = COLS / 2;

  for (int i = 0; i < NUM_PARTICLES; i++)
  {
    Boid boid = Boid(random(1, COLS - 1), random(1, ROWS - 1)); // Full screen
    // Boid boid = Boid(random(COL_CENTER - ROW_CENTER, COL_CENTER + ROW_CENTER), random(1, ROWS - 1)); // Square in middle
    boid.velocity.x = ((float)random(40, 50)) / 14.0;
    boid.velocity.x *= direction;
    boid.velocity.y = ((float)random(40, 50)) / 14.0;
    boid.velocity.y *= direction;
    boid.hue = huecounter;
    huecounter += 0xFABCDE;
    boidss[i] = boid;
  }
}

void printTouchToSerial(TS_Point p)
{
  Serial.print("Pressure = ");
  Serial.print(p.z);
  Serial.print(", x = ");
  Serial.print(p.x);
  Serial.print(", y = ");
  Serial.print(p.y);
  Serial.println();
}

void printTouchToDisplay(TS_Point p)
{
  // Clear screen first
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int x = 320 / 2; // center of display
  int y = 100;
  int fontSize = 2;

  String temp = "Pressure = " + String(p.z);
  tft.drawCentreString(temp, x, y, fontSize);

  y += 16;
  temp = "X = " + String(p.x);
  tft.drawCentreString(temp, x, y, fontSize);

  y += 16;
  temp = "Y = " + String(p.y);
  tft.drawCentreString(temp, x, y, fontSize);
}

void printConvertedTouchToDisplay(float inX, float inY, int16_t z)
{
  // Clear screen first
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int fontSize = 2;

  int x = 320 / 2; // center of display
  int y = 100;

  String temp = "Pressure = " + String(z);
  tft.drawCentreString(temp, x, y, fontSize);

  y += 16;
  temp = "X = " + String(inX);
  tft.drawCentreString(temp, x, y, fontSize);

  y += 16;
  temp = "Y = " + String(inY);
  tft.drawCentreString(temp, x, y, fontSize);
}

void setup()
{
  // Start the SPI for the touch screen and init the TS library
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Start the tft display and set it to black
  tft.init();
  tft.fillScreen(TFT_BLACK);

  lastMillis = millis();

  delay(2000);
}

void loop()
{
  if (loadingFlag)
  {
    loadingFlag = false;
    start();
  }

  unsigned long currentMillis = millis();

  // Get frame rate.
  fps = 1000 / max(currentMillis - lastMillis, (unsigned long)1);
  sprintf(fpsStringBuffer, "fps: %lu", fps);
  
  // Display frame rate
  tft.fillRect(1, 1, 55, 10, TFT_BLACK);
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
    inputY = std::abs(ROWS - map(p.y, TS_MINY, TS_MAXY, 0, ROWS)); // Needs flipping this axis for some reason.

    tft.drawCircle(inputY, inputX, 6, TFT_SKYBLUE);

    attractor.resetAttractor();
  }

  // Move particles.
  for (int i = 0; i < NUM_PARTICLES; i++)
  {
    Boid boid = boidss[i];
    tft.drawPixel(boid.location.y, boid.location.x, 0x0000);
    PVector force = attractor.attract(boid);
    boid.applyForce(force);
    boid.update();
    tft.drawPixel(boid.location.y, boid.location.x, boid.hue);
    boidss[i] = boid;
  }
}
