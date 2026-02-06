#include <Adafruit_NeoPixel.h>

// --- DIAGNOSTIC SWITCH ---
bool DEBUG_MODE = false;

// --- CONFIGURABLE PARAMETERS ---
const int LED_BRIGHTNESS   = 255;      
const unsigned long TANK_CAPACITY = 100000;
const int PUMP_FLOW_SINGLE = 1500; 
const int PUMP_FLOW_DUAL   = 1300; 
const int PUMP_AMPS_SINGLE = 90;
const int PUMP_AMPS_DUAL   = 85;  

// --- DRAIN PARAMETERS ---
int DRAIN_MIN = 200;                 
int DRAIN_MAX = 1000;                
unsigned long DRAIN_INTERVAL = 600000; 

// --- PIN ASSIGNMENTS ---
const int PIN_NEOPIXEL  = 6;
const int PIN_P1_RUN    = 2;   
const int PIN_P2_RUN    = 4;   
const int PIN_P1_FAULT  = 7;   
const int PIN_P2_FAULT  = 8;   
const int PIN_P1_SEAL   = 11;  
const int PIN_P2_SEAL   = 12;  

const int PWM_TANK_LVL  = 3;   
const int PWM_FLOW_RATE = 5;  
const int PWM_P1_AMPS   = 9;   
const int PWM_P2_AMPS   = 10;  

Adafruit_NeoPixel strip(36, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- SYSTEM GLOBALS ---
float currentGallons = 50000; 
int currentDrainRate = 500;         
float netFlowRate = 0;              
unsigned long lastUpdate = 0;
unsigned long lastDrainChange = 0;  
unsigned long lastReport = 0;
int fadeValue = 0;
bool fadeDirection = true;

void setup() {
  delay(2000); 
  if (DEBUG_MODE) {
    Serial.begin(9600);
    Serial.println(F("HIGHTIDE SMOOTH-FLOW ACTIVE"));
  }

  randomSeed(analogRead(A0)); 
  currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
  lastDrainChange = millis();

  strip.begin();
  strip.show(); 

  pinMode(PIN_P1_RUN, INPUT_PULLUP);
  pinMode(PIN_P2_RUN, INPUT_PULLUP);
  pinMode(PIN_P1_FAULT, INPUT_PULLUP);
  pinMode(PIN_P2_FAULT, INPUT_PULLUP);
  pinMode(PIN_P1_SEAL, INPUT_PULLUP);
  pinMode(PIN_P2_SEAL, INPUT_PULLUP);
}

void loop() {
  bool p1Run = !digitalRead(PIN_P1_RUN);
  bool p2Run = !digitalRead(PIN_P2_RUN);
  bool p1Fail = !digitalRead(PIN_P1_FAULT);
  bool p2Fail = !digitalRead(PIN_P2_FAULT);
  bool p1Seal = !digitalRead(PIN_P1_SEAL);
  bool p2Seal = !digitalRead(PIN_P2_SEAL);

  if (millis() - lastDrainChange >= DRAIN_INTERVAL) {
    currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
    lastDrainChange = millis();
  }

  // Physics loop (1Hz)
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    float p1Inflow = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    float p2Inflow = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    netFlowRate = (p1Inflow + p2Inflow) - currentDrainRate;
    currentGallons += (netFlowRate / 60.0);
    currentGallons = constrain(currentGallons, 0, TANK_CAPACITY);

    analogWrite(PWM_TANK_LVL, map(currentGallons, 0, TANK_CAPACITY, 0, 255));
    analogWrite(PWM_FLOW_RATE, map(p1Inflow + p2Inflow, 0, 1000, 0, 255));
    
    int p1A = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    int p2A = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    analogWrite(PWM_P1_AMPS, map(p1A, 0, 100, 0, 255));
    analogWrite(PWM_P2_AMPS, map(p2A, 0, 100, 0, 255));

    if (DEBUG_MODE && (millis() - lastReport >= 15000)) {
      lastReport = millis();
      Serial.print(F("TANK: ")); Serial.print(currentGallons, 0); 
      Serial.print(F(" | NET: ")); Serial.println(netFlowRate);
    }
  }

  // --- LED REFRESH (As fast as possible for smoothness) ---
  updateLEDsSmooth(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDsSmooth(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  strip.clear();

  // 1. Fault Pulse Math
  if (fadeDirection) fadeValue += 5; else fadeValue -= 5;
  if (fadeValue >= 255) fadeDirection = false;
  if (fadeValue <= 10) fadeDirection = true;

  // 2. Pump Rotation Math (Smooth 4-LED circle)
  float pos = (float)(millis() % 800) / 800.0 * 4.0; 

  for (int i = 0; i < 4; i++) {
    // PUMP 1
    if (f1) strip.setPixelColor(i, strip.Color(fadeValue, 0, 0));
    else if (r1) strip.setPixelColor(i, getSmoothColor(i, pos, s1 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));

    // PUMP 2
    if (f2) strip.setPixelColor(i + 4, strip.Color(fadeValue, 0, 0));
    else if (r2) strip.setPixelColor(i + 4, getSmoothColor(i, pos, s2 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));
  }

  // 3. --- NEW TANK FLOW MATH (Full Strip 8-35) ---
  int numLeds = map((long)currentGallons, 0, (long)TANK_CAPACITY, 0, 28);
  numLeds = constrain(numLeds, 0, 28);

  // Aqua base color
  float aquaWave = sin(millis() / 2000.0); 
  int gVal = map(aquaWave * 100, -100, 100, 60, 160); 
  uint32_t aquaColor = strip.Color(0, gVal, 255);

  // Global Flow Offset (This creates the "Movement")
  // We use a slow multiplier so it looks like fluid, not a strobe light
  float flowOffset = (float)millis() * 0.005; 

  for (int i = 0; i < numLeds; i++) {
    float shimmer;
    
    if (netFlowRate > 50) { // FILLING (Upward movement)
      shimmer = sin((i - flowOffset) * 0.5); 
    } else if (netFlowRate < -50) { // DRAINING (Downward movement)
      shimmer = sin((i + flowOffset) * 0.5);
    } else {
      shimmer = 0; // Standing water
    }

    if (shimmer > 0.8) { // This creates the "peak" of the wave
      // Add a bright highlight to the aqua
      strip.setPixelColor(i + 8, strip.Color(120, 255, 255));
    } else {
      strip.setPixelColor(i + 8, aquaColor);
    }
  }

  strip.show();
}

// Math helper to calculate brightness based on distance from the "virtual" light position
uint32_t getSmoothColor(int ledIndex, float currentPos, uint32_t fullColor) {
  float diff = fabs((float)ledIndex - currentPos);
  if (diff > 2.0) diff = 4.0 - diff; // Handle the "wrap around" of the circle

  float brightness = 1.0 - (diff / 1.5); // Falloff radius of 1.5 LEDs
  if (brightness < 0) brightness = 0;
  if (brightness > 1.0) brightness = 1.0;

  // Apply brightness to the RGB components
  uint8_t r = (uint8_t)((fullColor >> 16 & 0xFF) * brightness);
  uint8_t g = (uint8_t)((fullColor >> 8 & 0xFF) * brightness);
  uint8_t b = (uint8_t)((fullColor & 0xFF) * brightness);
  
  // Add a tiny bit of background glow so it never goes pitch black
  if (r == 0 && g == 0 && b == 0) return strip.Color(0, 10, 5);
  
  return strip.Color(r, g, b);
}