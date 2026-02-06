#include <Adafruit_NeoPixel.h>

// --- DIAGNOSTIC SWITCH ---
bool DEBUG_MODE = false;  // Set to true for diagnostics, false for butter-smooth flow

// --- CONFIGURABLE PARAMETERS ---
const int LED_BRIGHTNESS   = 220;      
const unsigned long TANK_CAPACITY = 50000;
const int PUMP_FLOW_SINGLE = 550; 
const int PUMP_FLOW_DUAL   = 490; 
const int PUMP_AMPS_SINGLE = 80;
const int PUMP_AMPS_DUAL   = 75;  

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
float currentGallons = 35000; 
int currentDrainRate = 500;         
float netFlowRate = 0;              
unsigned long lastUpdate = 0;
unsigned long lastDrainChange = 0;  
unsigned long lastReport = 0;
int fadeValue = 0;
bool fadeDirection = true;

void setup() {
  delay(2000); // Wait for 1000uF caps to stabilize
  
  if (DEBUG_MODE) {
    Serial.begin(9600);
    Serial.println(F("--- HighTide Master Active ---"));
  }

  randomSeed(analogRead(A0)); 
  currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
  lastDrainChange = millis();

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show(); 

  pinMode(PIN_P1_RUN, INPUT_PULLUP);
  pinMode(PIN_P2_RUN, INPUT_PULLUP);
  pinMode(PIN_P1_FAULT, INPUT_PULLUP);
  pinMode(PIN_P2_FAULT, INPUT_PULLUP);
  pinMode(PIN_P1_SEAL, INPUT_PULLUP);
  pinMode(PIN_P2_SEAL, INPUT_PULLUP);
}

void loop() {
  // Read Inputs
  bool p1Run = !digitalRead(PIN_P1_RUN);
  bool p2Run = !digitalRead(PIN_P2_RUN);
  bool p1Fail = !digitalRead(PIN_P1_FAULT);
  bool p2Fail = !digitalRead(PIN_P2_FAULT);
  bool p1Seal = !digitalRead(PIN_P1_SEAL);
  bool p2Seal = !digitalRead(PIN_P2_SEAL);

  // Drain Logic
  if (millis() - lastDrainChange >= DRAIN_INTERVAL) {
    currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
    lastDrainChange = millis();
  }

  // Physics & PWM Loop (1Hz)
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    float p1Inflow = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    float p2Inflow = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    netFlowRate = (p1Inflow + p2Inflow) - currentDrainRate;
    currentGallons += (netFlowRate / 60.0);
    currentGallons = constrain(currentGallons, 0, TANK_CAPACITY);

    // PWM Meters
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

  // Render the smooth animations
  updateLEDs(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDs(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  strip.clear();

  // 1. Fault Pulse Calculation
  if (fadeDirection) fadeValue += 5; else fadeValue -= 5;
  if (fadeValue >= 255) fadeDirection = false;
  if (fadeValue <= 10) fadeDirection = true;

  // 2. Pump Rotation Math
  float pos = (float)(millis() % 800) / 800.0 * 4.0; 

  for (int i = 0; i < 4; i++) {
    // PUMP 1
    if (f1) strip.setPixelColor(i, strip.Color(fadeValue, 0, 0));
    else if (r1) strip.setPixelColor(i, getSmoothColor(i, pos, s1 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));
    
    // PUMP 2
    if (f2) strip.setPixelColor(i + 4, strip.Color(fadeValue, 0, 0));
    else if (r2) strip.setPixelColor(i + 4, getSmoothColor(i, pos, s2 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0)));
  }

  // 3. Tank Level with Scaled Shimmer Tip
  float preciseLeds = (currentGallons / (float)TANK_CAPACITY) * 28.0;
  int fullLeds = (int)preciseLeds; 
  float tipBrightness = preciseLeds - (float)fullLeds; 

  float aquaWave = sin(millis() / 2000.0); 
  int gVal = map(aquaWave * 100, -100, 100, 60, 160); 
  uint32_t aquaColor = strip.Color(0, gVal, 255);
  float flowOffset = (float)millis() * 0.005; 

  for (int i = 0; i < 28; i++) {
    uint32_t baseCol = 0;
    float currentPixelLevel = 0.0; 

    if (i < fullLeds) {
      baseCol = aquaColor;
      currentPixelLevel = 1.0; 
    } 
    else if (i == fullLeds) {
      int tipG = (int)(gVal * tipBrightness);
      int tipB = (int)(255 * tipBrightness);
      baseCol = strip.Color(0, tipG, tipB);
      currentPixelLevel = tipBrightness; 
    }

    if (currentPixelLevel > 0.05 && abs(netFlowRate) > 50) {
      float shimmerPos = (netFlowRate > 0) ? (i - flowOffset) : (i + flowOffset);
      float shimmerVal = sin(shimmerPos * 0.5);
      
      if (shimmerVal > 0.8) {
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

  strip.show();
}

uint32_t getSmoothColor(int ledIndex, float currentPos, uint32_t fullColor) {
  float diff = fabs((float)ledIndex - currentPos);
  if (diff > 2.0) diff = 4.0 - diff;
  float br = 1.0 - (diff / 1.5);
  if (br < 0) br = 0;
  
  uint8_t r = (uint8_t)((fullColor >> 16 & 0xFF) * br);
  uint8_t g = (uint8_t)((fullColor >> 8 & 0xFF) * br);
  uint8_t b = (uint8_t)((fullColor & 0xFF) * br);
  
  if (r == 0 && g == 0 && b == 0) return strip.Color(0, 5, 2); 
  return strip.Color(r, g, b);
}