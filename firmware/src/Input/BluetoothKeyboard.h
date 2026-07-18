#pragma once

#include <array>
#include <functional>
#include <vector>
#include "../Emulator/keyboard_defs.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

class BluetoothKeyboard {
private:
  using KeyEventType = std::function<void(SpecKeys keyCode, bool isPressed)>;
  using KeyPressedEventType = std::function<void(SpecKeys keyCode)>;

  KeyEventType m_keyEvent;
  KeyPressedEventType m_keyPressedEvent;

  bool m_started = false;
  bool m_connected = false;
  bool m_scanning = false;
  bool m_ready = false;
  NimBLEClient *m_client = nullptr;
  NimBLERemoteCharacteristic *m_inputReportCharacteristic = nullptr;
  std::array<uint8_t, 8> m_lastReport{};
  std::vector<SpecKeys> m_pressedKeys;

  static void onKeyboardNotify(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                               uint8_t *pData,
                               size_t length,
                               bool isNotify);
  void handleKeyboardReport(const uint8_t *data, size_t length);
  void emitKeyEvent(SpecKeys keyCode, bool isPressed);
  void setKeyPressedState(SpecKeys keyCode, bool isPressed);
  static SpecKeys mapHidUsageToSpecKey(uint8_t hidUsage);
  bool connectToKeyboardDevice();
  bool discoverKeyboardServices();

public:
  bool connectToKeyboardDeviceForScanCallback();
  BluetoothKeyboard(KeyEventType keyEvent, KeyPressedEventType keyPressedEvent);
  bool start();
  static void readKeyboardTask(void *pvParameters);
  void readKeyboard();
};
