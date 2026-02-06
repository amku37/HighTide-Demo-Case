#include <Adafruit_NeoPixel.h>

// --- CONFIGURABLE PARAMETERS ---
const int LED_BRIGHTNESS   = 200;      
const unsigned long TANK_CAPACITY = 60000;
const int PUMP_FLOW_SINGLE = 1000; 
const int PUMP_FLOW_DUAL   = 779; 
const int PUMP_AMPS_SINGLE = 90;
const int PUMP_AMPS_DUAL   = 82;  

// --- DRAIN PARAMETERS ---
int DRAIN_MIN = 200;                 
int DRAIN_MAX = 1000;                
unsigned long DRAIN_INTERVAL = 600000; // 10 Minutes

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

// Total LEDs: 4 (P1) + 4 (P2) + 28 (Tank) = 36
Adafruit_NeoPixel strip(36, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- SYSTEM GLOBALS ---
float currentGallons = 60000; 
int currentDrainRate = 500;         
unsigned long lastUpdate = 0;
unsigned long lastDrainChange = 0;  
unsigned long lastReport = 0;
int animationFrame = 0;
int fadeValue = 0;
bool fadeDirection = true;

void setup() {
  // 1. THE STABILIZER DELAY
  // Give the 1000uF caps time to charge before we touch anything
  delay(2000); 

  Serial.begin(9600);
  
  randomSeed(analogRead(A0)); 
  currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
  lastDrainChange = millis();

  // 2. SAFE PIXEL INITIALIZATION
  strip.begin();
  strip.setBrightness(0); 
  strip.show();           
  delay(100);             
  strip.setBrightness(LED_BRIGHTNESS); 
  strip.show();

  // 3. PIN SETUP
  pinMode(PIN_P1_RUN, INPUT_PULLUP);
  pinMode(PIN_P2_RUN, INPUT_PULLUP);
  pinMode(PIN_P1_FAULT, INPUT_PULLUP);
  pinMode(PIN_P2_FAULT, INPUT_PULLUP);
  pinMode(PIN_P1_SEAL, INPUT_PULLUP);
  pinMode(PIN_P2_SEAL, INPUT_PULLUP);

  Serial.println(F("--- HighTide System Boot Complete ---"));
  Serial.println(F("Power Stabilized. Animation Active."));
}

void loop() {
  // Read Inputs
  bool p1Run = !digitalRead(PIN_P1_RUN);
  bool p2Run = !digitalRead(PIN_P2_RUN);
  bool p1Fail = !digitalRead(PIN_P1_FAULT);
  bool p2Fail = !digitalRead(PIN_P2_FAULT);
  bool p1Seal = !digitalRead(PIN_P1_SEAL);
  bool p2Seal = !digitalRead(PIN_P2_SEAL);

  // 1. DRAIN LOGIC (Pick and Stick)
  if (millis() - lastDrainChange >= DRAIN_INTERVAL) {
    currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
    lastDrainChange = millis();
  }

  // 2. PHYSICS & PWM LOGIC (1 Second Interval)
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    
    float p1Inflow = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    float p2Inflow = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    
    float netChangePerSecond = ((p1Inflow + p2Inflow) - currentDrainRate) / 60.0;
    currentGallons += netChangePerSecond;
    currentGallons = constrain(currentGallons, 0, TANK_CAPACITY);

    // PWM Output Scaling
    analogWrite(PWM_TANK_LVL, map(currentGallons, 0, TANK_CAPACITY, 0, 255));
    analogWrite(PWM_FLOW_RATE, map(p1Inflow + p2Inflow, 0, 1000, 0, 255));
    
    int p1A = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    int p2A = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    analogWrite(PWM_P1_AMPS, map(p1A, 0, 100, 0, 255));
    analogWrite(PWM_P2_AMPS, map(p2A, 0, 100, 0, 255));

    // 3. SERIAL REPORT (Every 15 Seconds)
    if (millis() - lastReport >= 15000) {
      lastReport = millis();
      long timeUntilNewDrain = (DRAIN_INTERVAL - (millis() - lastDrainChange)) / 1000;

      Serial.println(F("\n========================================"));
      Serial.println(F("       HIGHTIDE SYSTEM STATUS          "));
      Serial.println(F("========================================"));
      Serial.print(F(" TANK LEVEL:  ")); Serial.print(currentGallons, 0); Serial.println(F(" Gallons"));
      Serial.print(F(" SYSTEM DRAW: ")); Serial.print(currentDrainRate); Serial.print(F(" GPM"));
      Serial.print(F(" (Next Change: ")); Serial.print(timeUntilNewDrain); Serial.println(F("s)"));
      Serial.println(F("----------------------------------------"));
      Serial.print(F(" PUMP 1: ")); 
      if (p1Fail) Serial.print(F("FAULT")); else if (p1Run) Serial.print(p1Seal ? F("RUN (SEAL FAIL)") : F("RUN")); else Serial.print(F("OFF"));
      Serial.print(F(" | Amps: ")); Serial.println(p1A);
      
      Serial.print(F(" PUMP 2: ")); 
      if (p2Fail) Serial.print(F("FAULT")); else if (p2Run) Serial.print(p2Seal ? F("RUN (SEAL FAIL)") : F("RUN")); else Serial.print(F("OFF"));
      Serial.print(F(" | Amps: ")); Serial.println(p2A);
      Serial.println(F("========================================\n"));
    }
  }

  updateLEDs(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDs(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  static unsigned long lastAnim = 0;
  if (millis() - lastAnim < 150) return; 
  lastAnim = millis();
  
  animationFrame = (animationFrame + 1) % 4;
  
  if (fadeDirection) fadeValue += 25; else fadeValue -= 25;
  if (fadeValue >= 250) { fadeValue = 250; fadeDirection = false; }
  if (fadeValue <= 5)   { fadeValue = 5;   fadeDirection = true; }

  strip.clear();

  // --- PUMP ANIMATIONS (Comet Tail) ---
  for (int i = 0; i < 4; i++) {
    // PUMP 1
    if (f1) { strip.setPixelColor(i, strip.Color(fadeValue, 0, 0)); }
    else if (r1) {
      uint32_t color = s1 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0);
      if (i == animationFrame) strip.setPixelColor(i, color);
      else if (i == (animationFrame + 3) % 4) strip.setPixelColor(i, dimColor(color, 0.5));
      else if (i == (animationFrame + 2) % 4) strip.setPixelColor(i, dimColor(color, 0.2));
    }

    // PUMP 2
    int p2_idx = i + 4;
    if (f2) { strip.setPixelColor(p2_idx, strip.Color(fadeValue, 0, 0)); }
    else if (r2) {
      uint32_t color = s2 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0);
      if (i == animationFrame) strip.setPixelColor(p2_idx, color);
      else if (i == (animationFrame + 3) % 4) strip.setPixelColor(p2_idx, dimColor(color, 0.5));
      else if (i == (animationFrame + 2) % 4) strip.setPixelColor(p2_idx, dimColor(color, 0.2));
    }
  }

  // --- TANK LEVEL (Pixels 8 to 35) ---
  int numLeds = map((long)currentGallons, 0, (long)TANK_CAPACITY, 0, 28);
  numLeds = constrain(numLeds, 0, 28);
  for (int i = 0; i < numLeds; i++) {
    strip.setPixelColor(i + 8, strip.Color(0, 0, 255));
  }

  strip.show();
}

uint32_t dimColor(uint32_t color, float multiplier) {
  uint8_t r = (uint8_t)((color >> 16 & 0xFF) * multiplier);
  uint8_t g = (uint8_t)((color >> 8 & 0xFF) * multiplier);
  uint8_t b = (uint8_t)((color & 0xFF) * multiplier);
  return strip.Color(r, g, b);
}