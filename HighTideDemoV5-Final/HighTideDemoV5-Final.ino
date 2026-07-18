/*
 * ======================================================================================
 * HIGHTIDE SYSTEM MASTER CONTROL - FINAL VERSION
 * ======================================================================================
 * DESCRIPTION:
 * This code simulates a dual-pump lift station with a 28-LED vertical tank display 
 * and two 4-LED circular pump displays. It uses advanced "Sub-Pixel" math to 
 * ensure that animations and water levels move with analog smoothness.
 *
 * KEY FEATURES:
 * 1. Sub-pixel Tank Leveling: The top LED fades in/out for liquid-smooth transitions.
 * 2. Scaled Shimmer: Water flow highlights scale their brightness to the water level.
 * 3. Time-Based Rotation: Pump animations are calculated by time, not fixed steps.
 * 4. Aqua-Breathing: The tank color oscillates slowly between Blue and Cyan.
 * ======================================================================================
 */

#include <Adafruit_NeoPixel.h>

// --- DIAGNOSTIC SWITCH ---
// Set to 'false' for production. In 'true' mode, the Nano sends data to the PC
// which can cause minor "stuttering" in the LED animations.
bool DEBUG_MODE = false;  

// --- SIMULATION PARAMETERS ---
const int LED_BRIGHTNESS   = 255;      // Master brightness (0-255)
const unsigned long TANK_CAPACITY = 50000; // Capacity in Gallons (lower this number to inccrese the cycal rate) (smaller tank, faster fill and drain)
const int PUMP_FLOW_SINGLE = 510;      // GPM for one pump (Flowmeter is set to output 0-1000gpm so keep this number lower then 1000)
const int PUMP_FLOW_DUAL   = 490;      // GPM per pump when both run (number is dubled (2 pumps Running keep the total under 1000)
const int PUMP_AMPS_SINGLE = 65;       // Simulated Amps
const int PUMP_AMPS_DUAL   = 57;  

// --- DRAIN (DEMAND) LOGIC --- set your min and max drate rate, this number is randomly selected and helt for the drain internval
int DRAIN_MIN = 100;    //this number sets the lowes rate the sysetem will drain              
int DRAIN_MAX = 700;    // this number sets the max drain, think about pump speeds if this number is higher then the pump output the system will never fill up. if to low the drain will be slow)            
unsigned long DRAIN_INTERVAL = 600000; // Pick a new drain rate every 10 mins

// --- PIN ASSIGNMENTS ---
const int PIN_NEOPIXEL  = 6;   // Data pin for LED strip
const int PIN_P1_RUN    = 2;   // Input: Pump 1 Running
const int PIN_P2_RUN    = 4;   // Input: Pump 2 Running
const int PIN_P1_FAULT  = 7;   // Input: Pump 1 Fault (Red Pulse)
const int PIN_P2_FAULT  = 8;   // Input: Pump 2 Fault (Red Pulse)
const int PIN_P1_SEAL   = 11;  // Input: Pump 1 Seal Leak (Purple Pulse)
const int PIN_P2_SEAL   = 12;  // Input: Pump 2 Seal Leak (Purple Pulse)

const int PWM_TANK_LVL  = 3;   // Analog Out: Tank Level Meter
const int PWM_FLOW_RATE = 5;   // Analog Out: System Flow Meter
const int PWM_P1_AMPS   = 9;   // Analog Out: Pump 1 Amperage
const int PWM_P2_AMPS   = 10;  // Analog Out: Pump 2 Amperage

// Define Strip: 36 LEDs (4 P1, 4 P2, 28 Tank)
Adafruit_NeoPixel strip(36, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- GLOBAL VARIABLES ---
float currentGallons = 25000;  // Current water volume Set this to half the tank volume (start point at 50%)
int currentDrainRate = 500;    // Random demand from system
float netFlowRate = 0;         // Inflow minus Outflow
unsigned long lastUpdate = 0;  // Timer for physics loop
unsigned long lastDrainChange = 0;  
unsigned long lastReport = 0;
int fadeValue = 0;             // Global brightness for pulsing effects
bool fadeDirection = true;

void setup() {
  // 1. CAPACITOR STABILIZATION
  // Allows the 1000uF capacitor to charge fully before LEDs start pulling current.
  delay(2000); 
  
  if (DEBUG_MODE) {
    Serial.begin(9600);
    Serial.println(F("--- HighTide Master Active ---"));
  }

  // Seed the randomizer using noise from an empty analog pin
  randomSeed(analogRead(A0)); 
  currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
  lastDrainChange = millis();

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show(); // Initialize all pixels to 'off'

  // Initialize Inputs
  pinMode(PIN_P1_RUN, INPUT_PULLUP);
  pinMode(PIN_P2_RUN, INPUT_PULLUP);
  pinMode(PIN_P1_FAULT, INPUT_PULLUP);
  pinMode(PIN_P2_FAULT, INPUT_PULLUP);
  pinMode(PIN_P1_SEAL, INPUT_PULLUP);
  pinMode(PIN_P2_SEAL, INPUT_PULLUP);
}

void loop() {
  // READ INPUTS (Active Low/Grounded)
  bool p1Run = !digitalRead(PIN_P1_RUN);
  bool p2Run = !digitalRead(PIN_P2_RUN);
  bool p1Fail = !digitalRead(PIN_P1_FAULT);
  bool p2Fail = !digitalRead(PIN_P2_FAULT);
  bool p1Seal = !digitalRead(PIN_P1_SEAL);
  bool p2Seal = !digitalRead(PIN_P2_SEAL);

  // DRAIN LOGIC (System Demand)
  if (millis() - lastDrainChange >= DRAIN_INTERVAL) {
    currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
    lastDrainChange = millis();
  }

  // PHYSICS LOOP (Runs at 1Hz/Once per second)
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    
    // Calculate total Inflow based on which pumps are running and not faulted
    float p1Inflow = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    float p2Inflow = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    
    // Net Flow = (In - Out). Divide by 60 to convert GPM to Gallons Per Second.
    netFlowRate = (p1Inflow + p2Inflow) - currentDrainRate;
    currentGallons += (netFlowRate / 60.0);
    currentGallons = constrain(currentGallons, 0, TANK_CAPACITY);

    // ANALOG METER OUTPUTS (PWM)
    analogWrite(PWM_TANK_LVL, map(currentGallons, 0, TANK_CAPACITY, 0, 255));
    analogWrite(PWM_FLOW_RATE, map(p1Inflow + p2Inflow, 0, 1000, 0, 255));
    
    int p1A = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    int p2A = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    analogWrite(PWM_P1_AMPS, map(p1A, 0, 100, 0, 255));
    analogWrite(PWM_P2_AMPS, map(p2A, 0, 100, 0, 255));

    if (DEBUG_MODE && (millis() - lastReport >= 15000)) {
      lastReport = millis();
      Serial.print(F("TANK: ")); Serial.print(currentGallons, 0);
      Serial.print(F(" | NET FLOW: ")); Serial.println(netFlowRate);
    }
  }

  // RENDER ANIMATIONS (Runs as fast as possible for smoothness)
  updateLEDs(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDs(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  strip.clear(); // Clear the buffer

  // 1. FAULT PULSE CALCULATION
  // Slow triangle wave (10 to 255) for "breathing" fault lights.
  if (fadeDirection) fadeValue += 5; else fadeValue -= 5;
  if (fadeValue >= 255) fadeDirection = false;
  if (fadeValue <= 10) fadeDirection = true;

  // 2. PUMP ROTATION MATH
  // Uses time (millis) to calculate a fractional position (0.0 to 4.0).
  // This ensures the rotation is consistent even if the code slows down.
  float pos = (float)(millis() % 800) / 800.0 * 4.0; 

  for (int i = 0; i < 4; i++) {
    // PUMP 1 (Pixels 0-3)
    if (f1) strip.setPixelColor(i, strip.Color(fadeValue, 0, 0)); // Red Pulse if Fault
    else if (r1) strip.setPixelColor(i, getSmoothColor(i, pos, s1 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));
    
    // PUMP 2 (Pixels 4-7)
    if (f2) strip.setPixelColor(i + 4, strip.Color(fadeValue, 0, 0)); // Red Pulse if Fault
    else if (r2) strip.setPixelColor(i + 4, getSmoothColor(i, pos, s2 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));
  }

  // 3. TANK LEVEL WITH SUB-PIXEL FADING
  // preciseLeds calculates exactly how many LEDs should be on (e.g. 14.75)
  float preciseLeds = (currentGallons / (float)TANK_CAPACITY) * 28.0;
  int fullLeds = (int)preciseLeds;            // Solid LEDs (The integer part)
  float tipBrightness = preciseLeds - (float)fullLeds; // The "tip" fade (The decimal part)

  // AQUA-BREATHING COLOR: Slow Sine wave shifting the Green channel
  float aquaWave = sin(millis() / 2000.0); 
  int gVal = map(aquaWave * 100, -100, 100, 60, 160); 
  uint32_t aquaColor = strip.Color(0, gVal, 255);
  
  // SHIMMER OFFSET: Time-based movement for the flow animation
  float flowOffset = (float)millis() * 0.005; 

  for (int i = 0; i < 28; i++) {
    uint32_t baseCol = 0;
    float currentPixelLevel = 0.0; // 0.0 to 1.0 (how "present" this pixel is)

    if (i < fullLeds) {
      baseCol = aquaColor;
      currentPixelLevel = 1.0; 
    } 
    else if (i == fullLeds) {
      // THE FADING TIP: Scale Green/Blue channels by the tipBrightness
      int tipG = (int)(gVal * tipBrightness);
      int tipB = (int)(255 * tipBrightness);
      baseCol = strip.Color(0, tipG, tipB);
      currentPixelLevel = tipBrightness; 
    }

    // APPLY SCALED SHIMMER (Flowing highlights)
    if (currentPixelLevel > 0.05 && abs(netFlowRate) > 50) {
      // Calculate shimmering wave position
      float shimmerPos = (netFlowRate > 0) ? (i - flowOffset) : (i + flowOffset);
      float shimmerVal = sin(shimmerPos * 0.5);
      
      if (shimmerVal > 0.8) {
        // SCALED SHIMMER: Multiply the highlight by the pixel's level
        // to prevent the top LED from flashing to 100% white.
        int shimR = (int)(120 * currentPixelLevel);
        int shimG = (int)(255 * currentPixelLevel);
        int shimB = (int)(255 * currentPixelLevel);
        strip.setPixelColor(i + 8, strip.Color(shimR, shimG, shimB));
      } else {
        strip.setPixelColor(i + 8, baseCol);
      }
    } else {
      strip.setPixelColor(i + 8, baseCol);
    }
  }

  strip.show(); // Push the final frame to the LEDs
}

/**
 * Helper: Calculates the brightness of a pump LED based on its distance
 * from the 'virtual' light position. This creates the 'Slide' effect.
 */
uint32_t getSmoothColor(int ledIndex, float currentPos, uint32_t fullColor) {
  float diff = fabs((float)ledIndex - currentPos);
  
  // Handle the circular wrap-around (distance from 3 to 0 is 1, not 3)
  if (diff > 2.0) diff = 4.0 - diff;
  
  // Linear falloff: Light is 100% at the center, 0% if 1.5 pixels away
  float br = 1.0 - (diff / 1.5);
  if (br < 0) br = 0;
  
  // Apply the brightness multiplier to the color channels
  uint8_t r = (uint8_t)((fullColor >> 16 & 0xFF) * br);
  uint8_t g = (uint8_t)((fullColor >> 8 & 0xFF) * br);
  uint8_t b = (uint8_t)((fullColor & 0xFF) * br);
  
  // Provide a faint "pilot light" so the circle is always visible
  if (r == 0 && g == 0 && b == 0) return strip.Color(0, 5, 2); 
  
  return strip.Color(r, g, b);
}
