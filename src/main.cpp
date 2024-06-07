#include <Arduino.h>
#include <Wire.h>
#include "esp_bt.h"
#include <BleGamepad.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "config.h"

#define numOfButtons 64
#define numOfHatSwitches 0

BleGamepad *bleGamepad = nullptr;

const byte no_neck_buttons = 5;
const byte no_gitadora_up_down = 2;
const byte no_physical_buttons = 8;

const byte no_buttons = no_neck_buttons + no_gitadora_up_down + no_physical_buttons;

byte physical_buttons[no_physical_buttons] = {16, 17, 18, 19, 23, 26, 33, 32};
const byte led = 25;
const byte whammy = 36;
const byte tilt = 14;

const byte pot_samples = 5;

byte previousButtonStates[numOfButtons];
byte currentButtonStates[numOfButtons];

#define NECK_ADDRESS 0x0D
#define MPU_ADDRESS 0x68
const char *hex = "0123456789ABCDEF";
typedef struct
{
  bool green;
  bool red;
  bool yellow;
  bool blue;
  bool orange;
} NeckButtons;

typedef struct
{
  int roll;
  int pitch;
  int yaw;
} Accelerometer;

void setup()
{
  Wire.begin();

  // init button inputs.
  for (byte i = 0; i < no_physical_buttons; i++)
  {
    pinMode(physical_buttons[i], INPUT_PULLUP);
  }

  for (byte i = 0; i < no_buttons; i++)
  {
    previousButtonStates[no_buttons] = HIGH;
    currentButtonStates[no_buttons] = HIGH;
  }

  pinMode(led, OUTPUT);

  if (ENABLE_TILT)
  {
    pinMode(tilt, INPUT);
  }

  bleGamepad = new BleGamepad(DEVICE_NAME, MANUFACTURER);

  // Serial.println("Starting BLE work!");
  bleGamepad->setAutoReport(false);
  bleGamepad->setControllerType(CONTROLLER_TYPE_GAMEPAD); // CONTROLLER_TYPE_JOYSTICK, CONTROLLER_TYPE_GAMEPAD (DEFAULT), CONTROLLER_TYPE_MULTI_AXIS
  bleGamepad->begin(numOfButtons, numOfHatSwitches);      // Simulation controls are disabled by

  // inrease transmit power
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  // esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,ESP_PWR_LVL_P9);

  // digitalWrite(led, HIGH);

  if (SERIAL_EN)
  {
    Serial.begin(115200);
    Serial.println("Setup complete");
  }
}

void whammy_input()
{
  int sum_whammy_val = 0;
  for (byte i = 0; i < pot_samples; i++)
  {
    sum_whammy_val = analogRead(whammy);
    delay(4);
  }
  bleGamepad->setX(map(sum_whammy_val / pot_samples, 1920, 0, 32737, -32737));
}

void diagnoseTransmissionError(byte code)
{
  // Docs for wire.endtransmission: https://docs.particle.io/cards/firmware/wire-i2c/endtransmission/
  switch (code)
  {
  case 0:
    Serial.println("success");
    break;
  case 1:
    Serial.println("busy timeout upon entering endTransmission()");
    break;
  case 2:
    Serial.println("START bit generation timeout");
    break;
  case 3:
    Serial.println("end of address transmission timeout");
    break;
  case 4:
    Serial.println("data byte transfer timeout");
    break;
  case 5:
    Serial.println("data byte transfer succeeded, busy timeout immediately after");
    break;
  case 6:
    Serial.println("timeout waiting for peripheral to clear stop bit");
    break;
  default:
    Serial.print("Unknown return from EndTransmission: ");
    Serial.println(code);
  }
}

unsigned int readFromSerial(uint8_t *arr, unsigned int expectedByteCount)
{
  unsigned int readCount = 0;
  Wire.requestFrom(NECK_ADDRESS, expectedByteCount); // request N bytes from peripheral device
  while (Wire.available() && (readCount < expectedByteCount))
  {                               // peripheral may send less than requested
    arr[readCount] = Wire.read(); // receive a byte as character
    readCount++;
  }
  return readCount;
}

int previousTime = 0;
int currentTime = 0;

Accelerometer readFromMPU()
{
  Wire.beginTransmission(MPU_ADDRESS); // Start communication with MPU6050 // MPU=0x68
  Wire.write(0x3B);                    // Start with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDRESS, 6, true); // Read 6 registers total, each axis value is stored in 2 registers

  int16_t AcX = (Wire.read() << 8 | Wire.read()) / 16384.0; // X-axis value
  int16_t AcY = (Wire.read() << 8 | Wire.read()) / 16384.0; // Y-axis value
  int16_t AcZ = (Wire.read() << 8 | Wire.read()) / 16384.0; // Z-axis value

  int16_t acAngleX = (atan(AcY / sqrt(pow(AcX, 2) + pow(AcZ, 2))) * 180 / PI) - 0.58;
  int16_t acAngleY = (atan(-1 * AcX / sqrt(pow(AcY, 2) + pow(AcZ, 2))) * 180 / PI) + 1.58;

  previousTime = currentTime;
  currentTime = millis();
  int elapsedTime = currentTime - previousTime;

  Wire.beginTransmission(MPU_ADDRESS); // Start communication with MPU6050 // MPU=0x68
  Wire.write(0x43);                    // Gyro data first register address 0x43
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDRESS, 6, true); // Read 4 registers total, each axis value is stored in 2 registers

  int16_t GyroX = (Wire.read() << 8 | Wire.read()) / 131.0; // For a 250deg/s range we have to divide first the raw value by 131.0, according to the datasheet
  int16_t GyroY = (Wire.read() << 8 | Wire.read()) / 131.0; // For a 250deg/s range we have to divide first the raw value by 131.0, according to the datasheet
  int16_t GyroZ = (Wire.read() << 8 | Wire.read()) / 131.0; // For a 250deg/s range we have to divide first the raw value by 131.0, according to the datasheet

  GyroX = GyroX + 0.56;
  GyroY = GyroY - 2;
  GyroZ = GyroZ + 0.79;

  int16_t gyroAngleX = GyroX * elapsedTime / 1000;
  int16_t gyroAngleY = GyroY * elapsedTime / 1000;

  Accelerometer accelerometer = {.roll = 0.96 * gyroAngleY + 0.04 * acAngleX, .pitch = 0.96 * gyroAngleY + 0.04 * acAngleY, .yaw = GyroZ * elapsedTime};

  return accelerometer;
}

void printByteArray(uint8_t *arr, unsigned int len)
{
  Serial.print("Read ");
  Serial.print(len);
  Serial.print(" bytes:");
  for (unsigned int i = 0; i < len; i++)
  {
    Serial.print(" 0x");
    Serial.print(hex[(arr[i] >> 4) & 0xF]);
    Serial.print(hex[arr[i] & 0xF]);
  }
  Serial.println("");
}

void printButtons(NeckButtons buttons)
{
  if (VERBOSE)
  {
    Serial.print("Green: ");
  }
  Serial.print(buttons.green);
  if (VERBOSE)
  {
    Serial.print(" Red: ");
  }
  Serial.print(buttons.red);
  if (VERBOSE)
  {
    Serial.print(" Yellow: ");
  }
  Serial.print(buttons.yellow);
  if (VERBOSE)
  {
    Serial.print(" Blue: ");
  }
  Serial.print(buttons.blue);
  if (VERBOSE)
  {
    Serial.print(" Orange: ");
  }
  Serial.println(buttons.orange);
}

NeckButtons twoBytesToButton(uint8_t *arr)
{
  NeckButtons buttons = {.green = false, .red = false, .yellow = false, .blue = false, .orange = false};
  char topButtons = arr[0];
  if (topButtons & 0x10)
  {
    buttons.green = true;
  }
  if (topButtons & 0x20)
  {
    buttons.red = true;
  }
  if (topButtons & 0x80)
  {
    buttons.yellow = true;
  }
  if (topButtons & 0x40)
  {
    buttons.blue = true;
  }
  if (topButtons & 0x01)
  {
    buttons.orange = true;
  }

  // The remaining array on the real guitar 5 bytes long. It varies in a stable
  // way when pressing on the touch pad. However, we can get away with just
  // looking at the first byte of the five, since it has a unique value for
  // each combination of touchpad presses.
  // There's gotta be a pattern here, but I'm too tired to spot it...
  switch (arr[1])
  {
  case 0x00:
    // no buttons
    break;
  case 0x95:
    buttons.green = true;
    break;
  case 0xCD:
    buttons.red = true;
    break;
  case 0x1A:
    buttons.yellow = true;
    break;
  case 0x49:
    buttons.blue = true;
    break;
  case 0x7F:
    buttons.orange = true;
    break;
  case 0xB0:
    buttons.green = true;
    buttons.red = true;
    break;
  case 0x19:
    buttons.green = true;
    buttons.yellow = true;
    break;
  case 0x47:
    buttons.green = true;
    buttons.blue = true;
    break;
  case 0x7B:
    buttons.green = true;
    buttons.orange = true;
    break;
  case 0xE6:
    buttons.red = true;
    buttons.yellow = true;
    break;
  case 0x48:
    buttons.red = true;
    buttons.blue = true;
    break;
  case 0x7D:
    buttons.red = true;
    buttons.orange = true;
    break;
  case 0x2F:
    buttons.yellow = true;
    buttons.blue = true;
    break;
  case 0x7E:
    buttons.yellow = true;
    buttons.orange = true;
    break;
  case 0x66:
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x65:
    buttons.yellow = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x64:
    buttons.red = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x7C:
    buttons.red = true;
    buttons.yellow = true;
    buttons.orange = true;
    break;
  case 0x2E:
    buttons.red = true;
    buttons.yellow = true;
    buttons.blue = true;
    break;
  case 0x62:
    buttons.green = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x7A:
    buttons.green = true;
    buttons.yellow = true;
    buttons.orange = true;
    break;
  case 0x2D:
    buttons.green = true;
    buttons.yellow = true;
    buttons.blue = true;
    break;
  case 0x79:
    buttons.green = true;
    buttons.red = true;
    buttons.orange = true;
    break;
  case 0x46:
    buttons.green = true;
    buttons.red = true;
    buttons.blue = true;
    break;
  case 0xE5:
    buttons.green = true;
    buttons.red = true;
    buttons.yellow = true;
    break;
  case 0x63:
    buttons.red = true;
    buttons.yellow = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x61:
    buttons.green = true;
    buttons.yellow = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x60:
    buttons.green = true;
    buttons.red = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  case 0x78:
    buttons.green = true;
    buttons.red = true;
    buttons.yellow = true;
    buttons.orange = true;
    break;
  case 0x2C:
    buttons.green = true;
    buttons.red = true;
    buttons.yellow = true;
    buttons.blue = true;
    break;
  case 0x5F:
    buttons.green = true;
    buttons.red = true;
    buttons.yellow = true;
    buttons.blue = true;
    buttons.orange = true;
    break;
  default:
    if (SERIAL_EN)
    {
      Serial.print("Unrecognized pattern! ");
      printByteArray(&arr[1], 1);
    }
  }

  return buttons;
}

bool initializeNeck()
{
  Wire.beginTransmission(NECK_ADDRESS); // Transmit to device
  Wire.write(0x00);                     // Sends value byte
  byte error = Wire.endTransmission();  // Stop transmitting
  if (error != 0)
  {
    if (SERIAL_EN)
    {
      diagnoseTransmissionError(error);
    }
    return false;
  }

  unsigned int expectedInitBytes = 7;
  uint8_t values[expectedInitBytes];
  unsigned int readCount = readFromSerial(values, expectedInitBytes);
  if (SERIAL_EN && VERBOSE)
  {
    printByteArray(values, readCount);
  }
  if (readCount == expectedInitBytes)
  {
    if (SERIAL_EN)
    {
      Serial.println("Initialized");
    }
    return true;
  }
  else
  {
    if (SERIAL_EN)
    {
      Serial.println("Wrong byte count read");
    }
    return false;
  }
}

bool initializeMPU()
{
  Wire.beginTransmission(MPU_ADDRESS); // Start communication with MPU6050 // MPU=0x68
  Wire.write(0x6B);                    // Talk to the register 6B
  Wire.write(0x00);                    // Make reset - place a 0 into the 6B register
  Wire.endTransmission(true);          // end the transmission

  return true;
}

void initOTA()
{
  if (SERIAL_EN)
  {
    Serial.println("Enabling OTA");
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    if (SERIAL_EN)
    {
      Serial.println("Connection Failed! Rebooting...");
    }
    delay(5000);
    ESP.restart();
  }
  
  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    {
      type = "filesystem";
    }

    if (SERIAL_EN)
    {
      Serial.println("Start updating " + type);
    }
  }).onEnd([]() {
    if (SERIAL_EN)
    {
      Serial.println("\nEnd");
    }
  }).onProgress([](unsigned int progress, unsigned int total) {
    if (SERIAL_EN)
    {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    }
  }).onError([](ota_error_t error) {
    if (SERIAL_EN)
    {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR)
      {
        Serial.println("Auth Failed");
      }
      else if (error == OTA_BEGIN_ERROR)
      {
        Serial.println("Begin Failed");
      }
      else if (error == OTA_CONNECT_ERROR)
      {
        Serial.println("Connect Failed");
      }
      else if (error == OTA_RECEIVE_ERROR)
      {
        Serial.println("Receive Failed");
      }
      else if (error == OTA_END_ERROR)
      {
        Serial.println("End Failed");
      }
    }
  });

  ArduinoOTA.begin();

  if (SERIAL_EN)
  {
    Serial.println("OTA ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

bool enableOTA = false;
bool isInitialized = false;
unsigned long lastChange = 0;
unsigned int loopCounter = 0; // Only used to control how often debug output is printed
int prevPitch = 0;

void loop()
{
  // handle_button_inputs();
  // whammy_input();

  if (!isInitialized)
  {
    delay(1000);
    if (SERIAL_EN)
    {
      Serial.println("Not yet initialized");
    }

    // if more then 3 buttons are pressed at the sae timme
    int pressedButtons = 0;
    for (byte i = 0; i < no_physical_buttons; i++)
    {
      if (digitalRead(physical_buttons[i]) == LOW)
      {
        pressedButtons++;
      }
    }

    if (pressedButtons >= 3)
    {
      enableOTA = true;
    }

    if (enableOTA)
    {
      if (SERIAL_EN)
      {
        Serial.println("Enabling OTA");
      }
      initOTA();
      isInitialized = true;
      return;
    }


    bool neckInitialized = initializeNeck();

    bool mpuInitialized = true;

    if (ENABLE_ACCELEROMETER)
    {
      mpuInitialized = initializeMPU();
    }

    if (!neckInitialized || !mpuInitialized)
    {
      return;
    }

    isInitialized = true;
  }

  if (enableOTA)
  {
    ArduinoOTA.handle();
    return;
  }

  if (SERIAL_EN && VERBOSE)
  {
    delay(500); // read every N ms
  }
  else
  {
    delay(0.5); // The guitar leaves a 9ms gap between reads, but it seems like we can go lower
  }

  if (bleGamepad != nullptr && bleGamepad->isConnected())
  {
    whammy_input(); // = delay 20 (5 samples 4ms delay between them)

    Wire.beginTransmission(0x0D);
    Wire.write(0x12);
    byte error = Wire.endTransmission(); // Stop transmitting
    if (error != 0)
    {
      if (SERIAL_EN)
      {
        diagnoseTransmissionError(error);
      }
    }

    unsigned int expectedByteCount = 2;
    uint8_t values[expectedByteCount];
    unsigned int readCount = readFromSerial(values, expectedByteCount);
    if (SERIAL_EN && VERBOSE)
    {
      printByteArray(values, readCount);
    }
    if (readCount != expectedByteCount)
    {
      if (SERIAL_EN)
      {
        Serial.println("Wrong byte count read");
      }
    }

    // reading neck buttons
    NeckButtons neckButtons = twoBytesToButton(values);
    if (neckButtons.green)
    {
      currentButtonStates[0] = LOW;
    }
    else
    {
      currentButtonStates[0] = HIGH;
    }
    if (neckButtons.red)
    {
      currentButtonStates[1] = LOW;
    }
    else
    {
      currentButtonStates[1] = HIGH;
    }
    if (neckButtons.yellow)
    {
      currentButtonStates[2] = LOW;
    }
    else
    {
      currentButtonStates[2] = HIGH;
    }
    if (neckButtons.blue)
    {
      currentButtonStates[3] = LOW;
    }
    else
    {
      currentButtonStates[3] = HIGH;
    }
    if (neckButtons.orange)
    {
      currentButtonStates[4] = LOW;
    }
    else
    {
      currentButtonStates[4] = HIGH;
    }

    if (ENABLE_ACCELEROMETER)
    {
      // reading accelerometer
      Accelerometer accelerometer = readFromMPU();

      int16_t pitchDifference = accelerometer.pitch - prevPitch;

      if (pitchDifference > ACCELEROMETER_THRESHOLD)
      {
        currentButtonStates[5] = LOW;
      }
      else
      {
        currentButtonStates[5] = HIGH;
      }

      if (pitchDifference < -ACCELEROMETER_THRESHOLD)
      {
        currentButtonStates[6] = LOW;
      }
      else
      {
        currentButtonStates[6] = HIGH;
      }

      prevPitch = accelerometer.pitch;
    }

    if (ENABLE_TILT)
    {
      uint16_t currentTilt = digitalRead(tilt);

      if (currentTilt == LOW)
      {
        currentButtonStates[5] = LOW;
      }
      else
      {
        currentButtonStates[5] = HIGH;
      }

      // if (delta < -TILT_THRESHOLD)
      // {
      //   currentButtonStates[6] = LOW;
      // }
      // else
      // {
      //   currentButtonStates[6] = HIGH;
      // }

      // prevPitch = currentTilt;
    }

    for (byte i = 0; i < no_buttons; i++)
    {
      int begin_physical = no_neck_buttons + no_gitadora_up_down - 1;

      // itearate through every input buttons
      if (i > begin_physical)
      {
        currentButtonStates[i] = digitalRead(physical_buttons[i - begin_physical - 1]);
      }

      if (currentButtonStates[i] != previousButtonStates[i])
      {
        if (currentButtonStates[i] == LOW)
          bleGamepad->press(i + 1);
        else
          bleGamepad->release(i + 1);
      }
    }

    if (currentButtonStates != previousButtonStates)
    {
      for (byte i = 0; i < numOfButtons; i++)
      {
        previousButtonStates[i] = currentButtonStates[i];
      }
      bleGamepad->sendReport();
    }

    if (SERIAL_EN)
    {
      loopCounter++;
      // Only print out the button state periodically, so polling stays fast
      if (loopCounter % 25 == 0)
      {
        printButtons(neckButtons);
      }
    }
  }
}