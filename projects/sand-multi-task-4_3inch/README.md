# Sand (Multi-Task)

I created this project as a copy of my [Sand project](../sand) initially and then added multi-tasking to take advantage of the dual-core CPU in the ESP32. This was my first time trying out parallelization programming on an MCU and with FreeRTOS.

The performance was indeed improved, but not by quite as much as I was hoping. I haven't done any performance analysis, but I suspect there is some overhead with the constant semaphore context switching. Still, a win is a win.

You can see a Youtube video of the code in action here:

[![Sand Simulation on the Cheap Yellow Display (CYD) ESP32 MCU + Display](https://img.youtube.com/vi/j8XRMEEZ0gM/0.jpg)](https://www.youtube.com/watch?v=j8XRMEEZ0gM)
