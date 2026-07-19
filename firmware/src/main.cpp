/*=====================================================================
 * Open Vega+ Emulator. Handheld ESP32 based hardware with
 * ZX Spectrum emulation
 *
 * (C) 2019 Alvaro Alea Fernandez
 *
 * This program is free software; redistributable under the terms of
 * the GNU General Public License Version 2
 *
 * Based on the Aspectrum emulator Copyright (c) 2000 Santiago Romero Iglesias
 * Use Adafruit's IL9341 and GFX libraries.
 * Compile as ESP32 Wrover Module
 *======================================================================
 */
#include "Serial.h"
#include <esp_err.h>
#include <map>
#include <vector>
#include <sstream>
#include "SPI.h"
#include "AudioOutput/I2SOutput.h"
#include "AudioOutput/PDMOutput.h"
#include "AudioOutput/DACOutput.h"
#include "AudioOutput/BuzzerOutput.h"
#include "Emulator/snaps.h"
#include "Emulator/spectrum.h"
#include "Files/Files.h"
#include "Files/Settings.h"
#include "Screens/NavigationStack.h"
#include "Screens/EmulatorScreen.h"
#include "Input/SerialKeyboard.h"
#include "Input/Nunchuck.h"
#include "Input/AdafruitSeeSaw.h"
#include "Input/TDeckKeyboard.h"
#ifdef CYD_BLUETOOTH_KEYBOARD
#include "Input/BluetoothKeyboard.h"
#endif
#include "TFT/TFTDisplay.h"
#include "TFT/ST7789.h"
#include "TFT/ILI9341.h"
#include "TFT/HDMIDisplay.h"
#ifdef TOUCH_KEYBOARD
#include "Input/TouchKeyboard.h"
#endif
#ifdef TOUCH_KEYBOARD_V2
#include "Input/TouchKeyboardV2.h"
#endif
#ifdef CYD_TOUCH_KEYBOARD
#include "Input/CydTouchKeyboard.h"
#include "Input/CydKeyboardTheme.h"
#include "Screens/CydCalibration.h"
#endif
#include "SerialInterface/PacketHandler.h"
#include "SerialInterface/SerialTransport.h"
#include "SerialInterface/Messages/GetVersion.h"
#include "SerialInterface/Messages/ListFolder.h"
#include "SerialInterface/Messages/WriteFile.h"
#include "SerialInterface/Messages/ReadFile.h"
#include "SerialInterface/Messages/DeleteFile.h"
#include "SerialInterface/Messages/MakeDirectory.h"
#include "SerialInterface/Messages/RenameFile.h"
#include "Screens/fonts/GillSans_15_vlw.h"
#ifdef CYD_TAPE_BUFFER_SIZE
#include "Screens/EmulatorScreen/GameLoader.h"
#endif
void SerialInterfaceTask(void *arg) {
  PacketHandler *packetHandler = (PacketHandler *) arg;
  while(true) {
    packetHandler->loop();
    vTaskDelay(1);
  }
}

void setup(void)
{
  #ifdef BOARD_POWERON
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  #endif
#ifdef CYD_SERIAL_TX_BUFFER
  Serial.setTxBufferSize(CYD_SERIAL_TX_BUFFER);
#else
  Serial.setTxBufferSize(20000);
#endif
#ifdef CYD_SERIAL_RX_BUFFER
  Serial.setRxBufferSize(CYD_SERIAL_RX_BUFFER);
#else
  Serial.setRxBufferSize(20000);
#endif
  Serial.begin(115200);
  #ifdef POWER_PIN
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, POWER_PIN_ON);
  vTaskDelay(100);
  #endif
  #ifdef TFT_MOSI
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = TFT_MOSI;
  buscfg.miso_io_num = TFT_MISO;
  buscfg.sclk_io_num = TFT_SCLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 65535;
  buscfg.flags = SPICOMMON_BUSFLAG_MASTER;
  buscfg.intr_flags = 0;
  esp_err_t spi2_err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  (void)spi2_err;
  #endif
  // Files
  SDCard *sdFileSystem = nullptr;
  #ifdef USE_SDCARD
    #ifdef USE_SDIO
      sdFileSystem = new SDCard(SDCard::DEFAULT_MOUNT_POINT, SD_CARD_CLK, SD_CARD_CMD, SD_CARD_D0, SD_CARD_D1, SD_CARD_D2, SD_CARD_D3);
      // TODO setupUSB(fileSystem);
    #else
      #ifdef SD_CARD_MISO
        sdFileSystem = new SDCard(SDCard::DEFAULT_MOUNT_POINT, SD_CARD_MISO, SD_CARD_MOSI, SD_CARD_CLK, SD_CARD_CS);
      #else
        // SD Card shares the SPI bus with the TFT
        sdFileSystem = new SDCard(SDCard::DEFAULT_MOUNT_POINT, SD_CARD_CS);
      #endif
    #endif
  #endif
  FilesImplementation<SDCard> *sdFiles = new FilesImplementation<SDCard>(sdFileSystem);
  FlashLittleFS *spiffsFileSystem = new FlashLittleFS(FlashLittleFS::DEFAULT_MOUNT_POINT);
  FilesImplementation<FlashLittleFS> *spiffsFiles = new FilesImplementation<FlashLittleFS>(spiffsFileSystem);

  IFiles *files = new UnifiedStorage(spiffsFiles, sdFiles);
  
  ISettings *settings = new Settings(spiffsFiles);

  #ifdef TFT_ST7789
  Display *tft = new ST7789(TFT_CS, TFT_DC, TFT_RST, TFT_BL, TFT_WIDTH, TFT_HEIGHT);
  #endif
  #ifdef TFT_ILI9341
  Display *tft = new ILI9341(TFT_CS, TFT_DC, TFT_RST, TFT_BL, TFT_WIDTH, TFT_HEIGHT);
  #endif
  #ifdef CYD_BOOT_STATUS
    const uint16_t bootBg = Display::color565(0, 60, 120);
    tft->fillScreen(bootBg);
    tft->loadFont(GillSans_15_vlw);
    tft->setTextColor(TFT_WHITE, bootBg);
    tft->drawCenterString("BLE minimal boot", tft->height() / 2 - 20);
    tft->drawCenterString("Display ready", tft->height() / 2 + 10);
    vTaskDelay(1500 / portTICK_PERIOD_MS);
  #endif
#ifdef CYD_TAPE_BUFFER_SIZE
  // After TFT DMA buffer (32 KiB); before emulator/serial fragment the heap.
  GameLoader::reserveTapeBuffer(CYD_TAPE_BUFFER_SIZE);
#endif
  HDMIDisplay *hdmiDisplay = nullptr; // new HDMIDisplay(GPIO_NUM_7);
  EmulatorScreen *emulatorScreen = nullptr;
  // navigation stack
  NavigationStack *navigationStack = new NavigationStack(tft, hdmiDisplay);
  // Audio output
  AudioOutput *audioOutput = nullptr;
#ifdef USE_DAC_AUDIO
  audioOutput = new DACOutput(I2S_NUM_0, settings);
#endif
#ifdef BUZZER_GPIO_NUM
  audioOutput = new BuzzerOutput(BUZZER_GPIO_NUM, settings);
#endif
#ifdef PDM_GPIO_NUM
  // i2s speaker pins
  i2s_pin_config_t i2s_speaker_pins = {
      .bck_io_num = I2S_PIN_NO_CHANGE,
      .ws_io_num = GPIO_NUM_0,
      .data_out_num = BUZZER_GPIO_NUM,
      .data_in_num = I2S_PIN_NO_CHANGE};
  audioOutput = new PDMOutput(I2S_NUM_0, i2s_speaker_pins, settings);
#endif
#ifdef I2S_SPEAKER_SERIAL_CLOCK
#ifdef SPK_MODE
  pinMode(SPK_MODE, OUTPUT);
  digitalWrite(SPK_MODE, HIGH);
#endif
  // i2s speaker pins
  i2s_pin_config_t i2s_speaker_pins = {
      .bck_io_num = I2S_SPEAKER_SERIAL_CLOCK,
      .ws_io_num = I2S_SPEAKER_LEFT_RIGHT_CLOCK,
      .data_out_num = I2S_SPEAKER_SERIAL_DATA,
      .data_in_num = I2S_PIN_NO_CHANGE};
  audioOutput = new I2SOutput(I2S_NUM_1, i2s_speaker_pins, settings);
#endif
#ifdef TOUCH_KEYBOARD
  TouchKeyboard *touchKeyboard = new TouchKeyboard(
      [&](SpecKeys key, bool down)
      {
        {
          navigationStack->updateKey(key, down);
        }
      });
  touchKeyboard->calibrate();
  touchKeyboard->start();
#endif
#ifdef TOUCH_KEYBOARD_V2
  TouchKeyboardV2 *touchKeyboard = new TouchKeyboardV2(
      [&](SpecKeys key, bool down) 
      { navigationStack->updateKey(key, down); },
      [&](SpecKeys key)
      { navigationStack->pressKey(key); });
  touchKeyboard->start();
#endif
#ifdef LILYGO_T_KEYBOARD
  TDeckKeyboard *tDeckKeyboard = new TDeckKeyboard([&](SpecKeys key, bool down)
                                                { navigationStack->updateKey(key, down); },
                                                [&](SpecKeys key)
                                                { navigationStack->pressKey(key); });
  tDeckKeyboard->start();
#endif
#ifdef CYD_BLUETOOTH_KEYBOARD
  bool bluetoothInputEnabled = false;
  BluetoothKeyboard *bluetoothKeyboard = new BluetoothKeyboard(
      [&](SpecKeys key, bool down) {
        if (!bluetoothInputEnabled)
        {
          return;
        }
#if defined(CYD_BLE_KEYPRESS_LOG) && CYD_BLE_KEYPRESS_LOG
        Serial.printf("BLE key event: key=%d state=%s\n", (int)key, down ? "down" : "up");
#endif
        navigationStack->updateKey(key, down);
      },
      [&](SpecKeys key) {
        if (!bluetoothInputEnabled)
        {
          return;
        }
#if defined(CYD_BLE_KEYPRESS_LOG) && CYD_BLE_KEYPRESS_LOG
        Serial.printf("BLE key pressed: key=%d\n", (int)key);
#endif
        if (key == SPECKEY_MENU)
        {
          const bool rightHanded = !settings->isCydRightHanded();
          settings->setCydRightHanded(rightHanded);
#ifdef CYD_TOUCH_KEYBOARD
          if (emulatorScreen != nullptr)
          {
            emulatorScreen->setCydHandedness(rightHanded);
          }
#endif
          Serial.printf("Toggled Cyd handedness to %s\n", rightHanded ? "right" : "left");
          return;
        }
        navigationStack->pressKey(key);
      });
#endif
#ifdef CYD_BLE_SCAN_ONLY
#ifdef CYD_BLUETOOTH_KEYBOARD
  tft->fillScreen(TFT_BLACK);
  tft->loadFont(GillSans_15_vlw);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->drawCenterString("BLE scan only", tft->height() / 2 - 20);
  tft->drawCenterString("Waiting for adverts", tft->height() / 2 + 10);
  while (true)
  {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
#else
  tft->fillScreen(TFT_RED);
  tft->loadFont(GillSans_15_vlw);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->drawCenterString("BLE scan only", tft->height() / 2 - 20);
  tft->drawCenterString("Bluetooth disabled", tft->height() / 2 + 10);
  while (true)
  {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
#endif
#endif
  if (audioOutput) {
    audioOutput->start(15625);
  }
  // create the directory structure
  if (!files->createDirectory("/snapshots"))
  {
    Serial.println("Failed to create /snapshots directory");
  }
#ifdef CYD_BLUETOOTH_KEYBOARD
  bluetoothKeyboard->start();
#ifdef CYD_BLE_CONNECT_BEFORE_EMULATOR
  const uint32_t bluetoothConnectTimeoutMs = CYD_BLE_CONNECT_TIMEOUT_MS;
  const uint32_t bluetoothStableConnectionMs = CYD_BLE_STABLE_CONNECTION_MS;
  auto waitForBluetoothReady = [&](const char *stageLabel) {
    const uint32_t bluetoothWaitStartMs = millis();
    uint32_t bluetoothReadySinceMs = 0;
    Serial.printf("Waiting for Bluetooth HID ready state %s (timeout=%u ms, stable=%u ms)\n",
                  stageLabel,
                  (unsigned)bluetoothConnectTimeoutMs,
                  (unsigned)bluetoothStableConnectionMs);

    while (true)
    {
      const uint32_t nowMs = millis();
      const bool ready = bluetoothKeyboard->isReady();
      if (ready)
      {
        if (bluetoothReadySinceMs == 0)
        {
          bluetoothReadySinceMs = nowMs;
          Serial.println("Bluetooth HID subscribed, verifying link stability...");
        }
        if ((nowMs - bluetoothReadySinceMs) >= bluetoothStableConnectionMs)
        {
          Serial.println("Bluetooth HID connection stable. Continuing startup.");
          return true;
        }
      }
      else
      {
        bluetoothReadySinceMs = 0;
      }

      if (bluetoothConnectTimeoutMs > 0 && (nowMs - bluetoothWaitStartMs) >= bluetoothConnectTimeoutMs)
      {
        Serial.println("Bluetooth keyboard wait timed out; continuing startup.");
        return false;
      }

      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
  };

  waitForBluetoothReady("before emulator init");
#else
  const uint32_t bluetoothBootWaitMs = 30000;
  const uint32_t bluetoothBootStartMs = millis();
  while (!bluetoothKeyboard->isConnected() && !bluetoothKeyboard->hasSeenCandidate() && (millis() - bluetoothBootStartMs) < bluetoothBootWaitMs)
  {
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  if (bluetoothKeyboard->isConnected())
  {
    Serial.println("Bluetooth keyboard connected during boot window");
  }
  else if (bluetoothKeyboard->hasSeenCandidate())
  {
    Serial.println("Bluetooth keyboard candidate seen during boot window");
  }
  else
  {
    Serial.println("No Bluetooth keyboard candidate seen during boot window; continuing startup");
  }
#endif
#endif
#ifdef CYD_TOUCH_KEYBOARD
  CydCalibration::runIfNeeded(*tft, *settings);
  cydKeyboardThemeInit(files);
#endif
  emulatorScreen = new EmulatorScreen(*tft, hdmiDisplay, audioOutput, files);
#ifdef CYD_TOUCH_KEYBOARD
  const bool cydRightHanded = settings->isCydRightHanded();
  emulatorScreen->setCydHandedness(cydRightHanded);
  CydTouchKeyboard *cydTouchKeyboard = new CydTouchKeyboard(
      [&](SpecKeys key, bool down) { navigationStack->updateKey(key, down); },
      cydRightHanded,
      [&](SpecKeys key) { navigationStack->pressKey(key); });
  navigationStack->setCydTouchKeyboard(cydTouchKeyboard);
  emulatorScreen->setCydTouchKeyboard(cydTouchKeyboard, settings);
  navigationStack->push(emulatorScreen);
  cydTouchKeyboard->start();
#else
  navigationStack->push(emulatorScreen);
#endif
#ifdef CYD_BLUETOOTH_KEYBOARD
#ifdef CYD_BLE_CONNECT_BEFORE_EMULATOR
  if (!bluetoothKeyboard->isReady())
  {
    Serial.println("Bluetooth HID link changed during emulator init; waiting for recovery...");
  }
  waitForBluetoothReady("after emulator init");
#endif
#endif
  emulatorScreen->run("", models_enum::SPECMDL_48K);

#ifdef CYD_BLUETOOTH_KEYBOARD
  bluetoothInputEnabled = true;
#endif

  // start off the keyboard and feed keys into the active scene
#ifdef CYD_SERIAL_KEYBOARD
  SerialKeyboard *keyboard = new SerialKeyboard([&](SpecKeys key, bool down)
                                                {
                                                  navigationStack->updateKey(key, down);
                                                  if (down)
                                                  {
                                                    navigationStack->pressKey(key);
                                                  }
                                                });
#endif

// start up the nunchuk controller and feed events into the active screen
#ifdef NUNCHUK_CLOCK
   Nunchuck *nunchuck = new Nunchuck([&](SpecKeys key, bool down)
                                    { navigationStack->updateKey(key, down); },
                                    [&](SpecKeys key)
                                    { navigationStack->pressKey(key); },
                                    NUNCHUK_CLOCK, NUNCHUK_DATA);
#endif
#ifdef SEESAW_CLOCK
  // TODO - this seems to hang the system
  // AdafruitSeeSaw *seeSaw = new AdafruitSeeSaw([&](SpecKeys key, bool down)
  //                                   { navigationStack->updateKey(key, down); },
  //                                   [&](SpecKeys key)
  //                                   { navigationStack->pressKey(key); });
  // seeSaw->begin(SEESAW_DATA, SEESAW_CLOCK);
#endif
#ifndef CYD_SERIAL_KEYBOARD
  SerialTransport *serialTransport = new SerialTransport();
  PacketHandler *packetHandler = new PacketHandler(*serialTransport);
  packetHandler->registerMessageHandler(new GetVersionMessageReciever(spiffsFiles, sdFiles, packetHandler), MessageId::GetVersionRequest);
  packetHandler->registerMessageHandler(new ListFolderMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::ListFolderRequest);
  packetHandler->registerMessageHandler(new WriteFileStartMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::WriteFileStartRequest);
  packetHandler->registerMessageHandler(new WriteFileDataMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::WriteFileDataRequest);
  packetHandler->registerMessageHandler(new WriteFileEndMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::WriteFileEndRequest);
  packetHandler->registerMessageHandler(new ReadFileMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::ReadFileRequest);
  packetHandler->registerMessageHandler(new DeleteFileMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::DeleteFileRequest);
  packetHandler->registerMessageHandler(new MakeDirectoryMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::MakeDirectoryRequest);
  packetHandler->registerMessageHandler(new RenameFileMessageReceiver(spiffsFiles, sdFiles, packetHandler), MessageId::RenameFileRequest);

  xTaskCreatePinnedToCore(
    SerialInterfaceTask,
    "SerialInterface",
#ifdef CYD_TOUCH_KEYBOARD
    8192,
#else
    16384,
#endif
    packetHandler,
    1,
    nullptr,
    1
  );
#endif

#ifndef CYD_NO_EMULATOR_MENU
  pinMode(0, INPUT_PULLUP);
  bool bootButtonWasPressed = false;
#endif
#ifdef CYD_BLUETOOTH_KEYBOARD
  bool waitingForBluetoothKeyboard = true;
#endif
  while (true)
  {
    vTaskDelay(20 / portTICK_PERIOD_MS);
#ifdef CYD_BLUETOOTH_KEYBOARD
    if (bluetoothKeyboard != nullptr && !bluetoothKeyboard->isReady())
    {
      if (!waitingForBluetoothKeyboard)
      {
        waitingForBluetoothKeyboard = true;
        Serial.println("Bluetooth keyboard not ready - continuing emulation and waiting for reconnect");
      }
    }
    else
    {
      if (waitingForBluetoothKeyboard)
      {
        waitingForBluetoothKeyboard = false;
        Serial.println("Bluetooth keyboard connected - keyboard input active");
      }
    }
    emulatorScreen->tickEmulation();
#else
    emulatorScreen->tickEmulation();
#endif
#ifdef CYD_TOUCH_KEYBOARD
    emulatorScreen->openMenuIfRequested();
    Screen *topScreen = navigationStack->getTop();
    if (topScreen != nullptr && topScreen->usesCydTouch())
    {
      topScreen->pollCydTouch();
    }
#endif
#ifndef CYD_NO_EMULATOR_MENU
    if (digitalRead(0) == LOW)
    {
      navigationStack->updateKey(SPECKEY_MENU, true);
      bootButtonWasPressed = true;
    }
    else if (bootButtonWasPressed)
    {
      navigationStack->updateKey(SPECKEY_MENU, false);
      navigationStack->pressKey(SPECKEY_MENU);
      bootButtonWasPressed = false;
    }
#endif
  }
}

unsigned long frame_millis;
void loop()
{
}
