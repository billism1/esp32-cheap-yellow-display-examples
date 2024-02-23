# ESP32-fluid-simulation

Project from:

https://github.com/colonelwatch/ESP32-fluid-simulation/

I made the following changes:
- I renamed ESP32-fluid-simulation.ino to main.cpp
- In the main cpp source file, I changed:
    - SPIClass mySpi = SPIClass(HSPI);
    - to:
    - SPIClass mySpi = SPIClass(VSPI);
    - Because HSPI did not work for the board I have.
- I converted this to a Platform.io project for easy dependency management.
