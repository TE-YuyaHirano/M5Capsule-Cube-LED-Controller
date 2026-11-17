#include <Adafruit_NeoPixel.h>
#include <driver/gpio.h>

// M5 Capsule HY2.0-4P (PORT.CUSTOM)
constexpr uint8_t DIN1_PIN = 13;
constexpr uint8_t DIN2_PIN = 15;
constexpr uint8_t WAKE_BUTTON_PIN = 42;
constexpr uint16_t DIN1_LED_COUNT = 16;
constexpr uint16_t DIN2_LED_COUNT = 25;
constexpr uint8_t PRESET_COUNT = 7;
constexpr uint8_t DEFAULT_BRIGHTNESS = 40;
constexpr uint16_t DEBOUNCE_MS = 35;
constexpr size_t MAX_COMMAND_LENGTH = 240;

constexpr uint16_t EDGE_MASKS[] = {0xFFFF, 0xBBBB, 0x5555};
constexpr uint8_t LEFT_FORWARD[] = {8, 7, 6, 5, 4};       // D9 -> D5
constexpr uint8_t RIGHT_FORWARD[] = {12, 13, 14, 15, 0}; // D13 -> D1
constexpr uint8_t FRONT_EDGE[] = {0, 1, 2, 3, 4};        // D1 -> D5
constexpr uint8_t RIGHT_FRONT[] = {15, 0, 1};            // D16, D1, D2
constexpr uint8_t LEFT_FRONT[] = {3, 4, 5};              // D4, D5, D6

Adafruit_NeoPixel ledsDin1(DIN1_LED_COUNT, DIN1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledsDin2(DIN2_LED_COUNT, DIN2_PIN, NEO_GRB + NEO_KHZ800);

enum class ControlMode : uint8_t { Preset, Manual };
ControlMode controlMode = ControlMode::Preset;

uint8_t currentPreset = 0;
uint8_t presetBrightness = DEFAULT_BRIGHTNESS;
uint8_t presetSpeeds[PRESET_COUNT] = {6, 6, 7, 5, 5, 7, 1};
uint8_t edgeLayout = 0;
uint32_t animationFrame = 0;
uint32_t lastFrameMs = 0;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
bool presetPaused = false;
uint32_t buttonChangedMs = 0;
String commandBuffer;

uint32_t manualMask1 = 0;
uint32_t manualColor1 = 0x008CFF;
uint8_t manualBrightness1 = 40;

// Keep the built-in buzzer silent and assert the Capsule power-hold signal.
extern "C" void initVariant(void) {
  gpio_set_level(GPIO_NUM_2, 0);
  gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_46, 1);
  gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);
}

bool edgeEnabled(uint8_t index) {
  return EDGE_MASKS[edgeLayout] & (1U << index);
}

void clearBoth() {
  ledsDin1.clear();
  ledsDin2.clear();
}

void showBoth() {
  ledsDin1.show();
  ledsDin2.show();
}

void allOff() {
  clearBoth();
  showBoth();
}

uint32_t rgbColor(Adafruit_NeoPixel &strip, uint32_t rgb) {
  return strip.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

uint8_t collectActive(const uint8_t *source, uint8_t length, uint8_t *target) {
  uint8_t count = 0;
  for (uint8_t index = 0; index < length; ++index) {
    if (edgeEnabled(source[index])) target[count++] = source[index];
  }
  return count;
}

void drawFlowSide(const uint8_t *source, bool reverse) {
  uint8_t active[5];
  const uint8_t count = collectActive(source, 5, active);
  if (count == 0) return;
  uint8_t position = animationFrame % count;
  if (reverse) position = count - 1 - position;
  ledsDin1.setPixelColor(active[position], ledsDin1.Color(0, 128, 255));
}

void showSideFlow(bool reverse) {
  clearBoth();
  drawFlowSide(LEFT_FORWARD, reverse);
  drawFlowSide(RIGHT_FORWARD, reverse);
  showBoth();
}

uint8_t bouncePosition(uint32_t step, uint8_t count) {
  if (count < 2) return 0;
  const uint8_t phase = step % (count * 2 - 2);
  return phase < count ? phase : count * 2 - 2 - phase;
}

void showKnightRider() {
  clearBoth();
  uint8_t active[5];
  const uint8_t count = collectActive(FRONT_EDGE, 5, active);
  if (count > 0) {
    const uint8_t head = bouncePosition(animationFrame, count);
    const uint8_t tail = bouncePosition(animationFrame == 0 ? 0 : animationFrame - 1, count);
    ledsDin1.setPixelColor(active[tail], ledsDin1.Color(90, 0, 0));
    ledsDin1.setPixelColor(active[head], ledsDin1.Color(255, 0, 0));
  }
  showBoth();
}

void showIndicator(bool right) {
  clearBoth();
  if ((animationFrame & 1U) == 0) {
    const uint8_t *leds = right ? RIGHT_FRONT : LEFT_FRONT;
    for (uint8_t index = 0; index < 3; ++index) {
      if (edgeEnabled(leds[index])) {
        ledsDin1.setPixelColor(leds[index], ledsDin1.Color(255, 105, 0));
      }
    }
  }
  showBoth();
}

void showRedPatrol() {
  clearBoth();
  uint8_t active[DIN1_LED_COUNT];
  uint8_t count = 0;
  for (uint8_t index = 0; index < DIN1_LED_COUNT; ++index) {
    if (edgeEnabled(index)) active[count++] = index;
  }
  static constexpr uint8_t RED_LEVELS[] = {255, 170, 90, 35};
  const uint8_t tailCount = count < 4 ? count : 4;
  for (uint8_t tail = 0; tail < tailCount; ++tail) {
    const uint8_t position = (animationFrame + count - tail) % count;
    ledsDin1.setPixelColor(active[position], ledsDin1.Color(RED_LEVELS[tail], 0, 0));
  }
  showBoth();
}

void showEdgeAllOn() {
  clearBoth();
  for (uint8_t index = 0; index < DIN1_LED_COUNT; ++index) {
    if (edgeEnabled(index)) ledsDin1.setPixelColor(index, ledsDin1.Color(255, 255, 255));
  }
  showBoth();
}

void selectPreset(uint8_t preset) {
  controlMode = ControlMode::Preset;
  currentPreset = preset % PRESET_COUNT;
  presetPaused = false;
  animationFrame = 0;
  lastFrameMs = 0;
  ledsDin1.setBrightness(presetBrightness);
  ledsDin2.setBrightness(presetBrightness);
  allOff();
  if (currentPreset == 6) showEdgeAllOn();
}

void showManualFrame() {
  controlMode = ControlMode::Manual;
  ledsDin1.setBrightness(manualBrightness1);
  ledsDin2.setBrightness(1);
  clearBoth();
  const uint32_t color = rgbColor(ledsDin1, manualColor1);
  const uint32_t enabledMask = manualMask1 & EDGE_MASKS[edgeLayout];
  for (uint8_t index = 0; index < DIN1_LED_COUNT; ++index) {
    if (enabledMask & (1UL << index)) ledsDin1.setPixelColor(index, color);
  }
  showBoth();
}

uint32_t readUnsignedField(const String &json, const char *name, uint32_t fallback) {
  const String key = String('"') + name + "\":";
  const int keyPosition = json.indexOf(key);
  if (keyPosition < 0) return fallback;
  const char *number = json.c_str() + keyPosition + key.length();
  char *end = nullptr;
  const unsigned long value = strtoul(number, &end, 10);
  return end == number ? fallback : static_cast<uint32_t>(value);
}

String readStringField(const String &json, const char *name, const String &fallback) {
  const String key = String('"') + name + "\":\"";
  const int keyPosition = json.indexOf(key);
  if (keyPosition < 0) return fallback;
  const int valueStart = keyPosition + key.length();
  const int valueEnd = json.indexOf('"', valueStart);
  return valueEnd < 0 ? fallback : json.substring(valueStart, valueEnd);
}

uint32_t readHexColor(const String &value, uint32_t fallback) {
  if (value.length() != 7 || value[0] != '#') return fallback;
  char *end = nullptr;
  const unsigned long color = strtoul(value.c_str() + 1, &end, 16);
  return end == value.c_str() + 7 ? static_cast<uint32_t>(color) : fallback;
}

void handleCommand(const String &json) {
  const String command = readStringField(json, "cmd", "");
  if (command == "preset") {
    const uint32_t id = readUnsignedField(json, "id", 1);
    presetBrightness = constrain(readUnsignedField(json, "brightness", presetBrightness), 1UL, 255UL);
    edgeLayout = constrain(readUnsignedField(json, "layout", edgeLayout), 0UL, 2UL);
    if (id >= 1 && id <= PRESET_COUNT) {
      const uint8_t preset = id - 1;
      presetSpeeds[preset] = constrain(readUnsignedField(json, "speed", presetSpeeds[preset]), 1UL, 10UL);
      selectPreset(preset);
    }
    return;
  }
  if (command == "layout") {
    edgeLayout = constrain(readUnsignedField(json, "value", edgeLayout), 0UL, 2UL);
    if (controlMode == ControlMode::Preset) selectPreset(currentPreset);
    else showManualFrame();
    return;
  }
  if (command == "brightness") {
    presetBrightness = constrain(readUnsignedField(json, "value", presetBrightness), 1UL, 255UL);
    if (controlMode == ControlMode::Preset) {
      ledsDin1.setBrightness(presetBrightness);
      ledsDin1.show();
      lastFrameMs = 0;
    }
    return;
  }
  if (command == "speed") {
    if (controlMode == ControlMode::Preset && currentPreset < PRESET_COUNT - 1) {
      presetSpeeds[currentPreset] = constrain(
          readUnsignedField(json, "value", presetSpeeds[currentPreset]), 1UL, 10UL);
      lastFrameMs = 0;
    }
    return;
  }
  if (command == "pause") {
    presetPaused = readUnsignedField(json, "value", 0) != 0;
    if (!presetPaused) lastFrameMs = 0;
    return;
  }
  if (command == "frame") {
    manualMask1 = readUnsignedField(json, "m1", manualMask1) & 0xFFFFUL;
    manualColor1 = readHexColor(readStringField(json, "c1", ""), manualColor1);
    manualBrightness1 = constrain(readUnsignedField(json, "b1", manualBrightness1), 1UL, 255UL);
    showManualFrame();
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\n') {
      commandBuffer.trim();
      if (!commandBuffer.isEmpty()) handleCommand(commandBuffer);
      commandBuffer = "";
    } else if (incoming != '\r') {
      if (commandBuffer.length() < MAX_COMMAND_LENGTH) commandBuffer += incoming;
      else commandBuffer = "";
    }
  }
}

void updateWakeButton(uint32_t now) {
  const bool reading = digitalRead(WAKE_BUTTON_PIN);
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    buttonChangedMs = now;
  }
  if ((now - buttonChangedMs >= DEBOUNCE_MS) && (reading != stableButtonState)) {
    stableButtonState = reading;
    if (stableButtonState == LOW) selectPreset(currentPreset + 1);
  }
}

uint16_t presetInterval() {
  static constexpr uint16_t BASE[] = {140, 140, 85, 320, 320, 75};
  static constexpr uint8_t DEFAULT_SPEED[] = {6, 6, 7, 5, 5, 7};
  static constexpr uint16_t MIN_INTERVAL[] = {55, 55, 40, 160, 160, 35};
  static constexpr uint16_t MAX_INTERVAL[] = {600, 600, 500, 1200, 1200, 500};
  const uint8_t speed = presetSpeeds[currentPreset];
  uint16_t interval = static_cast<uint32_t>(BASE[currentPreset]) *
                      DEFAULT_SPEED[currentPreset] / speed;
  if (interval < MIN_INTERVAL[currentPreset]) interval = MIN_INTERVAL[currentPreset];
  if (interval > MAX_INTERVAL[currentPreset]) interval = MAX_INTERVAL[currentPreset];
  return interval;
}

void updatePresetAnimation(uint32_t now) {
  if (controlMode != ControlMode::Preset || currentPreset == 6 || presetPaused) return;
  if (lastFrameMs != 0 && now - lastFrameMs < presetInterval()) return;
  lastFrameMs = now;
  switch (currentPreset) {
    case 0: showSideFlow(false); break;
    case 1: showSideFlow(true); break;
    case 2: showKnightRider(); break;
    case 3: showIndicator(true); break;
    case 4: showIndicator(false); break;
    case 5: showRedPatrol(); break;
  }
  ++animationFrame;
}

void setup() {
  digitalWrite(2, LOW);
  pinMode(2, OUTPUT);
  digitalWrite(46, HIGH);
  pinMode(46, OUTPUT);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  commandBuffer.reserve(MAX_COMMAND_LENGTH);
  ledsDin1.begin();
  ledsDin2.begin();
  selectPreset(0);
}

void loop() {
  const uint32_t now = millis();
  readSerialCommands();
  updateWakeButton(now);
  updatePresetAnimation(now);
  delay(1);
}
