//=====================================================================
// https://github.com/lovyan03/LovyanGFX/tree/master/src/lgfx/v1/platforms/esp32s3
//=====================================================================
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device{
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  lgfx::Light_PWM   _light_instance;
  lgfx::Touch_GT911 _touch_instance;

public:LGFX(void){
  auto cfg              = _bus_instance.config();
  cfg.panel             = &_panel_instance;
  cfg.pin_d0            = 8;  // B0
  cfg.pin_d1            = 3;  // B1
  cfg.pin_d2            = 46; // B2
  cfg.pin_d3            = 9;  // B3
  cfg.pin_d4            = 1;  // B4
  cfg.pin_d5            = 5;  // G0
  cfg.pin_d6            = 6;  // G1
  cfg.pin_d7            = 7;  // G2
  cfg.pin_d8            = 15; // G3
  cfg.pin_d9            = 16; // G4
  cfg.pin_d10           = 4;  // G5
  cfg.pin_d11           = 45; // R0
  cfg.pin_d12           = 48; // R1
  cfg.pin_d13           = 47; // R2
  cfg.pin_d14           = 21; // R3
  cfg.pin_d15           = 14; // R4
  cfg.pin_henable       = 40;
  cfg.pin_vsync         = 41;
  cfg.pin_hsync         = 39;
  cfg.pin_pclk          = 42;
  cfg.freq_write        = 14000000;
  cfg.hsync_polarity    = 0;
  cfg.hsync_front_porch = 8;
  cfg.hsync_pulse_width = 4;
  cfg.hsync_back_porch  = 16;
  cfg.vsync_polarity    = 0;
  cfg.vsync_front_porch = 4;
  cfg.vsync_pulse_width = 4;
  cfg.vsync_back_porch  = 4;
  cfg.pclk_idle_high    = 1;
  _bus_instance.config(cfg);
  _panel_instance.setBus(&_bus_instance);
  
  { auto cfg = _panel_instance.config();
  cfg.memory_width      = 800;
  cfg.memory_height     = 480;
  cfg.panel_width       = 800;
  cfg.panel_height      = 480;
  cfg.offset_x          = 1000;
  cfg.offset_y          = 2000;
  _panel_instance.config(cfg);
  }
  
  { auto cfg = _panel_instance.config_detail();
  cfg.use_psram         = 1;
  _panel_instance.config_detail(cfg);
  }

  { auto cfg = _light_instance.config();
  cfg.pin_bl            = 2;
  cfg.freq              = 44100;
  cfg.pwm_channel       = 7;
  _light_instance.config(cfg);
  }
  _panel_instance.light(&_light_instance);
  
  { auto cfg = _touch_instance.config();
  cfg.x_min             = 0;      // タッチスクリーンから得られる最小のX値(生の値)
  cfg.x_max             = 480;    // タッチスクリーンから得られる最大のX値(生の値)
  cfg.y_min             = 0;      // タッチスクリーンから得られる最小のY値(生の値)
  cfg.y_max             = 272;    // タッチスクリーンから得られる最大のY値(生の値)
  cfg.pin_int           = -1;     // INTが接続されているピン番号 18
  cfg.bus_shared        = false;  // 画面と共通のバスを使用している場合 trueを設定
  cfg.offset_rotation   = 0; // 表示とタッチの向きの調整 0~7の値で設定
  // I2C接続
  cfg.i2c_port          = 1;      // I2C(0 = SPI or 1 = Wire)
  cfg.pin_sda           = 19;     // SDA
  cfg.pin_scl           = 20;     // SCL
  cfg.pin_rst           = 38;
  cfg.freq              = 800000; // I2C
  cfg.i2c_addr          = 0x5D;   // I2C 0x5D or 0x14
  _touch_instance.config(cfg);
  _panel_instance.setTouch(&_touch_instance);//タッチスクリーンをパネルにセット
  }
  
  setPanel(&_panel_instance); // 使用するパネルをセットします。
  }
};
