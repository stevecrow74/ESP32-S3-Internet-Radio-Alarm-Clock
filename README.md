# ESP32-S3-Internet-Radio-Alarm-Clock
Not just any ordinary radio alarm clock

# webui
The webui displays sections.
the top section is three buttons that change between pages Clock, Radio, Alarm.
below that is what's currently playing on the radio, Station name with metadata 
showing song title and artist (if available).
two buttons to select previous/next radio station.
A volume slider to increase/decrease volume with text readout below that.

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3637.png

Next section you can select radio station through a dropdown box.
next section displays alarm time and alarm status.
In the dropdown box you can select the time either by the scroll function (mobile view) or numerical input (desktop view)
in mobile view you can tap on the scrolled numbers to enter alarm time with mobile number pad. then ta the blue tick to 
enter that time into the box and the save time to accept the changes. With Button to turn alarm on/off.
below that are two dropdown boxes to change primary and fallback stations.

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3638.png

The last section shows device details, WIFI, IP, Time, Alarm time and status.
at the very bottom is a RESTART DEVICE button, this will reset the device the same as the physical button and volume down combination.
when pressed you will get a confirmation screen, press ok to restart the device.

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3639.png

# Clock
The clock page displays time, wifi connection, date, alarm time, alarm icon (grey/alarm off, blue/alarm on)
current radio stream and button press details (short: Radio, Hold 3s: Alarm)

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3641.jpg

# Radio
The radio page shows previous stream, now playing stream, and next stream, selected with touch pads.
radio stream metadata, Artist and Song (if available).
Volume bar, visual indication of volume level.
Volume numeric indication.
touch pad instructions ( + station - , + volume - )

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3640.jpg

# Alarm
The alarm page shows alrarm on/off.
Alarm set time.
change alarm time instructions for touch pads ( -H +H  +M -M) 
primary station for alarm to use.
fallback station if first station doesn't load
touchpad instructions, as above, and Button = save (press physical button to save, if alarm off, pressing button in this 
page will turn it on, and if alarm is on, pressing button will turn it off while in this page.

When alarm triggers, the volume moves up in steps from 0 to max in 10 second intervals.

https://github.com/stevecrow74/ESP32-S3-Internet-Radio-Alarm-Clock/blob/main/img_3642.jpg

# Reset
it is possible for what ever reason to reset the device using a button combination.
push physical button and Volume down pad together for 1 second, this will force a reset.
alarm time and current station shouldn't be affected by this.

# Backlight led
It is possible to turn on and off the backlight led to the screen (with hardware mod, see below).
Push and hold physical button and Volume up pad for one second to switch on/off.





# ESP32-S3 Radio Alarm Clock

Internet radio and alarm clock firmware for an ESP32-S3, an ST7789 color TFT, capacitive touch controls, a push button, and an external I2S audio output stage. The firmware also serves a browser-based control panel on the local network.

## Features

- Clock display with local time, date, and Wi-Fi status.
- Radio station playback, station browsing, volume adjustment, and stream metadata.
- Alarm time, enable state, primary station, and fallback station saved in ESP32 Preferences.
- Alarm volume ramps up gradually and ringing stops automatically after 30 minutes.
- Web controls for display page, radio station, volume, alarm configuration, and device restart.
- Physical controls for changing stations, setting the alarm, dismissing a ringing alarm, and restarting the device.

## Hardware

| Device | Connection / details |
| --- | --- |
| ESP32-S3-N16R8 | 16 MB flash, 8 MB PSRAM; current PlatformIO target is `esp32-s3-devkitc-1` |
| GMT020-02 7p v1.3 LCD | 2-inch ST7789 color LCD, SPI; wiring below |
| Capacitive touch electrodes or compatible touch inputs | ESP32-S3 GPIOs 2, 6, 4, and 5 |
| Momentary push button | GPIO 1 to GND; configured with internal pull-up |
| GY-PCM5102 I2S stereo board | I2S DAC; connect its analog output to an amplifier or powered speakers |
| USB connection / suitable supply | Programming, serial monitor, and board power |

### Pin Map

| Function | ESP32-S3 GPIO |
| --- | ---: |
| TFT SCLK | 12 |
| TFT MOSI | 11 |
| TFT CS | 10 |
| TFT DC | 9 |
| TFT reset | 8 |
| Touch: previous / alarm hour down | 2 |
| Touch: next / alarm hour up | 6 |
| Touch: volume down / alarm minute down | 4 |
| Touch: volume up / alarm minute up | 5 |
| Push button | 1 |
| I2S BCLK | 16 |
| I2S LRC / word select | 15 |
| I2S DOUT | 17 |

The display backlight and power wiring are not configured by this firmware; follow the GMT020-02 module requirements. Connect the I2S pins to the GY-PCM5102 board. The PCM5102 provides line-level analog audio, so use an amplifier or powered speakers for speaker output. Check module voltage and pin labels before powering the circuit.

The N16R8 hardware has more flash and PSRAM than the generic `esp32-s3-devkitc-1` target may describe. The firmware currently uses that target in `platformio.ini`; check the PlatformIO build output and use a matching board profile/configuration if you need PlatformIO to configure all onboard memory. The firmware does not currently require PSRAM.

## Build and Upload

The project uses PlatformIO with the Arduino framework. From the project root (the directory containing `platformio.ini`):

```sh
pio run
pio run --target upload
pio device monitor
```

The serial monitor runs at **115200 baud**. PlatformIO installs the libraries listed in `platformio.ini` during the build. The upload port is not explicitly set, so PlatformIO will select a detected port; set `upload_port` in `platformio.ini` if automatic selection does not find the board.

## First-Time Setup

1. Wire the display, touch inputs, button, and I2S audio output according to the pin map.
2. Set `WIFI_SSID` and `WIFI_PASSWORD` in `src/main.cpp` for the network the device will use. These credentials are currently stored directly in the source; replace them with your own and do not publish real credentials.
3. Check `TZ_INFO` in `src/main.cpp` for the desired time zone. The current setting is for Ireland/UK-style GMT/IST daylight-saving transitions.
4. Build and upload the firmware, then open the serial monitor at 115200 baud.
5. Keep the capacitive touch electrodes untouched during startup calibration. The device prints its Wi-Fi address when it connects.
6. Open `http://<device-ip>/` in a browser on the same local network.

Wi-Fi connection is attempted for up to 20 seconds at startup. Internet access is needed for NTP time synchronization and internet radio streams. Alarm settings are stored in non-volatile Preferences and survive a reboot.

## Controls

### Touch Inputs

| Input | Clock / Now Playing pages | Alarm Setup page |
| --- | --- | --- |
| Previous (GPIO 2) | Previous radio station | Decrease alarm hour |
| Next (GPIO 6) | Next radio station | Increase alarm hour |
| Volume down (GPIO 4) | Decrease volume | Decrease alarm minute |
| Volume up (GPIO 5) | Increase volume | Increase alarm minute |

### Push Button (GPIO 1)

- Quick press, under 1 second: stop a ringing alarm; otherwise toggle the alarm on/off and return to the clock when on Alarm Setup; otherwise switch between Clock and Now Playing.
- Hold for 3 seconds: open Alarm Setup.
- Hold the button and touch Volume Down together for 1 second: restart the device. This is a restart, not a factory reset; saved alarm settings are retained.

When manually stopped, a ringing alarm remains enabled and can ring again the next day. An alarm also stops automatically after 30 minutes.

### Web Control Panel

The web page provides three display-page buttons (Clock, Now Playing, and Alarm), station selection, volume, alarm time and enable controls, primary/fallback alarm station selection, and device/network status. The **Restart Device** button asks for confirmation before restarting; saved settings are retained.

The web server uses plain HTTP and has no authentication. Keep it on a trusted local network and do not expose it directly to the internet.

## Troubleshooting

- **No Wi-Fi or no web page:** Check the SSID/password in `src/main.cpp`, confirm the device and browser are on the same network, then use the IP printed to serial.
- **Incorrect clock:** Confirm Wi-Fi and internet access, and verify `TZ_INFO` for the local time zone.
- **Touch controls trigger at startup:** Keep the touch electrodes clear during startup calibration. Check electrode wiring and avoid touching them while the board boots.
- **No radio audio:** Confirm internet access and station availability; check the I2S BCLK, LRC, and DOUT connections and the external DAC/amplifier wiring.
- **No display:** Verify TFT power, backlight, SPI wiring, and the CS/DC/reset connections against the pin map.
- **Alarm station fails:** The alarm attempts its configured fallback station if playback reports an error.

## Hardware Modifications

**GY-PCM5102 I2S stereo board:** cut the trace to leg 12 on the PAM8403 chip, this is Shutdown SHDN, connect this pin to GPIO 14.
This mod, cuts all noise from the amp when volume is set to 0.
**GMT020-02 7p v1.3 LCD:** Lift the resistor R6 from the pad on the side labeled R6, using a transistor S9013, connect the Emitter to the bare pad, conect the Collector to the resistor, connect the Base to a 1 kOhm resistor and that to GPIO 7. this will control the backlight leds and turn them on/off with  hold button + volume up for one second.


## Project Files

- `platformio.ini` - PlatformIO board, framework, upload/monitor speeds, and library dependencies.
- `src/main.cpp` - Firmware, pin assignments, network/time-zone settings, web UI, and device behavior.

