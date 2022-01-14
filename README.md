# Arduino-Soundlab
This project is based on https://www.instructables.com/Arduino-Soundlab/ from rgco (Rolf Oldeman).

Its a 2 operator fm synthesizer running on a arduino nano.

license: https://creativecommons.org/licenses/by-nc-sa/2.5/

## How to use the midi over usb (linux)
Because the arduino is not capable of usb host mode without extra hardware I use a serial to midi conversion tool called ttymidi: https://github.com/cjbarnes18/ttymidi
This tool emulates midi over usb serial connection with the arduino. You can use it as a midi device on your pc via usb and control it with standard midi controllers.

This command should be enough to make the arduino visible as a midi device:
```
ttymidi -b 9600 -s /dev/ttyUSB0
```
You can connect the arduino to another midi program by:
```
aconnect -i                       # list available MIDI input clients
aconnect -o                       # list available MIDI output clients
aconnect 128:0 129:0              # where 128 and 129 are the client numbers for
```
Or use a graphical tool for the connection like qjackctl.

The arduino should respond to all midi notes played by all channels and should respond to the first 8 controlchanges by all channels.


