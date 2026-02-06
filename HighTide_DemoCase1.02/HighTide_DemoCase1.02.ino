#include <Adafruit_NeoPixel.h>

// --- CONFIGURABLE PARAMETERS ---
const int LED_BRIGHTNESS   = 255;      // Adjust based on your power supply heat
const unsigned long TANK_CAPACITY = 100000;
const int PUMP_FLOW_SINGLE = 450; 
const int PUMP_FLOW_DUAL   = 350; 
const int PUMP_AMPS_SINGLE = 75;
const int PUMP_AMPS_DUAL   = 65;  

// --- PIN ASSIGNMENTS ---
const int PIN_NEOPIXEL  = 6;
const int PIN_P1_RUN    = 2;   
const int PIN_P2_RUN    = 4;   
const int PIN_P1_FAULT  = 7;   
const int PIN_P2_FAULT  = 8;   
const int PIN_P1_SEAL   = 11;  // New Seal Fail Input
const int PIN_P2_SEAL   = 12;  // New Seal Fail Input

const int PWM_TANK_LVL  = 3;   
const int PWM_FLOW_RATE = 5;  
const int PWM_P1_AMPS   = 9;   
const int PWM_P2_AMPS   = 10;  

Adafruit_NeoPixel strip(39, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

float currentGallons = 50000; 
unsigned long lastUpdate = 0;
int animationFrame = 0;
int fadeValue = 0;
bool fadeDirection = true;

void setup() {
  Serial.begin(9600);
  delay(1000); 
  Serial.println("--- System Booting: HighTide Gen 2 ---"); 
  
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

  // Physics Logic (1 Second Interval)
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    int currentDrain = random(100, 601);
    
    // Seal fail does NOT stop the pump, only a Fault does
    float p1Inflow = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    float p2Inflow = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_FLOW_DUAL : PUMP_FLOW_SINGLE) : 0;
    
    float netChangePerSecond = ((p1Inflow + p2Inflow) - currentDrain) / 60.0;
    currentGallons += netChangePerSecond;
    currentGallons = constrain(currentGallons, 0, TANK_CAPACITY);

    // Update PWM Outputs
    analogWrite(PWM_TANK_LVL, map(currentGallons, 0, TANK_CAPACITY, 0, 255));
    analogWrite(PWM_FLOW_RATE, map(p1Inflow + p2Inflow, 0, 1000, 0, 255));
    
    int p1A = (p1Run && !p1Fail) ? ((p2Run && !p2Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    int p2A = (p2Run && !p2Fail) ? ((p1Run && !p1Fail) ? PUMP_AMPS_DUAL : PUMP_AMPS_SINGLE) : 0;
    analogWrite(PWM_P1_AMPS, map(p1A, 0, 100, 0, 255));
    analogWrite(PWM_P2_AMPS, map(p2A, 0, 100, 0, 255));

    // Telemetry
    Serial.print("Tank: "); Serial.print(currentGallons, 0); Serial.print(" GAL | ");
    Serial.print("Flow: "); Serial.print(p1Inflow + p2Inflow); Serial.print(" GPM | ");
    Serial.print("P1: "); Serial.print(p1A); Serial.print(" Amps | ");
    Serial.print("P2: "); Serial.print(p2A); Serial.println(" Amps");
  }

  updateLEDs(p1Run, p1Fail, p1Seal, p2Run, p2Fail, p2Seal);
}

void updateLEDs(bool r1, bool f1, bool s1, bool r2, bool f2, bool s2) {
  static unsigned long lastAnim = 0;
  if (millis() - lastAnim < 400) return; 
  lastAnim = millis();
  
  animationFrame = (animationFrame + 1) % 4;
  
  if (fadeDirection) fadeValue += 25; else fadeValue -= 25;
  if (fadeValue >= 250) { fadeValue = 250; fadeDirection = false; }
  if (fadeValue <= 5)   { fadeValue = 5;   fadeDirection = true; }

  strip.clear();

  // Colors
  uint32_t green = strip.Color(0, 255, 0);
  uint32_t purple = strip.Color(180, 0, 255); // Red + Blue = Purple
  uint32_t redFade = strip.Color(fadeValue, 0, 0);
  uint32_t blue = strip.Color(0, 0, 255);

  for (int i = 0; i < 4; i++) {
    // Pump 1
    if (f1) strip.setPixelColor(i, redFade);
    else if (r1 && i == animationFrame) {
      strip.setPixelColor(i, s1 ? purple : green);
    }
    
    // Pump 2
    if (f2) strip.setPixelColor(i + 4, redFade);
    else if (r2 && i == animationFrame) {
      strip.setPixelColor(i + 4, s2 ? purple : green);
    }
  }

  // Tank Level
  int numTankLeds = map((long)currentGallons, 0, (long)TANK_CAPACITY, 0, 31);
  numTankLeds = constrain(numTankLeds, 0, 31); 
  for (int i = 0; i < numTankLeds; i++) {
    strip.setPixelColor(i + 8, blue);
  }

  strip.show();
}