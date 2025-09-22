# esp32-Alpaca

An alpaca implementation for esp32. But not only!

This repository contains an implementation of an AscomAlpaca server that you can use to create your own Alpaca devices.

Althrough this code was made and intended to be used by an esp32, it has only one dependency on esp systems (the persistant storage), which means that it would be trivial to make it work on other systems.
Note that you can also compile it under windows (mostly for testing)...

Another advantage of this code is that it has NO dependencies on any 3rd party libraries.
The only dependencies are:
- stdio
- sockets (lwip on alpaca, winsocks on windows)
- Free-rtos for "createTask" (and CreateThread on windows)
- esp persistant storage (nvs) and createFile on windows.

The code is contained in only 2 files. alpaca.h and alpaca.cpp


The code has 100% support for Telescope, Focuser and FilterWheel. The other device types are only partially created. Should you need them please contact me and I will take the time to add them.

You will find an example that implements a telescope and a focuser in the main.cpp file.

cyrille.de.brebisson@gmail.com