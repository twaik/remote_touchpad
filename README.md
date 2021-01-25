# What is it
This is a highly experimental tool that allows us to use Android device as a touchpad

# How does it work
This tool uses [Pico](https://github.com/foxweb/pico) to behave as a Web server and uinput for input device emulation (requires `root`). 

# How to use it
1. You should build (`make`) and start it with superuser privilegies (`sudo build/server`). 
2. You should open Chrome browser on your Android device and connect to the server. 
3. Now you are able to interact with PC using your Android device as a touchpad.
