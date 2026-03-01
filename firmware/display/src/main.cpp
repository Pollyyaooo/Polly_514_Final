#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// 你可以把服务UUID和特征UUID替换为你的服务器的UUID
static BLEUUID serviceUUID("72e8ce72-67f1-4114-8412-1b0e1b35e0b1");
static BLEUUID charUUID("59ce0b83-f1c9-4229-9227-c6bd61ca7797");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

// 线序定义
#define A1 9
#define A2 10
#define B1 8
#define B2 5

int stepIndex = 0;

// 步进电机的步进序列（半步驱动）
const uint8_t seq[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1}
};

// 当前步进位置
int currentStep = 0;

// 目标角度（由 BLE 传入）
volatile int targetAngle = 0;
volatile bool needMove = false;

// 通知回调函数
static void notifyCallback(
  BLERemoteCharacteristic*,
  uint8_t* pData,
  size_t length,
  bool)
{
    if (length == 1) {
        uint8_t value = pData[0];

        // 映射值到 0-180 度
        int angle = map(value, 0, 100, 0, 180);

        Serial.print("Received: ");
        Serial.println(value);
        Serial.print("Move to angle: ");
        Serial.println(angle);

        targetAngle = angle;
        needMove = true;  // 设置标志，表示需要开始移动电机
    }
}

// 电机步进函数
void stepMotor(int dir)
{
  stepIndex = (stepIndex + dir + 4) % 4;  // 确保步进序列循环

  digitalWrite(A1, seq[stepIndex][0]);
  digitalWrite(A2, seq[stepIndex][1]);
  digitalWrite(B1, seq[stepIndex][2]);
  digitalWrite(B2, seq[stepIndex][3]);

  delay(6);  // 控制步进速度
}

// 电机移动到指定角度
void moveToAngle(int angle)
{
  // 将角度映射到步进
  int steps = map(angle, 0, 180, 0, 2048);  // 假设2048步为一圈
  int diff = steps - currentStep;

  // 计算步进方向
  int dir = (diff > 0) ? 1 : -1;

  // 移动电机
  for (int i = 0; i < abs(diff); i++) {
    stepMotor(dir);
  }

  currentStep = steps;  // 更新当前步进位置
}

// BLE 客户端回调函数
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

// BLE 扫描回调函数
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // 只连接包含目标服务UUID的设备
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// 连接到 BLE 服务器
bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  bool connectionResult = pClient->connect(myDevice);
  if (!connectionResult) {
    Serial.println(" - Failed to connect to server");
    return false;
  }

  Serial.println(" - Connected to server");
  pClient->setMTU(517); // 设置客户端请求最大MTU

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }

  connected = true;
  return true;
}

// 扫描设备并连接
void checkServerStatus() {
  Serial.println("Scanning for server...");
  BLEDevice::getScan()->start(5, false);

  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Connected to BLE Server.");
    } else {
      Serial.println("Failed to connect.");
    }
    doConnect = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
}

void loop() {
  if (!connected) {
    checkServerStatus();
  }

  if (needMove) {
    needMove = false;
    moveToAngle(targetAngle);
  }

  delay(10);
}