#include <Adafruit_NeoPixel.h>

// --- CONFIGURABLE PARAMETERS ---
const int LED_BRIGHTNESS   = 200;      
const unsigned long TANK_CAPACITY = 100000;
const int PUMP_FLOW_SINGLE = 450; 
const int PUMP_FLOW_DUAL   = 350; 
const int PUMP_AMPS_SINGLE = 75;
const int PUMP_AMPS_DUAL   = 65;  

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
float netFlowRate = 0;              // Tracks GPM in vs out
unsigned long lastUpdate = 0;
unsigned long lastDrainChange = 0;  
unsigned long lastReport = 0;
int animationFrame = 0;
int tankAnimFrame = 0;              // For the flowing water effect
int fadeValue = 0;
bool fadeDirection = true;

void setup() {
  delay(2000); 
  Serial.begin(9600);
  randomSeed(analogRead(A0)); 
  currentDrainRate = random(DRAIN_MIN, DRAIN_MAX + 1);
  lastDrainChange = millis();

  strip.begin();
  strip.setBrightness(0); 
  strip.show();           
  delay(100);             
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

    if (millis() - lastReport >= 15000) {
      lastReport = millis();
      Serial.println(F("\n--- HIGHTIDE STATUS ---"));
      Serial.print(F("Level: ")); Serial.print(currentGallons, 0); Serial.print(F(" | Net Flow: ")); Serial.println(netFlowRate);
    }
  }

  updateLEDs(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDs(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  static unsigned long lastAnim = 0;
  // Dynamic speed: the more flow, the faster the tank animation
  int animSpeed = map(abs(netFlowRate), 0, 1000, 200, 50); 
  if (millis() - lastAnim < 100) return; 
  lastAnim = millis();
  
  animationFrame = (animationFrame + 1) % 4;
  
  // Tank animation moves based on flow direction
  if (netFlowRate > 50) tankAnimFrame--; // Filling: Up
  else if (netFlowRate < -50) tankAnimFrame++; // Draining: Down
  if (tankAnimFrame < 0) tankAnimFrame = 3;
  if (tankAnimFrame > 3) tankAnimFrame = 0;

  if (fadeDirection) fadeValue += 25; else fadeValue -= 25;
  if (fadeValue >= 250) { fadeValue = 250; fadeDirection = false; }
  if (fadeValue <= 5)   { fadeValue = 5;   fadeDirection = true; }

  strip.clear();

  // --- PUMP ANIMATIONS ---
  for (int i = 0; i < 4; i++) {
    if (f1) strip.setPixelColor(i, strip.Color(fadeValue, 0, 0));
    else if (r1) {
      uint32_t color = s1 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0);
      if (i == animationFrame) strip.setPixelColor(i, color);
      else if (i == (animationFrame + 3) % 4) strip.setPixelColor(i, dimColor(color, 0.5));
    }
    int p2_idx = i + 4;
    if (f2) strip.setPixelColor(p2_idx, strip.Color(fadeValue, 0, 0));
    else if (r2) {
      uint32_t color = s2 ? strip.Color(180, 0, 255) : strip.Color(0, 255, 0);
      if (i == animationFrame) strip.setPixelColor(p2_idx, color);
      else if (i == (animationFrame + 3) % 4) strip.setPixelColor(p2_idx, dimColor(color, 0.5));
    }
  }

  // --- TANK LEVEL (Pixels 8 to 35) ---
  int numLeds = map((long)currentGallons, 0, (long)TANK_CAPACITY, 0, 28);
  numLeds = constrain(numLeds, 0, 28);

  // Aqua color shifting logic (Sine wave)
  float wave = sin(millis() / 3000.0); // 3 second cycle
  int gVal = map(wave * 100, -100, 100, 20, 180); // Mix in greens/cyans
  uint32_t aquaColor = strip.Color(0, gVal, 255);

  for (int i = 0; i < numLeds; i++) {
    // Flow chase effect: every 4th pixel is brighter
    if ((i + tankAnimFrame) % 4 == 0 && abs(netFlowRate) > 50) {
      strip.setPixelColor(i + 8, strip.Color(100, 255, 255)); // Bright spark
    } else {
      strip.setPixelColor(i + 8, aquaColor);
    }
  }

  strip.show();
}

uint32_t dimColor(uint32_t color, float multiplier) {
  uint8_t r = (uint8_t)((color >> 16 & 0xFF) * multiplier);
  uint8_t g = (uint8_t)((color >> 8 & 0xFF) * multiplier);
  uint8_t b = (uint8_t)((color & 0xFF) * multiplier);
  return strip.Color(r, g, b);
}