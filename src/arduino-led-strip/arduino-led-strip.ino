/**
 * Copyright (c) 2021 Artem Hlumov <artyom.altair@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Suppress pragma message during compilation.
#define FASTLED_INTERNAL

#include "FastLED.h"
#include "IRremote.h"

// ====================================================
//                         PINS
// ====================================================

/**
 * Data pin of WS2811 LED (usually the middle one).
 */
const int LED_DATA_PIN = 4;
/**
 * Data pin of the LED receiver (marked as Y).
 */
const int RECEIVER_DATA_PIN = 12;

// ====================================================
//              REMOTE RECEIVER CONTROLLER
// ====================================================

/**
 * Helper class to handle remote input.
 */
class Receiver
{
public:
  /**
   * Buttons on the remote control.
   */
  enum Button { POWER, FUNC_STOP, VOL_PLUS, FAST_BACK, PAUSE, FAST_FORWARD, DOWN, VOL_MINUS, UP, EQ, ST_REPT,
                NUM_0, NUM_1, NUM_2, NUM_3, NUM_4, NUM_5, NUM_6, NUM_7, NUM_8, NUM_9, NONE };
  /**
   * Constructor.
   * @param data_pin Data pin of the infrared receiver.
   */
  Receiver(int data_pin)
  {
    // Initialize the infrared receiver and start listening.
    IrReceiver.begin(data_pin, DISABLE_LED_FEEDBACK);
    IrReceiver.enableIRIn();
  }
  /**
   * Read signal from the remote control and return the presed button or NONE if there is no signal.
   * @returns Button that has been pressed.
   */
  Button readSignal() const
  {
    // IRremote conflicts with FastLED and these libraries cannot be mixed together.
    // Find details here: https://github.com/FastLED/FastLED/issues/198
    // So we should first make sure the signal is completely read before letting FastLED take control.
    while (!IrReceiver.isIdle());
    // Try to deconde the incoming signal.
    if (IrReceiver.decode()) {
      // Convert the raw signal data into a button code.
      Button result = rawDataToButtonCode(IrReceiver.decodedIRData.decodedRawData);
      // Be ready to receive new events.
      IrReceiver.resume();
      // Return the pressed button.
      return result;
    } else {
      // Return NONE if there is no signal.
      return Button::NONE;
    }
  }
private:
  /**
   * Helper function to convert raw signal data into a button code.
   * Particular codes might require alignment depending on model of the remote control.
   * Here we have correct codes for a remote control from Elegoo.
   * Do not handle repeat code (0xFFFFFFFF), so we only react on a single press, not press and hold.
   * @param rawData Data to process.
   * @returns Button code that corresponds to the signal or NONE if the signal is not recognized.
   */
  Button rawDataToButtonCode(const uint32_t& rawData) const
  {
    switch(rawData) {
      case 0xBA45FF00:
        return Button::POWER;
      case 0xB847FF00:
        return Button::FUNC_STOP;
      case 0xB946FF00:
        return Button::VOL_PLUS;
      case 0xBB44FF00:
        return Button::FAST_BACK;
      case 0xBF40FF00:
        return Button::PAUSE;
      case 0xBC43FF00:
        return Button::FAST_FORWARD;
      case 0xF807FF00:
        return Button::DOWN;
      case 0xEA15FF00:
        return Button::VOL_MINUS;
      case 0xF609FF00:
        return Button::UP;
      case 0xE619FF00:
        return Button::EQ;
      case 0xF20DFF00:
        return Button::ST_REPT;
      case 0xE916FF00:
        return Button::NUM_0;
      case 0xF30CFF00:
        return Button::NUM_1;
      case 0xE718FF00:
        return Button::NUM_2;
      case 0xA15EFF00:
        return Button::NUM_3;
      case 0xF708FF00:
        return Button::NUM_4;
      case 0xE31CFF00:
        return Button::NUM_5;
      case 0xA55AFF00:
        return Button::NUM_6;
      case 0xBD42FF00:
        return Button::NUM_7;
      case 0xAD52FF00:
        return Button::NUM_8;
      case 0xB54AFF00:
        return Button::NUM_9;
      default:
        return Button::NONE;
    }
  }
};

// ====================================================
//                    LED STRIP DATA
// ====================================================

/**
 * Amount of addressable LEDs on a strip. Keep in mind that most of WS2811 strips have one controller per a group of 3 LEDs,
 * so you can only control one group but not each single LED. Here we need a numer of individually addressed groups.
 * Therefore if you have 150 LEDs arranged in groups of 3, NUM_LEDS will be 50.
 */
const int NUM_LEDS = 50;
/**
 * Buffer of addressable LEDs where we can put color.
 */
CRGB leds[NUM_LEDS];

// ====================================================
//                   SCENARIOS DATA
// ====================================================

/**
 * Descriptor of an LED scenario.
 */
struct Scenario
{
  /**
   * Function that is called in the main loop if the scenario becomes active.
   */
  void (*func)();
  /**
   * Number of scenario variants. The current variant is always available as currentVariant global variable.
   */
  int numVariants;
};

/**
 * Total amount of scenarios.
 * We have 10 buttons, so each button corresponds to one scenario.
 */
const int NUM_SCENARIOS = 10;
/**
 * Array of animation scenarios.
 */
Scenario scenarios[NUM_SCENARIOS];
/**
 * Index of currently selected scenario.
 */
int currentScenario = 0;

// ====================================================
//               SCENARIO CONFIGURATION
// ====================================================

/**
 * Amount of brightness steps (adjustable with VOL+ and VOL- buttons on the remote control).
 */
const int NUM_BRIGHTNESS_STEPS  = 4;
/**
 * Mapping between brightness step and particular brightness in range [0 - 255].
 */
int brightnessMap[NUM_BRIGHTNESS_STEPS] = { 25, 100, 175, 255 };
/**
 * Current step of brightness (index in brightnessMap).
 */
int currentBrightness = NUM_BRIGHTNESS_STEPS - 1;
/**
 * Indicate whether the LED is switched off.
 * In this case all buttons except POWER are ignored and the current scenario is stopped.
 */
bool isSwitchedOn = true;
/**
 * Current variant of the scenario.
 * Total amount of variants is defined by the scenario as Scenario::numVariants.
 */
int currentVariant = 0;
/**
 * Amount of speed steps.
 */
const int NUM_SPEED_STEPS  = 5;
/**
 * Default speed step that corresponds to 1:1 speed.
 */
const int DEFAULT_SPEED_STEP = 2;
/**
 * Current speed step.
 */
int currentSpeed = DEFAULT_SPEED_STEP;
/**
 * Indicate if the LED strip works in switch mode meaning each scenario is only played for SCENARIO_SWITCH_TIMEOUT milliseconds
 * and then it switches to the new one.
 */
bool isSwitchModeActive = false;
/**
 * Timeout of the switch mode. Used only in case isSwitchModeActive == true.
 */
const unsigned long SCENARIO_SWITCH_TIMEOUT = 60000;
/**
 * Timestamp of last scenario switch. Used only in case isSwitchModeActive == true.
 */
unsigned long lastScenarioSwitchTimestamp = millis();

// ====================================================
//                 ADDITIONAL FUNCTIONS
// ====================================================

/**
 * Receiver to listen to remote control buttons.
 */
Receiver* rec;

/**
 * Helper function that applies action depending on the pressed button.
 * The function could be called during the normal loop as well as during sleep time inside the scenario.
 * In case it is called during the scenario execution, it will return false if the scenario could continue
 * or true if it should be immediately finished.
 * @return Whether the current scenario should be canceled.
 */
bool readReceiverAndProcess()
{
  // Read button input.
  Receiver::Button button = rec->readSignal();
  // Case 1: Switch on and off the LED strip.
  if (button == Receiver::Button::POWER) {
    isSwitchedOn = !isSwitchedOn;
    if (!isSwitchedOn) {
      // Clean up the strip.
      FastLED.clear();
      FastLED.show();
      // Force the running scenario to cancel.
      return true;
    }
  }
  // If the LED strip is switched off, no commands are accepted.
  if (!isSwitchedOn) {
    return true;
  }
  // Case 2: Select a scenario.
  if (button >= Receiver::Button::NUM_0 && button <= Receiver::Button::NUM_9) {
    // Convert button number into a scenario index.
    int newScenario = button - Receiver::Button::NUM_0;
    // Validate the scenario index.
    if (newScenario >= NUM_SCENARIOS) {
      return false;
    }
    // Switch and restart the scenario only if it is really changed.
    if (currentScenario != newScenario) {
      currentScenario = newScenario;
      currentVariant = 0;
      // Clean up the strip.
      FastLED.clear();
      FastLED.show();
      // Force the running scenario to cancel.
      return true;
    }
  }
  // Case 3: Select a scenario variant.
  int numVariants = scenarios[currentScenario].numVariants;
  if (button == Receiver::Button::FAST_FORWARD || button == Receiver::Button::FAST_BACK) {
    // Do nothing if there is only one variant for the current scenario.
    if (numVariants == 1) {
      return false;
    }
    // Increase/decrease the variant number.
    // The counter is looped, so after the last variant we jump back to the first one.
    if (button == Receiver::Button::FAST_FORWARD) {
      currentVariant = (currentVariant + 1) % numVariants;
    } else {
      currentVariant = (numVariants + currentVariant - 1) % numVariants;
    }
    // Clean up the strip.
    FastLED.clear();
    FastLED.show();
    // Force the running scenario to cancel.
    return true;
  }
  // Case 4: Brightness value.
  if (button == Receiver::Button::VOL_PLUS) {
    currentBrightness = min(currentBrightness + 1, NUM_BRIGHTNESS_STEPS - 1);
    FastLED.setBrightness(brightnessMap[currentBrightness]);
  } else if (button == Receiver::Button::VOL_MINUS) {
    currentBrightness = max(currentBrightness - 1, 0);
    FastLED.setBrightness(brightnessMap[currentBrightness]);
  }
  // Case 5: Speed value.
  if (button == Receiver::Button::UP) {
    currentSpeed = min(currentSpeed + 1, NUM_SPEED_STEPS - 1);
  } else if (button == Receiver::Button::DOWN) {
    currentSpeed = max(currentSpeed - 1, 0);
  }
  // Case 6: Switch mode.
  if (button == Receiver::Button::ST_REPT) {
    isSwitchModeActive = !isSwitchModeActive;
    lastScenarioSwitchTimestamp = millis();
  }
  return false;
}

/**
 * Check if we should switch to another scenario (in case of switch mode).
 * If the switch should be performed, the function returns true and updates lastScenarioSwitchTimestamp.
 * @return Wiether we should switch to another scenario because of the switch mode timeout.
 */
bool shouldSwitchScenario()
{
  // Does not work outside the switch mode.
  if (!isSwitchModeActive) {
    return false;
  }
  // Check if the timeout has passed.
  if (millis() - lastScenarioSwitchTimestamp > SCENARIO_SWITCH_TIMEOUT) {
    lastScenarioSwitchTimestamp = millis();
    return true;
  }
  return false;
}

/**
 * Select a random scenario and make it current one.
 * Does not select the scenario that is currently active.
 * It is important to have at least 2 non-empty scenarios, otherwise the function will hang up.
 */
void selectRandomScenario()
{
  while(true) {
    // Take a random one.
    int scenario = random8() % NUM_SCENARIOS;
    // Make sure this is not the currently active one.
    if (scenario == currentScenario) {
      continue;
    }
    // Apply the scenario.
    currentScenario = scenario;
    break;
  }
}

/**
 * Analogue of millis() to be used in the scenario code.
 * The function returns amount of milliseconds from the startup adjusting it to the speed value.
 * So if the speed is higher than DEFAULT_SPEED_STEP, the time passes faster.
 * If the speed is lower than DEFAULT_SPEED_STEP, the time passes slower.
 * @returns Current timestamp taking into account speed.
 */
unsigned long milliseconds()
{
  // Calculate time adjusted to the current speed value.
  unsigned long adjustedTime = millis();
  // If the current speed is higher than the normal one, increase the time value.
  if (currentSpeed > DEFAULT_SPEED_STEP) {
    adjustedTime *= (currentSpeed - DEFAULT_SPEED_STEP + 1);
  }
  // If the current speed is lower than the normal one, decrease the time value.
  if (currentSpeed < DEFAULT_SPEED_STEP) {
    adjustedTime /= (DEFAULT_SPEED_STEP - currentSpeed + 1);
  }
  return adjustedTime;
}

/**
 * Analogue of delay() to be used in the scenario code.
 * If we just run a scenario containing delays we won't be able to process input from the remote constrol. To avoid this limitation
 * we can provide our own delay() function that will monitor remote control input while waiting.
 * This function should be able to notify that the scenario must be canceled if, for example, the user switched to another one.
 * Also it applies time adjustment according to the current speed value.
 * In the scenario code the function should always be used as:
 * 
 * if (sleep(XX)) return;
 * 
 * to make sure cancel is handled properly.
 * Also if the scenario contains an infinite loop, it is necessary to run at least sleep(0) in each loop to
 * unblock input processing, for example:
 * 
 * ... // Scenario initialization
 * do {
 *   ... // Scenario loop
 * } while (!sleep(0));
 * 
 * @param milliseconds Desired delay in milliseconds. The delay might be adjusted according to the current speed.
 * @returns Whether the scenario should be immediately canceled.
 */
bool sleep(unsigned long milliseconds)
{
  // Check if we should drop the scenario execution due to switch mode timeout.
  if (shouldSwitchScenario()) {
    // Clean up the strip.
    FastLED.clear();
    FastLED.show();
    selectRandomScenario();
    currentVariant = random8() % scenarios[currentScenario].numVariants;
    return true;
  }
  // Calculate time adjusted to the current speed value.
  unsigned long adjustedTime = milliseconds;
  // If the current speed is higher than the normal one, decrease the time value.
  if (currentSpeed > DEFAULT_SPEED_STEP) {
    adjustedTime /= (currentSpeed - DEFAULT_SPEED_STEP + 1);
  }
  // If the current speed is lower than the normal one, increase the time value.
  if (currentSpeed < DEFAULT_SPEED_STEP) {
    adjustedTime *= (DEFAULT_SPEED_STEP - currentSpeed + 1);
  }
  // Emulate delay() constantly looking for the remote control signal.
  unsigned long startTime = millis();
  // Make sure we process input at least once.
  do {
    if (readReceiverAndProcess()) {
      // If the signal forces us to cancel the scenario, return true.
      return true;
    }
  } while (millis() - startTime <= adjustedTime);
  // If the scenario could proceed further, return false.
  return false;
}

/**
 * Amount of standard colors.
 */
const int NUM_STANDARD_COLORS = 7;

/**
 * Return a standard color that corresponds to the given index.
 * There are NUM_STANDARD_COLORS standard colors and the last one is always white.
 * @param index Color index.
 * @return Color that corresponds to the given index.
 */
CRGB getStandardColor(int index)
{
  switch(index) {
    case 0:
      return CRGB(255, 0, 0); // Red
    case 1:
      return CRGB(0, 255, 0); // Green
    case 2:
      return CRGB(0, 0, 255); // Blue
    case 3:
      return CRGB(255, 255, 0); // Yellow
    case 4:
      return CRGB(255, 0, 255); // Magenta
    case 5:
      return CRGB(0, 255, 255); // Cyan
    case 6:
    default:
      return CRGB(255, 255, 255); // White
  }
}

// ====================================================
//                      SCENARIOS
// ====================================================

// Scenarios are simple functions describing animation in terms of changing the LED colors.
// Scenarios are executed in the regular loop, so the function is called repetitively.
// There is a couple rules that should be taken into account during implementation of the scenario:
// 1) Use milliseconds() instead of millis()
// 2) Use sleep() instead of delay()
// 3) Always call sleep(0) in a cycle performing some long operation to unblock input handling.

/**
 * Create scenarios and put them to the array.
 */
void setupScenarios()
{
  // ---------------------------------
  // ---- Scenario 0: Fade in/out ----
  // ---------------------------------
  scenarios[0] = Scenario {
    []() {
      do {
        // To make the animation slower we can divide the current timestamp by some integer number.
        // Fast sin argument should be in range [0-255], so crop it.
        int argument = (milliseconds() / 6) % 256;
        // Calculate a fade in/out factor using fast sin() function.
        // The factor value will be in range [0.0-1.0).
        float factor = sin8_avr(argument) / 256.0;
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = getStandardColor(currentVariant);
          // Apply factor.
          leds[i][0] *= factor;
          leds[i][1] *= factor;
          leds[i][2] *= factor;
        }
        // Display changes.
        FastLED.show();
      } while (!sleep(0));
    }, NUM_STANDARD_COLORS
  };
  // -----------------------------------
  // ---- Scenario 1: Running light ----
  // -----------------------------------
  scenarios[1] = Scenario {
    []() {
      // Light up every 1 out of 5 LEDs.
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i % 5 == 0) {
          leds[i] = getStandardColor(currentVariant);
        }
      }
      do {
        // Shift all leds to the left by one.
        CRGB firstLED = leds[0];
        for (int i = 0; i < NUM_LEDS - 1; i++) {
          leds[i] = leds[i + 1];
        }
        leds[NUM_LEDS - 1] = firstLED;
        // Display changes.
        FastLED.show();
      } while (!sleep(60));
    }, NUM_STANDARD_COLORS
  };
  // -------------------------------------
  // ---- Scenario 2: Looped sequence ----
  // -------------------------------------
  scenarios[2] = Scenario {
    []() {
      // Initialize the LED strip with 3 groups of RGB colors.
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i % 3 == 0) leds[i] = CRGB(255, 0, 0);
        if (i % 3 == 1) leds[i] = CRGB(0, 255, 0);
        if (i % 3 == 2) leds[i] = CRGB(0, 0, 255);
      }
      do {
        // Shift all leds to the left by one.
        CRGB firstLED = leds[0];
        for (int i = 0; i < NUM_LEDS - 1; i++) {
          leds[i] = leds[i + 1];
        }
        leds[NUM_LEDS - 1] = firstLED;
        // Display changes.
        FastLED.show();
      } while (!sleep(1000));
    }, 1
  };
  // -----------------------------------------
  // ---- Scenario 3: Smooth color change ----
  // -----------------------------------------
  scenarios[3] = Scenario {
    []() {
      // Take a random color except white (the last one).
      CRGB fromColor = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
      do {
        // Take a random color except white (the last one).
        CRGB toColor = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
        unsigned long startTime = milliseconds();
        // Period of transition animation changing one color to another.
        const unsigned long TRANSITION_PERIOD = 1000;
        // Perform the transition animation during TRANSITION_PERIOD milliseconds.
        while (milliseconds() - startTime < TRANSITION_PERIOD) {
          const unsigned long dt = milliseconds() - startTime;
          // Linear transition of colors.
          CRGB color;
          color[0] = fromColor[0] * (TRANSITION_PERIOD - dt) / TRANSITION_PERIOD +
                     toColor[0] * dt / TRANSITION_PERIOD;
          color[1] = fromColor[1] * (TRANSITION_PERIOD - dt) / TRANSITION_PERIOD +
                     toColor[1] * dt / TRANSITION_PERIOD;
          color[2] = fromColor[2] * (TRANSITION_PERIOD - dt) / TRANSITION_PERIOD +
                     toColor[2] * dt / TRANSITION_PERIOD;
          for (int i = 0; i < NUM_LEDS - 1; i++) {
            leds[i] = color;
          }
          // Display changes.
          FastLED.show();
          // Perform input handling.
          if (sleep(0)) return;
        }
        // Next loop we will start from toColor and turn it into something else.
        fromColor = toColor;
      } while (!sleep(0));
    }, 1
  };
  // -----------------------------------
  // ---- Scenario 4: Random colors ----
  // -----------------------------------
  scenarios[4] = Scenario {
    []() {
      do {
        for (int i = 0; i < NUM_LEDS - 1; i++) {
          // Take a random color except white (the last one).
          leds[i] = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
        }
        // Display changes.
        FastLED.show();
      } while (!sleep(1000));
    }, 1
  };
  // ------------------------------------
  // ---- Scenario 5: Evens and odds ----
  // ------------------------------------
  scenarios[5] = Scenario {
    []() {
      // Select two initial colors (except white).
      CRGB color1 = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
      CRGB color2 = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
      // Make sure they are different.
      while (color1 == color2) {
        color2 = random8() % (NUM_STANDARD_COLORS - 1);
      }
      // Flag that switches every loop and allows us to change either even or odd lights.
      bool isOdd = true;
      do {
        // Even lights get color 1, odd lights get color 2.
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = i % 2 == 0 ? color1 : color2;
        }
        // Select a new random color.
        CRGB color = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
        // Make sure it is different from color 1 and color 2.
        while (color == color2 || color == color1) {
          color = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
        }
        // Replace one of the colors.
        if (isOdd) {
          color1 = color;
        } else {
          color2 = color;
        }
        isOdd = !isOdd;
        // Display changes.
        FastLED.show();
      } while (!sleep(700));
    }, 1
  };
  // -------------------------------------
  // ---- Scenario 6: Jump left-right ----
  // -------------------------------------
  scenarios[6] = Scenario {
    [](){
      bool isOdd = true;
      do {
        // Light up either only odd or only event elements.
        for (int i = 0; i < NUM_LEDS; i++) {
          if (isOdd) {
            leds[i] = (i % 2 != 0) ? getStandardColor(currentVariant) : CRGB(0, 0, 0);
          } else {
            leds[i] = (i % 2 == 0) ? getStandardColor(currentVariant) : CRGB(0, 0, 0);
          }
        }
        isOdd = !isOdd;
        // Display changes.
        FastLED.show();
      } while (!sleep(700));
    }, NUM_STANDARD_COLORS
  };
  // -----------------------------
  // ---- Scenario 7: Racing -----
  // -----------------------------
  scenarios[7] = Scenario {
    [](){
      do {
        // Select 3 starting points.
        int startIndices[] = {
          random16() % NUM_LEDS,
          random16() % NUM_LEDS,
          random16() % NUM_LEDS,
        };
        // Now we need to make sure all points are different.
        // Make sure the second point is different from the first one.
        while (startIndices[1] == startIndices[0]) {
          startIndices[1] = random16() % NUM_LEDS;
        }
        // Make sure the third point is different from the first and the second.
        while (startIndices[2] == startIndices[0] || startIndices[2] == startIndices[1]) {
          startIndices[2] = random16() % NUM_LEDS;
        }
        // Light will propagate from the start point to left and to right during NUM_LEDS / 2 steps.
        for (int i = 0; i <= NUM_LEDS / 2; i++) {
          // Clean up the LED strip.
          memset(leds, 0, sizeof(CRGB) * NUM_LEDS);
          // For each starting point.
          for (int j = 0; j < sizeof(startIndices) / sizeof(startIndices[0]); j++) {
            // Select the left and right points that correspond to the step i.
            // Assume the LED strip is looped.
            int leftIndex = (NUM_LEDS + startIndices[j] - i) % NUM_LEDS;
            int rightIndex = (startIndices[j] + i) % NUM_LEDS;
            // Select a color for the start point j (except white).
            CRGB color = getStandardColor((currentVariant + j) % (NUM_STANDARD_COLORS - 1));
            // Blend points, so if at some point of time two traces overlap, we get color mix.
            // Blend left point.
            leds[leftIndex].r = min(leds[leftIndex].r + color.r, 256);
            leds[leftIndex].g = min(leds[leftIndex].g + color.g, 256);
            leds[leftIndex].b = min(leds[leftIndex].b + color.b, 256);
            // Blend right point.
            leds[rightIndex].r = min(leds[rightIndex].r + color.r, 256);
            leds[rightIndex].g = min(leds[rightIndex].g + color.g, 256);
            leds[rightIndex].b = min(leds[rightIndex].b + color.b, 256);
          }
          // Display changes.
          FastLED.show();
          if (sleep(30)) return;
        }
      } while (!sleep(0));
    }, NUM_STANDARD_COLORS
  };
  // --------------------------------
  // ---- Scenario 8: Blast wave ----
  // --------------------------------
  scenarios[8] = Scenario {
    [](){
      do {
        // Select a random color.
        CRGB color = getStandardColor(random8() % (NUM_STANDARD_COLORS - 1));
        // Select a random starting point.
        int startIndex = random16() % NUM_LEDS;
        // Propagate the color to left and to right without cleaning up previous positions.
        for (int i = 0; i <= NUM_LEDS / 2; i++) {
          // Calculate left and right indices that correspond to the step i.
          int leftIndex = (NUM_LEDS + startIndex - i) % NUM_LEDS;
          int rightIndex = (startIndex + i) % NUM_LEDS;
          // Fill them in with the color.
          leds[leftIndex] = color;
          leds[rightIndex] = color;
          // Display changes.
          FastLED.show();
          if (sleep(30)) return;
        }
      } while (!sleep(0));
    }, 1
  };
  // -------------------------------------
  // ---- Scenario 9: Stacking colors ----
  // -------------------------------------
  scenarios[9] = Scenario {
    [](){
      do {
        // Clean up the LED strip.
        memset(leds, 0, sizeof(CRGB) * NUM_LEDS);
        // For each block we want to drop.
        for (int i = 0; i < NUM_LEDS; i++) {
          // Select a color of the block.
          CRGB color = getStandardColor(i % (NUM_STANDARD_COLORS - 1));
          // Move it from right to left cleaning up the previous position.
          // So we have only a single moving light.
          for (int j = NUM_LEDS - 1; j >= i; j--) {
            if (j != NUM_LEDS - 1) {
              leds[j + 1] = CRGB(0, 0, 0);
            }
            leds[j] = color;
            // Display changes.
            FastLED.show();
            if (sleep(10)) return;
          }
        }
      } while (!sleep(0));
    }, 1
  };
}

// ====================================================
//               SETUP AND MAIN LOOP
// ====================================================

void setup()
{
  // Initialize FastLED.
  // You might need to adjust the last template parameter according to
  // colors of your LED strip.
  FastLED.addLeds<WS2811, LED_DATA_PIN, BRG>(leds, NUM_LEDS);
  FastLED.setBrightness(brightnessMap[currentBrightness]);
  // Initialize the receiver.
  // Important: there is a conflict between these two libraries, so IRreceiver should be
  // initialized strictly after FastLED. Otherwise you will get random noise instead of remote control signal.
  rec = new Receiver(RECEIVER_DATA_PIN);
  // Create scenarios.
  setupScenarios();
}

void loop()
{
  // Process input from the remote control.
  readReceiverAndProcess();
  // Execute the current scenario.
  if (isSwitchedOn) {
    scenarios[currentScenario].func();
  }
}
