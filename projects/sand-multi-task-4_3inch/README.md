# Sand (Multi-Task) 4.3 Inch

Modified from the prior [Sand (Multi-Task) project](../sand-multi-task). This code targets the 4.3 inch display, with capacitive touch, version of the CYD - the ESP32-8048S043C.

I also messed around a little with this version and added an option to drop a random seleciton of a few shapes supported by the [LovyanGFX](https://github.com/lovyan03/LovyanGFX) graphics library. I also added a routine that color cycles the fallen pixels over time.

### Description from the prior [Sand (Multi-Task) Project](../sand-multi-task)

I created this project as a copy of my [Sand project](../sand) initially and then added multi-tasking to take advantage of the dual-core CPU in the ESP32. This was my first time trying out parallelization programming on an MCU and with FreeRTOS.

The performance was indeed improved, but not by quite as much as I was hoping. I haven't done any performance analysis, but I suspect there is some overhead with the constant semaphore context switching. Still, a win is a win.

You can see a Youtube video of the code in action here:

[![Sand Simulation on the Cheap Yellow Display (CYD) ESP32 MCU + Display](https://img.youtube.com/vi/j8XRMEEZ0gM/0.jpg)](https://www.youtube.com/watch?v=j8XRMEEZ0gM)
