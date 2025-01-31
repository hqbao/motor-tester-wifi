Getting Started
====================


Setup and Installation
---------------------------

1. Set up environment variables: Run get_idf to set up the necessary environment variables.
2. Build the project: Run idf.py build to build the project.
3. Flash to device: Run idf.py -p <port> flash to flash the project to your device.
4. View console logs: Run idf.py -p <port> monitor to view the console logs.


Additional Tips
-------------------

- Flash and monitor: Run idf.py -p <port> flash monitor to flash and monitor the device simultaneously.
- List available ports (MacOS): Run ls /dev/cu.* to list the available ports.
- Clean the build: Run idf.py clean to clean the build directory.


Usage
-----

Follow the steps above to set up, build, and flash your project. Use the additional tips to troubleshoot and optimize your workflow.