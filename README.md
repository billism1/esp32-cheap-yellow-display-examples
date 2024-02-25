# Projects using the CYD (Cheap Yellow Display)

These are projects I made* to run on the [ESP32 Arduino LVGL WIFI&Bluetooth Development Board 2.8 " 240*320 Smart Display Screen 2.8inch LCD TFT Module With Touch WROOM](https://www.aliexpress.us/item/3256805849164942.html) board as sold on AliExpress. This is the ESP32-2432S028 board. Based on the ESP32-D0WDQ6.

For more information on this CYD and its variants, more example projects, mods, and more; check out [Brian Lough's ESP32-Cheap-Yellow-Display GitHub repo](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

### Other useful links:

[LVGL drivers for Chinese Sunton Smart display boards, aka CYD (Cheap Yellow Display)](https://github.com/rzeldent/esp32-smartdisplay)

[Sunton CYD (Cheap Yellow Display) PlatformIo Board definitions](https://github.com/rzeldent/platformio-espressif32-sunton/)

---

# The projects

- [Sand](projects/sand) A falling sand with new sand/pixels changing color over time, falling from the touchscreen inpout point.
- [Paint](projects/paint) A simple spray paint program with the new paint changing color over time.
- [Fluid Simulation *](projects/fluid-simulation) A simple (though not state-of-the-art) C++ implementation of Jos Stam's fluid simulation method
- [Particles **](projects/particles) Particles orbiting around a point of center mass.

---

\* The "Fluid Simulation" project was copied from [github.com/colonelwatch/ESP32-fluid-simulation/](https://github.com/colonelwatch/ESP32-fluid-simulation/). I modified it to work on my CYD device and converted it to a Platform.io project.

\*\* The "Particles" project was copied from [github.com/doctorpartlow/esp32s3lilygoanimations](https://github.com/doctorpartlow/esp32s3lilygoanimations/blob/main/particles.ino). I fixed a couple monor bugs and converted it to a Platform.io project.
