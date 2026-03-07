#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- BLE ----------------
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;

// Use the same UUIDs as your display device
#define SERVICE_UUID        "72e8ce72-67f1-4114-8412-1b0e1b35e0b1"
#define CHARACTERISTIC_UUID "59ce0b83-f1c9-4229-9227-c6bd61ca7797"

// ---------------- GSR ----------------
// Use A1 for analog input from Grove GSR signal pin
#define GSR_PIN A1

// Moving average filter settings
const int FILTER_SIZE = 10;
int gsrBuffer[FILTER_SIZE] = {0};
int bufferIndex = 0;
long bufferSum = 0;

int rawGsr = 0;
int filteredGsr = 0;
int lastFilteredGsr = 0;

// Output value sent over BLE (0-100)
int emotionValue = 0;

// Timing
unsigned long previousMillis = 0;
unsigned long sampleInterval = 300;  // default idle interval

// ---------------- Modes ----------------
enum DeviceMode {
  MODE_SLEEP,
  MODE_IDLE,
  MODE_ACTIVE
};

DeviceMode currentMode = MODE_SLEEP;

// Threshold for detecting stronger signal change
const int ACTIVE_DELTA_THRESHOLD = 8;

// These two values may need calibration for your own sensor
const int GSR_MIN = 300;
const int GSR_MAX = 800;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

// Moving average filter
int updateMovingAverage(int newValue) {
  bufferSum -= gsrBuffer[bufferIndex];
  gsrBuffer[bufferIndex] = newValue;
  bufferSum += gsrBuffer[bufferIndex];

  bufferIndex = (bufferIndex + 1) % FILTER_SIZE;

  return bufferSum / FILTER_SIZE;
}

// Update software mode based on connection + signal activity
void updateMode() {
  int delta = abs(filteredGsr - lastFilteredGsr);

  if (!deviceConnected) {
    currentMode = MODE_SLEEP;
    sampleInterval = 1000;   // slowest when disconnected
  } 
  else if (delta >= ACTIVE_DELTA_THRESHOLD) {
    currentMode = MODE_ACTIVE;
    sampleInterval = 100;    // fastest when signal changes more
  } 
  else {
    currentMode = MODE_IDLE;
    sampleInterval = 300;    // moderate update rate
  }
}

const char* modeToString(DeviceMode mode) {
  switch (mode) {
    case MODE_SLEEP: return "SLEEP";
    case MODE_IDLE:  return "IDLE";
    case MODE_ACTIVE:return "ACTIVE";
    default:         return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting BLE GSR sensing device...");

  pinMode(GSR_PIN, INPUT);

  // Initialize filter buffer with first reading
  int initial = analogRead(GSR_PIN);
  for (int i = 0; i < FILTER_SIZE; i++) {
    gsrBuffer[i] = initial;
    bufferSum += initial;
  }
  filteredGsr = initial;
  lastFilteredGsr = initial;

  BLEDevice::init("EmoSense");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());

  uint8_t initValue = 0;
  pCharacteristic->setValue(&initValue, 1);

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started.");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= sampleInterval) {
    previousMillis = currentMillis;

    // Read raw sensor value
    rawGsr = analogRead(GSR_PIN);

    // Store previous filtered value before updating
    lastFilteredGsr = filteredGsr;

    // DSP: moving average filter
    filteredGsr = updateMovingAverage(rawGsr);

    // Map filtered GSR to 0-100
    emotionValue = map(filteredGsr, GSR_MIN, GSR_MAX, 0, 100);
    emotionValue = constrain(emotionValue, 0, 100);

    // Update state machine
    updateMode();

    // Print debug info
    Serial.print("Raw: ");
    Serial.print(rawGsr);
    Serial.print(" | Filtered: ");
    Serial.print(filteredGsr);
    Serial.print(" | Emotion: ");
    Serial.print(emotionValue);
    Serial.print(" | Mode: ");
    Serial.println(modeToString(currentMode));

    // Send only when connected
    if (deviceConnected) {
      uint8_t data = emotionValue;
      pCharacteristic->setValue(&data, 1);
      pCharacteristic->notify();

      Serial.print("Notify value: ");
      Serial.println(emotionValue);
    }
  }

  // Handle reconnection advertising
  if (!deviceConnected && oldDeviceConnected) {
    delay(200);
    pServer->startAdvertising();
    Serial.println("Restart advertising");
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("Client connected");
    oldDeviceConnected = deviceConnected;
  }
}