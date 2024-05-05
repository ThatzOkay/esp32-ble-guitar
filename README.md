# ESP32 BLE guitar

Transform toy guitar into a Clone Hero controller with ESP32

**Pinout**

| ESP32 GPIO | Button               |
| ---------- | -------------------- |
| GPIO36     | Whammy potentiometer |
| GPIO32     | Strum up             |
| GPIO33     | Strum down           |
| GPIO21     | SDA                  |
| GPIO22     | SCL                  |
| 3.3V       | V pin on neck        |
| GPIO25     | LED on DPAD          |
| GPIO26     | Start                |
| GPIO23     | Select               |
| GPIO19     | DPAD RIGHT           |
| GPIO18     | DPAD LEFT            |
| GPIO17     | DPAD DOWN            |
| GPIO16     | DPAD UP              |



**Before:**

![](docs/guitar-before.jpg)

**After:**

![](docs/guitar-after.jpg)

**Credits**
[1dle](https://github.com/1dle/esp32-ble-guitar) Original controller code for guitar
[mnkhouri](https://gist.github.com/mnkhouri/e6ac28bc48560b31890ddb61cc7f7a87#file-gh5_neck_comm-ino) I2C code to communicate with the neck