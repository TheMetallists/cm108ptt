# COM port to cm108 PTT bridge

This app allows owners of cm108-based soundcard CAT interfaces to use 
older software that only supports PTT via serial port.

Usage:
* Install a loopback serial interface, like [com0com](https://github.com/vovsoft/com0com)
* Create a serial port pair, make sure one port's signals (like DTR) forwarded to another port's (like DSR)
* Configure your softmodem to use one of the ports from this pair
* Configure this app to open the second port, and cm108's parameters.
* Start the app's bridging function


## Configuration trick

When you click "start" the app saves it's settings in the Windows registry.
The configuration is saved under different subkeys, based on the app's filename.
On some systems it can even read the filename of the .lnk shortcut used to start the app,
so you can keep one .exe file, creating a shortcut per rig.

Only the base name, like "feng.lnk" is used, so you can move your files, but keep it in mind in case you expect two 
files with the same name, located in different places, to have different settings profiles.

Configuration layout:
```text
HKEY_CURRENT_USER\Software\HAMradio\cm108ptt\<filename_hash>\
    ├── FilenameForSettings (REG_SZ)      : "IC-7300.lnk"
    ├── SerialPort           (REG_SZ)      : "COM11"
    ├── Signal               (REG_DWORD)   : 0 (DTR) // see src/config.h
    ├── Invert               (REG_DWORD)   : 0 (0 = normal, 1 = inverted)
    ├── VID                  (REG_SZ)      : "0d8c"
    ├── PID                  (REG_SZ)      : "0012"
    ├── CM108Index           (REG_DWORD)   : 0
    ├── GPIO                 (REG_DWORD)   : 3
    └── PollIntervalMs       (REG_DWORD)   : 75
```

## Compressors

The tool is designed to be small, so that it can be transmitted over the radio, if such need arises.
It is built with two compressors:
* **Crinkler** - 5.1kb 32-bit executable without an icon, might work on winXP (tested only on win10)
* **UPX** - 16kb 64-bit executable with an icon, in case M$ drops 32-bit support

UPX has a history of being false-positived by AVs, so feel free to use the Crinkler build if your does not like it.
(or better, review the source and compile it yourself).


.

.
## EMOTIONAL SUPPORT
If you want to donate, you can use my Bitcoin wallet:
- bc1qy633s5me0ytyalgl3ra8k2pgc4eaqcgcl3srwc
