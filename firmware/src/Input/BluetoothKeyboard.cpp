#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>
#include <NimBLEClient.h>
#include <NimBLERemoteService.h>
#include <NimBLERemoteCharacteristic.h>
#include <esp_bt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include "BluetoothKeyboard.h"

namespace {
constexpr uint32_t kScanTimeMs = 10000;
constexpr uint16_t kHidServiceUuid = 0x1812;
constexpr uint16_t kInputReportUuid = 0x2A4D;
static BluetoothKeyboard *g_activeKeyboard = nullptr;

bool isHidServiceUuid(const NimBLEUUID &serviceUuid) {
  return serviceUuid.equals(NimBLEUUID("1812")) ||
         serviceUuid.equals(NimBLEUUID("00001812-0000-1000-8000-00805f9b34fb"));
}

std::string uppercaseCopy(const std::string &value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

std::string lowercaseCopy(const std::string &value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

bool isAllowedKeyboardAddress(const NimBLEAdvertisedDevice *advertisedDevice) {
  if (advertisedDevice == nullptr) {
    return false;
  }
#ifdef CYD_BLE_KEYBOARD_MAC
  const std::string target = uppercaseCopy(CYD_BLE_KEYBOARD_MAC);
  const std::string address = uppercaseCopy(advertisedDevice->getAddress().toString());
  return address == target;
#else
  return true;
#endif
}

bool isAllowedKeyboardName(const NimBLEAdvertisedDevice *advertisedDevice) {
#ifdef CYD_BLE_KEYBOARD_NAME
  if (advertisedDevice == nullptr || !advertisedDevice->haveName()) {
    return false;
  }
  return lowercaseCopy(advertisedDevice->getName()) == lowercaseCopy(CYD_BLE_KEYBOARD_NAME);
#else
  (void)advertisedDevice;
  return true;
#endif
}

const char *targetKeyboardLabel() {
#ifdef CYD_BLE_KEYBOARD_NAME
  return CYD_BLE_KEYBOARD_NAME;
#elif defined(CYD_BLE_KEYBOARD_MAC)
  return CYD_BLE_KEYBOARD_MAC;
#else
  return "any HID keyboard";
#endif
}

bool looksLikeKeyboardAdvert(const NimBLEAdvertisedDevice *advertisedDevice) {
  if (advertisedDevice == nullptr) {
    return false;
  }
  if (advertisedDevice->haveServiceUUID() && isHidServiceUuid(advertisedDevice->getServiceUUID())) {
    return true;
  }
#if defined(CYD_BLE_KEYBOARD_MAC) || defined(CYD_BLE_KEYBOARD_NAME)
  return isAllowedKeyboardAddress(advertisedDevice) && isAllowedKeyboardName(advertisedDevice);
#else
  if (advertisedDevice->haveName()) {
    const std::string name = lowercaseCopy(advertisedDevice->getName());
    return name.find("key") != std::string::npos ||
           name.find("kbd") != std::string::npos ||
           name.find("keyboard") != std::string::npos;
  }
  return false;
#endif
}

class KeyboardScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (advertisedDevice == nullptr || g_activeKeyboard == nullptr) {
      return;
    }

#if defined(CYD_BLE_SCAN_LOG_ALL) && CYD_BLE_SCAN_LOG_ALL
    const std::string addr = advertisedDevice->getAddress().toString();
    const std::string name = advertisedDevice->haveName() ? advertisedDevice->getName() : "";
    Serial.printf("BLE scan seen addr=%s name=%s hasSvc=%d\n",
                  addr.c_str(),
                  name.c_str(),
                  advertisedDevice->haveServiceUUID() ? 1 : 0);
#endif

    if (!isAllowedKeyboardAddress(advertisedDevice) || !isAllowedKeyboardName(advertisedDevice)) {
      return;
    }

    if (looksLikeKeyboardAdvert(advertisedDevice)) {
      Serial.printf("Bluetooth keyboard candidate: %s\n", advertisedDevice->toString().c_str());
      g_activeKeyboard->queueCandidateAddress(advertisedDevice->getAddress().toString());
      NimBLEDevice::getScan()->stop();
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
    if (g_activeKeyboard != nullptr) {
      g_activeKeyboard->handleDisconnected();
    }
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
  m_pressedKeys.reserve(8);
}

void BluetoothKeyboard::handleDisconnected() {
  m_connected = false;
  m_ready = false;
  m_inputReportCharacteristic = nullptr;
  m_pressedKeys.clear();
  m_backspacePressed = false;
  if (m_client != nullptr && !m_client->isConnected()) {
    NimBLEDevice::deleteClient(m_client);
    m_client = nullptr;
  }
}

void BluetoothKeyboard::queueCandidateAddress(const std::string &address) {
  if (address.empty()) {
    return;
  }

  if (m_hasPendingCandidate && m_pendingCandidateAddress == address) {
    return;
  }

  m_seenCandidate = true;
  m_pendingCandidateAddress = address;
  m_hasPendingCandidate = true;
  Serial.printf("Queued Bluetooth keyboard candidate: %s\n", m_pendingCandidateAddress.c_str());
}

bool BluetoothKeyboard::start() {
  if (m_started) {
    return true;
  }

  m_started = true;
  m_connected = false;
  m_ready = false;
  g_activeKeyboard = this;
  Serial.printf("Bluetooth keyboard target: %s\n", targetKeyboardLabel());

  // Free Classic BT memory; this firmware uses BLE only.
  const esp_err_t btMemRelease = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (btMemRelease != ESP_OK && btMemRelease != ESP_ERR_INVALID_STATE) {
    Serial.printf("Classic BT memory release failed: %d\n", (int)btMemRelease);
  }

  NimBLEDevice::init("CYD BLE Keyboard");

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks);
  scan->setActiveScan(true);
  scan->setDuplicateFilter(false);
  scan->setInterval(160);
  scan->setWindow(160);
  Serial.printf("Bluetooth scan config: interval=%u window=%u durationMs=%u\n",
                160u, 160u, (unsigned)kScanTimeMs);
  const bool scanStarted = scan->start(kScanTimeMs, false);
  if (!scanStarted) {
    Serial.printf("Bluetooth scan start failed (free heap=%u)\n", (unsigned)ESP.getFreeHeap());
    m_scanning = true;
    m_scanCycleActive = false;
  } else {
    m_scanning = true;
    m_scanCycleActive = true;
  }

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
      NimBLEScan *scan = NimBLEDevice::getScan();
      if (scan->isScanning()) {
        m_scanCycleActive = true;
      } else {
        if (m_scanCycleActive) {
          m_scanCycleActive = false;
          Serial.println("Bluetooth keyboard scan cycle complete");
          connectToKeyboardDevice();
          scan->clearResults();
        }
        if (!m_connected) {
          Serial.println("Bluetooth keyboard scan cycle start");
          const bool scanStarted = scan->start(kScanTimeMs, false);
          if (!scanStarted) {
            Serial.printf("Bluetooth scan restart failed (free heap=%u)\n", (unsigned)ESP.getFreeHeap());
            m_scanCycleActive = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
          } else {
            m_scanCycleActive = true;
          }
        }
      }
    }
    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}

bool BluetoothKeyboard::connectToKeyboardDeviceForScanCallback() {
  if (!m_hasPendingCandidate || m_pendingCandidateAddress.empty()) {
    return false;
  }

  Serial.printf("Processing queued Bluetooth keyboard candidate: %s\n", m_pendingCandidateAddress.c_str());
  return connectToKeyboardDevice();
}

bool BluetoothKeyboard::connectToKeyboardDevice() {
  if (m_connected && m_ready) {
    return true;
  }

  if (m_client != nullptr) {
    if (m_client->isConnected()) {
      return m_ready;
    }
    NimBLEDevice::deleteClient(m_client);
    m_client = nullptr;
  }

  m_client = NimBLEDevice::createClient();
  m_client->setClientCallbacks(&g_clientCallbacks, false);
  m_client->setConnectTimeout(5000);
  m_client->setConnectionParams(12, 12, 0, 150);

  NimBLEScan *scan = NimBLEDevice::getScan();
  NimBLEScanResults scanResults = scan->getResults();
  Serial.printf("Bluetooth scan results: %u device(s)\n", (unsigned)scanResults.getCount());

  auto tryConnectDevice = [&](const NimBLEAdvertisedDevice *device) -> bool {
    if (device == nullptr || !isAllowedKeyboardAddress(device) || !isAllowedKeyboardName(device)) {
      return false;
    }

    if (!looksLikeKeyboardAdvert(device)) {
      return false;
    }

    Serial.printf("Attempting Bluetooth keyboard connect: %s\n", device->toString().c_str());
    if (!m_client->connect(device)) {
      Serial.println("Bluetooth keyboard connect attempt failed.");
      return false;
    }

    m_connected = true;
    m_ready = false;
    if (discoverKeyboardServices()) {
      Serial.println("Bluetooth HID keyboard connected and subscribed.");
      return true;
    }

    Serial.println("Bluetooth keyboard connected but HID setup failed; disconnecting.");
    m_client->disconnect();
    m_connected = false;
    m_ready = false;
    return false;
  };

  if (m_hasPendingCandidate && !m_pendingCandidateAddress.empty()) {
    const std::string pendingUpper = uppercaseCopy(m_pendingCandidateAddress);
    for (size_t index = 0; index < scanResults.getCount(); ++index) {
      const NimBLEAdvertisedDevice *device = scanResults.getDevice(index);
      if (device == nullptr) {
        continue;
      }

      const std::string deviceUpper = uppercaseCopy(device->getAddress().toString());
      if (deviceUpper != pendingUpper) {
        continue;
      }

      const std::string pendingAddress = m_pendingCandidateAddress;
      m_pendingCandidateAddress.clear();
      m_hasPendingCandidate = false;

      const bool connected = tryConnectDevice(device);
      if (!connected) {
        Serial.printf("Bluetooth keyboard connect failed for queued candidate: %s\n", pendingAddress.c_str());
      }
      return connected;
    }

    Serial.printf("Queued Bluetooth keyboard candidate not found in scan snapshot: %s\n", m_pendingCandidateAddress.c_str());
    m_pendingCandidateAddress.clear();
    m_hasPendingCandidate = false;
  }

  for (size_t index = 0; index < scanResults.getCount(); ++index) {
    const NimBLEAdvertisedDevice *device = scanResults.getDevice(index);
    if (tryConnectDevice(device)) {
      m_pendingCandidateAddress.clear();
      m_hasPendingCandidate = false;
      return true;
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

  const NimBLEUUID inputReportUuid("2A4D");
  const auto &characteristics = service->getCharacteristics(true);
  size_t subscribedCount = 0;

  for (auto *characteristic : characteristics) {
    if (characteristic == nullptr) {
      continue;
    }
    if (!characteristic->getUUID().equals(inputReportUuid)) {
      continue;
    }
    if (!characteristic->canNotify()) {
      continue;
    }

    if (m_inputReportCharacteristic == nullptr) {
      m_inputReportCharacteristic = characteristic;
    }

    if (characteristic->subscribe(true, BluetoothKeyboard::onKeyboardNotify)) {
      subscribedCount++;
    }
  }

  if (subscribedCount == 0) {
    Serial.println("Failed to subscribe to any Bluetooth HID input report characteristic.");
    return false;
  }

  Serial.printf("Subscribed to %u HID input report characteristic(s).\n", (unsigned)subscribedCount);

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

  // Some BLE keyboards prepend a report ID byte. Use the last 8 bytes as
  // keyboard payload whenever possible.
  size_t payloadOffset = 0;
  if (length >= 9) {
    payloadOffset = length - 8;
  }

#if defined(CYD_BLE_KEYPRESS_LOG) && CYD_BLE_KEYPRESS_LOG
  Serial.printf("BLE report len=%u offset=%u\n", (unsigned)length, (unsigned)payloadOffset);
#endif

  const uint8_t modifier = data[payloadOffset + 0];
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
    reportKeys[index] = data[payloadOffset + index + 2];
  }

  std::array<SpecKeys, 8> activeKeys{};
  size_t activeKeyCount = 0;
  auto hasActiveKey = [&](SpecKeys key) {
    for (size_t index = 0; index < activeKeyCount; ++index) {
      if (activeKeys[index] == key) {
        return true;
      }
    }
    return false;
  };
  auto addActiveKey = [&](SpecKeys key) {
    if (key == SPECKEY_NONE || hasActiveKey(key) || activeKeyCount >= activeKeys.size()) {
      return;
    }
    activeKeys[activeKeyCount++] = key;
  };

  bool syntheticShiftDown = false;
  bool backspaceDown = false;
  for (uint8_t usage : reportKeys) {
    if (usage == 0) {
      continue;
    }

    // HID Backspace -> Spectrum DELETE (CAPS SHIFT + 0)
    if (usage == 0x2A) {
      syntheticShiftDown = true;
      backspaceDown = true;
      addActiveKey(SPECKEY_SHIFT);
      addActiveKey(SPECKEY_0);
      continue;
    }

    const SpecKeys mappedKey = mapHidUsageToSpecKey(usage);
    addActiveKey(mappedKey);
  }

  for (SpecKeys key : m_pressedKeys) {
    if (!hasActiveKey(key)) {
      emitKeyEvent(key, false);
    }
  }

  for (size_t index = 0; index < activeKeyCount; ++index) {
    const SpecKeys key = activeKeys[index];
    if (std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key) == m_pressedKeys.end()) {
      emitKeyEvent(key, true);
    }
  }

  if (!shiftDown && !syntheticShiftDown) {
    emitKeyEvent(SPECKEY_SHIFT, false);
  }
  if (!altDown) {
    emitKeyEvent(SPECKEY_SYMB, false);
  }

  // Touch keyboard emits pseudo SPECKEY_DEL from SHIFT+0; replicate that for BLE.
  if (backspaceDown && !m_backspacePressed) {
    m_keyPressedEvent(SPECKEY_DEL);
    m_backspacePressed = true;
  } else if (!backspaceDown) {
    m_backspacePressed = false;
  }

  m_pressedKeys.clear();
  for (size_t index = 0; index < activeKeyCount; ++index) {
    m_pressedKeys.push_back(activeKeys[index]);
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
