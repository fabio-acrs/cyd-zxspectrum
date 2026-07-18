#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>
#include <NimBLEClient.h>
#include <NimBLERemoteService.h>
#include <NimBLERemoteCharacteristic.h>
#include <algorithm>
#include <array>
#include <string>
#include "BluetoothKeyboard.h"

namespace {
constexpr uint32_t kScanTimeMs = 5000;
constexpr uint16_t kHidServiceUuid = 0x1812;
constexpr uint16_t kInputReportUuid = 0x2A4D;
static BluetoothKeyboard *g_activeKeyboard = nullptr;

class KeyboardScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (advertisedDevice == nullptr || g_activeKeyboard == nullptr) {
      return;
    }

    if (!advertisedDevice->haveServiceUUID()) {
      return;
    }

    const NimBLEUUID serviceUuid = advertisedDevice->getServiceUUID();
    if (serviceUuid.equals(NimBLEUUID("1812")) || serviceUuid.equals(NimBLEUUID("00001812-0000-1000-8000-00805f9b34fb"))) {
      Serial.printf("Bluetooth HID keyboard advertising: %s\n", advertisedDevice->toString().c_str());
      NimBLEDevice::getScan()->stop();
      g_activeKeyboard->connectToKeyboardDeviceForScanCallback();
    }
  }
};

class KeyboardClientCallbacks : public NimBLEClientCallbacks {
public:
  void onConnect(NimBLEClient *pClient) override {
    Serial.printf("Bluetooth keyboard connected: %s\n", pClient->getPeerAddress().toString().c_str());
  }

  void onDisconnect(NimBLEClient *pClient, int reason) override {
    Serial.printf("Bluetooth keyboard disconnected: %s (%d)\n", pClient->getPeerAddress().toString().c_str(), reason);
  }
};

static KeyboardScanCallbacks g_scanCallbacks;
static KeyboardClientCallbacks g_clientCallbacks;

const std::array<SpecKeys, 256> &hidUsageToSpecKeys() {
  static const std::array<SpecKeys, 256> map = []() {
    std::array<SpecKeys, 256> values{};
    values.fill(SPECKEY_NONE);

    // Modifier bits are handled separately in the HID report.
    values[0x04] = SPECKEY_A;
    values[0x05] = SPECKEY_B;
    values[0x06] = SPECKEY_C;
    values[0x07] = SPECKEY_D;
    values[0x08] = SPECKEY_E;
    values[0x09] = SPECKEY_F;
    values[0x0A] = SPECKEY_G;
    values[0x0B] = SPECKEY_H;
    values[0x0C] = SPECKEY_I;
    values[0x0D] = SPECKEY_J;
    values[0x0E] = SPECKEY_K;
    values[0x0F] = SPECKEY_L;
    values[0x10] = SPECKEY_M;
    values[0x11] = SPECKEY_N;
    values[0x12] = SPECKEY_O;
    values[0x13] = SPECKEY_P;
    values[0x14] = SPECKEY_Q;
    values[0x15] = SPECKEY_R;
    values[0x16] = SPECKEY_S;
    values[0x17] = SPECKEY_T;
    values[0x18] = SPECKEY_U;
    values[0x19] = SPECKEY_V;
    values[0x1A] = SPECKEY_W;
    values[0x1B] = SPECKEY_X;
    values[0x1C] = SPECKEY_Y;
    values[0x1D] = SPECKEY_Z;

    values[0x1E] = SPECKEY_1;
    values[0x1F] = SPECKEY_2;
    values[0x20] = SPECKEY_3;
    values[0x21] = SPECKEY_4;
    values[0x22] = SPECKEY_5;
    values[0x23] = SPECKEY_6;
    values[0x24] = SPECKEY_7;
    values[0x25] = SPECKEY_8;
    values[0x26] = SPECKEY_9;
    values[0x27] = SPECKEY_0;

    values[0x28] = SPECKEY_ENTER;
    values[0x2C] = SPECKEY_SPACE;
    return values;
  }();

  return map;
}
}

BluetoothKeyboard::BluetoothKeyboard(KeyEventType keyEvent, KeyPressedEventType keyPressedEvent)
    : m_keyEvent(keyEvent), m_keyPressedEvent(keyPressedEvent) {
}

bool BluetoothKeyboard::start() {
  if (m_started) {
    return true;
  }

  m_started = true;
  g_activeKeyboard = this;
  NimBLEDevice::init("CYD BLE Keyboard");

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->start(kScanTimeMs, false);
  m_scanning = true;

  xTaskCreate(readKeyboardTask, "bluetoothKeyboardTask", 8192, this, 1, nullptr);
  return true;
}

void BluetoothKeyboard::readKeyboardTask(void *pvParameters) {
  auto *self = static_cast<BluetoothKeyboard *>(pvParameters);
  self->readKeyboard();
}

void BluetoothKeyboard::readKeyboard() {
  while (true) {
    if (!m_connected && m_scanning) {
      NimBLEDevice::getScan()->start(kScanTimeMs, false);
    }
    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}

bool BluetoothKeyboard::connectToKeyboardDeviceForScanCallback() {
  return connectToKeyboardDevice();
}

bool BluetoothKeyboard::connectToKeyboardDevice() {
  if (m_connected || m_client != nullptr) {
    return true;
  }

  m_client = NimBLEDevice::createClient();
  m_client->setClientCallbacks(&g_clientCallbacks, false);
  m_client->setConnectTimeout(5000);
  m_client->setConnectionParams(12, 12, 0, 150);

  NimBLEScan *scan = NimBLEDevice::getScan();
  NimBLEScanResults scanResults = scan->getResults();
  for (size_t index = 0; index < scanResults.getCount(); ++index) {
    const NimBLEAdvertisedDevice *device = scanResults.getDevice(index);
    if (device == nullptr || !device->haveServiceUUID()) {
      continue;
    }

    const NimBLEUUID serviceUuid = device->getServiceUUID();
    if (serviceUuid.equals(NimBLEUUID("1812")) || serviceUuid.equals(NimBLEUUID("00001812-0000-1000-8000-00805f9b34fb"))) {
      if (m_client->connect(device)) {
        m_connected = true;
        if (discoverKeyboardServices()) {
          Serial.println("Bluetooth HID keyboard connected and subscribed.");
          return true;
        }
        m_client->disconnect();
        return false;
      }
    }
  }

  Serial.println("Bluetooth keyboard scan finished without a usable HID service.");
  return false;
}

bool BluetoothKeyboard::discoverKeyboardServices() {
  if (m_client == nullptr || !m_client->isConnected()) {
    return false;
  }

  NimBLERemoteService *service = m_client->getService(NimBLEUUID("1812"));
  if (service == nullptr) {
    service = m_client->getService(NimBLEUUID("00001812-0000-1000-8000-00805f9b34fb"));
  }

  if (service == nullptr) {
    Serial.println("Bluetooth HID keyboard service not found.");
    return false;
  }

  NimBLERemoteCharacteristic *characteristic = service->getCharacteristic(NimBLEUUID("2A4D"));
  if (characteristic == nullptr) {
    Serial.println("Bluetooth HID input report characteristic not found.");
    return false;
  }

  if (!characteristic->canNotify()) {
    Serial.println("Bluetooth HID input report characteristic is not notify-enabled.");
    return false;
  }

  m_inputReportCharacteristic = characteristic;
  if (!characteristic->subscribe(true, BluetoothKeyboard::onKeyboardNotify)) {
    Serial.println("Failed to subscribe to the Bluetooth HID input report.");
    return false;
  }

  m_ready = true;
  return true;
}

void BluetoothKeyboard::onKeyboardNotify(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                                          uint8_t *pData,
                                          size_t length,
                                          bool isNotify) {
  if (g_activeKeyboard == nullptr || pData == nullptr || length == 0) {
    return;
  }

  g_activeKeyboard->handleKeyboardReport(pData, length);
}

void BluetoothKeyboard::handleKeyboardReport(const uint8_t *data, size_t length) {
  if (data == nullptr || length < 8) {
    return;
  }

  const uint8_t modifier = data[0];
  const bool shiftDown = (modifier & 0x02) != 0;
  const bool altDown = (modifier & 0x04) != 0;

  if (shiftDown) {
    emitKeyEvent(SPECKEY_SHIFT, true);
  }
  if (altDown) {
    emitKeyEvent(SPECKEY_SYMB, true);
  }

  std::array<uint8_t, 6> reportKeys{};
  for (size_t index = 0; index < 6; ++index) {
    reportKeys[index] = data[index + 2];
  }

  std::vector<SpecKeys> activeKeys;
  for (uint8_t usage : reportKeys) {
    if (usage == 0) {
      continue;
    }

    const SpecKeys mappedKey = mapHidUsageToSpecKey(usage);
    if (mappedKey != SPECKEY_NONE) {
      activeKeys.push_back(mappedKey);
    }
  }

  for (SpecKeys key : m_pressedKeys) {
    if (std::find(activeKeys.begin(), activeKeys.end(), key) == activeKeys.end()) {
      emitKeyEvent(key, false);
    }
  }

  for (SpecKeys key : activeKeys) {
    if (std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key) == m_pressedKeys.end()) {
      emitKeyEvent(key, true);
      m_pressedKeys.push_back(key);
    }
  }

  if (!shiftDown) {
    emitKeyEvent(SPECKEY_SHIFT, false);
  }
  if (!altDown) {
    emitKeyEvent(SPECKEY_SYMB, false);
  }

  if (m_pressedKeys.size() > 0) {
    auto newEnd = std::remove_if(m_pressedKeys.begin(), m_pressedKeys.end(), [&](SpecKeys key) {
      return std::find(activeKeys.begin(), activeKeys.end(), key) == activeKeys.end();
    });
    m_pressedKeys.erase(newEnd, m_pressedKeys.end());
  }
}

void BluetoothKeyboard::emitKeyEvent(SpecKeys keyCode, bool isPressed) {
  if (keyCode == SPECKEY_NONE) {
    return;
  }

  m_keyEvent(keyCode, isPressed);
  if (isPressed) {
    m_keyPressedEvent(keyCode);
  }
}

void BluetoothKeyboard::setKeyPressedState(SpecKeys keyCode, bool isPressed) {
  if (isPressed) {
    if (std::find(m_pressedKeys.begin(), m_pressedKeys.end(), keyCode) == m_pressedKeys.end()) {
      m_pressedKeys.push_back(keyCode);
    }
  } else {
    m_pressedKeys.erase(std::remove(m_pressedKeys.begin(), m_pressedKeys.end(), keyCode), m_pressedKeys.end());
  }
}

SpecKeys BluetoothKeyboard::mapHidUsageToSpecKey(uint8_t hidUsage) {
  return hidUsageToSpecKeys()[hidUsage];
}
