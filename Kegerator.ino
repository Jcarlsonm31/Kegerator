#include <DHT.h> // temp
#include <EEPROM.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include "HX711.h" // scale

#define ROTARYA 18 // rotary knob A
#define ROTARYB 19 // rotary knob B
volatile byte rotaryAFlag = 0; // let's us know when we're expecting a rising edge on pinA to signal that the encoder has arrived at a detent
volatile byte rotaryBFlag = 0; // let's us know when we're expecting a rising edge on pinB to signal that the encoder has arrived at a detent (opposite direction to when aFlag is set)
volatile byte rotaryReading = 0; //somewhere to store the direct values we read from our interrupt pins before checking to see if we have moved a whole detent
volatile boolean updateRotaryLeft = false;
volatile boolean updateRotaryRight = false;

// pin 5 seems flakey on this particular Mega, don't use it
#define TEMP 48        // temp sensor pin
#define DHTTYPE DHT22 // temp sensor type DHT 22  (AM2302), AM2321
#define TFTBACKLIGHT 13  // TFT LED backlight
#define PIR 7         // PIR infrared sensor
#define FLOWSENSOR 46 // flow meter
#define BUTTON1 14     // lighted button
#define BUTTON1LED 11
#define BUTTON2 15     // lighted button
#define BUTTON2LED 12
#define ROTARYBUTTON 4      // rotary pushbutton
#define SCALECLK A14  // load sensor based scale
#define SCALEDOUT A15
HX711 scale;
#define BACKLIGHTDURATION 60  // time to leave backlight on once triggered
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);
#define BLACK 0x0000
#define RED 0xF800
#define DARKRED 0xC020
#define YELLOW 0xF7E0
#define DARKYELLOW 0xCE80
#define ORANGE 0xFDC4
#define BLUE 0x041F
#define MEDBLUE 0x00DD
#define DARKBLUE 0x02B5
#define PURPLE 0xF81A
#define LIGHTGREEN 0x07E0
#define GREEN 0x07C0
#define MEDGREEN 0x1742
#define DARKGREEN 0x0520
#define TEAL 0x279F
#define TAN 0xFF73
#define WHITE 0xFFFF
#define LIGHTGRAY 0xDEDB
#define MEDGRAY 0xBDF7
#define GRAY 0x9CD3
#define BROWN 0x7421
#define NORMAL 0 // display modes
#define SETUP  1 
#define POURING 2 
#define NOOP 0 // rotary encoder return codes
#define RIGHT 1
#define LEFT 2

#define NUMMENUITEMS 5
char setupMenuChoices[NUMMENUITEMS][22] = { "Beer Name", "Reset Keg", "Calibrate Scale", "Zero Scale", "Calibrate Flowmeter" };
char setupMenuChoice = 0;
char alphabet[83] = "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789-!?$%&*',.[]()<>/=+ ";
char beerName[25] = "STREET DOG IPA          "; // max 24 char beer name
int pirState = HIGH;   // we start, assuming no motion detected
int pirVal = 0;       // state variable for reading the PIR pin status
unsigned long pirDelay = 0; // time to wait until dimming backlight
unsigned long tappedDuration = 0; // count of millis since last reset (new keg)
int tappedDays = 0;
unsigned long checkTempDelay = 0;
volatile unsigned long updatePouringDelay = 0;
int currentTemp = 0;
int prevTemp = 0;
float beersRemaining = 0.0;
long luckyBeer = 0;
boolean luckyBeerFound = false;
volatile float currentPour = 0.0;
unsigned long buttonTimer = 0;
int  buttonDelay = 200;
volatile char displayMode = NORMAL;
volatile unsigned long pouringModeDuration = 0;
int cursorPosition = 0;
int alphabetCursorPosition = -1;
boolean tftFillToggle = false; // flip between black/white to fill screen while backlight off to prevent burn in
volatile boolean clearPouringMode = false;
volatile boolean tftResetNeeded = false;
// use volatile because of intterupt usage
volatile uint16_t pulses = 0;
volatile uint16_t prevpulses = 0;
volatile uint8_t lastflowpinstate; // track the state of the pulse pin
volatile uint32_t lastflowratetimer = 0; // you can try to keep time of how long it is between pulses
volatile float flowrate; // and use that to calculate a flow rate
float flowmeterPulsesPerSecond = 8.8;
float scaleCalibrationFactor = -11030; // calibration offset against a known weight
long scaleZeroFactor = 96000; // offset weight of empty scale to zero scale
float emptyKegWeight = 0.0; // used to offset full keg weight
float currentKegWeight = 0.0;
DHT dht(TEMP, DHTTYPE);

void setup() {
  Serial.begin(9600);
  pinMode(ROTARYA, INPUT_PULLUP);
  pinMode(ROTARYB, INPUT_PULLUP);                              // Reversed Rotary pins from pin definitions prevents skipping
  attachInterrupt(digitalPinToInterrupt(18), RotaryB, RISING); // set an interrupt on ROTARYB, looking for a rising edge signal and executing the "RotaryA" Interrupt Service Routine (below)
  attachInterrupt(digitalPinToInterrupt(19), RotaryA, RISING); // set an interrupt on ROTARYA, looking for a rising edge signal and executing the "RotaryB" Interrupt Service Routine (below)
  tft.begin();
  tft.setRotation(1);
  pinMode(TFTBACKLIGHT, OUTPUT);
  digitalWrite(TFTBACKLIGHT, HIGH);
  pinMode(BUTTON1LED, OUTPUT);
  analogWrite(BUTTON1LED, 55);
  pinMode(BUTTON2LED, OUTPUT);
  analogWrite(BUTTON2LED, 55);
  pinMode(BUTTON1, INPUT);
  digitalWrite(BUTTON1, LOW);
  pinMode(BUTTON2, INPUT);
  digitalWrite(BUTTON2, LOW);
  pinMode(PIR, INPUT);
  pinMode(FLOWSENSOR, INPUT);
  digitalWrite(FLOWSENSOR, HIGH);
  pinMode(ROTARYBUTTON, INPUT);
  digitalWrite(ROTARYBUTTON, HIGH);
  lastflowpinstate = digitalRead(FLOWSENSOR);
  useInterrupt(true); // flowmeter
  pirDelay = (millis() / 1000);
  randomSeed(analogRead(2));
  luckyBeer = random(1,40);
  luckyBeerFound = false;
  tappedDuration = millis();
  dht.begin();
  scale.begin(SCALEDOUT, SCALECLK);
  EEPROM.get(0, beerName);
  EEPROM.get(26, tappedDays);
  EEPROM.get(30, scaleCalibrationFactor);
  EEPROM.get(35, scaleZeroFactor);
  EEPROM.get(40, emptyKegWeight);
  EEPROM.get(45, flowmeterPulsesPerSecond);
  scale.set_scale(scaleCalibrationFactor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.set_offset(scaleZeroFactor); //Zero out the scale using a previously known zero_factor
  ResetNormalDisplay();
}

void loop() {
  if (tftResetNeeded == true) {
    tftResetNeeded = false;
    ResetNormalDisplay();
  }
  else if (displayMode == SETUP) {
    SetupMenuMode();
  } 
  else if (displayMode == POURING) {
    DisplayPouringMode();
  }
  else {
    if ((millis() - checkTempDelay) > 5000) {
      GetTemperature();
      checkTempDelay = millis();
    } 
    CheckPIRSensor();
    GetTappedDays();
    GetBeersRemaining();
    CheckSetupMode();    
  }
}

void ResetNormalDisplay() {
  displayMode = NORMAL;
  setupMenuChoice = 0;
  tft.fillScreen(BLACK);
  DrawBeerName();
  DrawBeersRemaining();
  DrawTemp();
  DrawTappedDays();
  DrawKegWeight();
}

void DrawBeerName() {
  tft.setTextSize(3); // 18px char to char width
  tft.setCursor(20, 20);
  tft.setTextColor(LIGHTGREEN, BLACK);
  tft.print(beerName);
  tft.drawFastHLine(20, 60, 420, GRAY);
}

void DrawBeersRemaining() {
  tft.setTextSize(3);
  tft.setCursor(20, 80);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Beers remaining:");  
  tft.setCursor(324, 80);
  tft.setTextColor(WHITE, BLACK);
  tft.print(beersRemaining, 1);
}

void DrawTemp() {
  tft.setTextSize(3);
  tft.setCursor(20, 140);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Temp:");
  tft.setCursor(126, 140);
  tft.setTextColor(WHITE, BLACK);
  tft.print(currentTemp);
  tft.setCursor(167, 140);
  tft.print("F");
}

void DrawTappedDays() {
  tft.setTextSize(3);
  tft.setCursor(20, 200);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Tapped:");
  tft.setCursor(162, 200);
  tft.setTextColor(WHITE, BLACK);
  if (tappedDays == 0) {
    tft.print("today");
  } else {
      tft.print(tappedDays);
      if (tappedDays > 99) {
        tft.setCursor(216, 200);
      } else if (tappedDays > 9) {
        tft.setCursor(198, 200);
      } else {  
        tft.setCursor(180, 200);  
      }
      tft.setTextColor(GRAY, BLACK);
      if (tappedDays == 1) {
        tft.print(" day ago");
      } else {  
        tft.print(" days ago");
      }  
  }
}

void DrawKegWeight() {
  tft.setTextSize(3);
  tft.setCursor(20, 260);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Keg weight:");  
  tft.setCursor(236, 260);
  tft.setTextColor(WHITE, BLACK);
  tft.print(currentKegWeight, 1);
}

void SetupMenuMode() {
  DisplaySetupMenu();
  SetupMenuInput();
}

void DisplaySetupMenu() {
  for (int x=0; x < NUMMENUITEMS; x++) {
    DrawSetupMenuChoices(x, setupMenuChoice);  
  }
}

void DrawSetupMenuChoices(int currentItem, int menuChoice) {
  tft.setTextSize(3);
  if (currentItem == menuChoice) {
    tft.setTextColor(LIGHTGREEN);
    tft.fillTriangle(20, 20+(currentItem * 50), 28, 29+(currentItem * 50), 20, 38+(currentItem * 50), LIGHTGREEN); 
  } else {
    tft.setTextColor(WHITE);
    tft.fillTriangle(20, 20+(currentItem * 50), 28, 29+(currentItem * 50), 20, 38+(currentItem * 50), BLACK); 
  }
  tft.setCursor(46, (20+(currentItem * 50)));
  tft.print(setupMenuChoices[currentItem]);
}

void SetupMenuInput() {
  if (digitalRead(BUTTON2) == HIGH) {
    if ((millis() - buttonTimer) > buttonDelay) {
      buttonTimer = millis();
      switch(setupMenuChoice) {
        case 0 :
          tft.fillScreen(BLACK);
          EditBeerName();
          break;
        case 1 :
          ResetNewKeg();          
          break;
        case 2 :
          tft.fillScreen(BLACK);
          CalibrateScale();
          break;
        case 3 :
          tft.fillScreen(BLACK);
          ZeroScale();
          break;
        case 4 :
          tft.fillScreen(BLACK);
          CalibrateFlowmeter();
          break;
      }
    }
  } else if (updateRotaryLeft == true) {
      updateRotaryLeft = false;
      setupMenuChoice--;
      if (setupMenuChoice < 0) {
        setupMenuChoice = NUMMENUITEMS-1;
      }
  } else if (updateRotaryRight == true) {
      updateRotaryRight = false;
      setupMenuChoice++;
      if (setupMenuChoice > NUMMENUITEMS-1) {
        setupMenuChoice = 0;
      }
  } else if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(ROTARYBUTTON) == LOW)) {
    if ((millis() - buttonTimer) > buttonDelay) {
      buttonTimer = millis();
      ResetNormalDisplay();
    }
  }      
}

void GetTemperature() {
  currentTemp = int(dht.readTemperature(true)); // Read temperature as Fahrenheit (isFahrenheit = true)
  // Check if temp read failed
  if ((isnan(currentTemp)) || (currentTemp == 0)) {
    currentTemp = prevTemp;
  }
  DrawTemp();
}

void GetTappedDays() {
  if ((unsigned long)(millis() - tappedDuration) > 86400000) { // add 1 day to tappedDays
    tappedDays += 1;
    EEPROM.put(26, tappedDays);
    tappedDuration = millis();
    DrawTappedDays();
  }
}

void CheckPIRSensor() {
  pirVal = digitalRead(PIR);  // read input value
  if (pirVal == HIGH) {            // check if the input is HIGH
    if (pirState == LOW) {
      pirState = HIGH;
      pirDelay = (millis() / 1000);      
      FadeLEDs(true); // turn LEDs ON
    }
  } else {
      if ((millis() / 1000) - pirDelay > BACKLIGHTDURATION) {
        if (pirState == HIGH){
          pirState = LOW;
          FadeLEDs(false); // turn LEDs OFF
        }
      }
  }
}

void FadeLEDs(bool OnOff) {
  int LEDBrightness = 0;
  int LEDFadeAmount = 1;  // try to fade up to less than 100%
  int TFTBrightness = 0;
  int TFTFadeAmount = 5;
  if (OnOff == true) { // fade on
    ResetNormalDisplay();
    for (int x=0; x<51; x++) {
      LEDBrightness = LEDBrightness + LEDFadeAmount;
      TFTBrightness = TFTBrightness + TFTFadeAmount;
      analogWrite(BUTTON1LED, LEDBrightness);
      analogWrite(BUTTON2LED, LEDBrightness);
      analogWrite(TFTBACKLIGHT, TFTBrightness);
      delay(20);
    }
    analogWrite(BUTTON1LED, 55);
    analogWrite(BUTTON2LED, 55);
    digitalWrite(TFTBACKLIGHT, HIGH);
  } else { // fade off
    LEDBrightness = 51;
    TFTBrightness = 255;
    for (int x=0; x<51; x++) {
      LEDBrightness = LEDBrightness - LEDFadeAmount;
      TFTBrightness = TFTBrightness - TFTFadeAmount;
      analogWrite(BUTTON1LED, LEDBrightness);
      analogWrite(BUTTON2LED, LEDBrightness);
      analogWrite(TFTBACKLIGHT, TFTBrightness);
      delay(30);
    }
    digitalWrite(BUTTON1LED, LOW);
    digitalWrite(BUTTON2LED, LOW);
    if (tftFillToggle == false) {
      tft.fillScreen(WHITE);      
      tftFillToggle = true;
    } else {
      tft.fillScreen(BLACK);
      tftFillToggle = false;
    }
    digitalWrite(TFTBACKLIGHT, LOW);
  }
}

void CheckSetupMode() { // setup menu entry mode
  if (digitalRead(ROTARYBUTTON) == LOW) {
    if ((millis() - buttonTimer) > buttonDelay) {
      buttonTimer = millis();
      displayMode = SETUP;
      tft.fillScreen(BLACK);
      updateRotaryLeft = false;
      updateRotaryRight = false;    }
  }
}

void EditBeerName() {
  bool doneEditing = false;
  bool editChar = false;
  char tmpbeerName[25];
  strncpy(tmpbeerName, beerName, 25);
  tft.drawFastHLine(20, 60, 420, GRAY);
  tft.setTextSize(2);
  tft.setTextColor(GRAY);
  tft.setCursor(20, 200);
  tft.print("Right button: edit/save character");
  tft.setCursor(20, 240);
  tft.print("Left button:  cancel edit/save name");
  tft.setCursor(20, 280);
  tft.print("Rotary dial:  change character");
  tft.setTextSize(3);
  tft.setTextColor(WHITE);
  tft.setCursor(20, 20);
  tft.print(beerName);
  cursorPosition = 1;
  while (!doneEditing) {
    if (editChar == true) {
      tft.fillRect(18*cursorPosition, 16, 20, 29, DARKRED);
    } else {
      tft.drawRect(18*cursorPosition, 16, 20, 29, LIGHTGRAY);
    }
    tft.setCursor(20, 20);
    if (editChar == false) {
      tft.print(beerName);    
    } else {
      tft.print(tmpbeerName);          
    }
    if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(ROTARYBUTTON) == LOW)) {
      if ((millis() - buttonTimer) > buttonDelay) {
        buttonTimer = millis();
        if (editChar == false) { // save name and exit
          EEPROM.put(0, beerName);
          tft.fillScreen(BLACK);
          doneEditing = true;
        } else { // cancel editing current char
          tft.fillRect(18*cursorPosition, 16, 20, 29, BLACK);
          alphabetCursorPosition = -1;
          editChar = false;
        }
      }
    } else if (digitalRead(BUTTON2) == HIGH) {
      if ((millis() - buttonTimer) > buttonDelay) {
        buttonTimer = millis();
        if (editChar == false) { // enter char edit mode
          strncpy(tmpbeerName, beerName, 25);
          alphabetCursorPosition = -1;
        } else {
          strncpy(beerName, tmpbeerName, 25);
          tft.fillRect(18*cursorPosition, 16, 20, 29, BLACK);
        }
        editChar = !editChar;
      }
    } else if (updateRotaryLeft == true) {
        updateRotaryLeft = false;
        if (editChar == true) {
          alphabetCursorPosition -= 1;
          if (alphabetCursorPosition < 0) {
            alphabetCursorPosition = 81;
          }          
          tmpbeerName[cursorPosition-1] = char(alphabet[alphabetCursorPosition]);
        } else {
          tft.drawRect(18*cursorPosition, 16, 20, 29, BLACK);
          cursorPosition--;
          if (cursorPosition < 1) {
            cursorPosition = 24;
          }
        }
    } else if (updateRotaryRight == true) {
        updateRotaryRight = false;
        if (editChar == true) {
          alphabetCursorPosition += 1;
          if (alphabetCursorPosition > 81) {
            alphabetCursorPosition = 0;
          }          
          tmpbeerName[cursorPosition-1] = char(alphabet[alphabetCursorPosition]);
        } else {
          tft.drawRect(18*cursorPosition, 16, 20, 29, BLACK);
          cursorPosition++;
          if (cursorPosition > 24) {
            cursorPosition = 1;
          }
        }
    }    
  }
}

void CalibrateScale() {
  bool doneEditing = false;
  float tmpCalibrationFactor = scaleCalibrationFactor;
  scaleZeroFactor = scale.read_average(); //Get a baseline zero reading with no weight
  EEPROM.put(35, scaleZeroFactor); //Store baseline reading
  scale.set_offset(scaleZeroFactor); //Zero out the scale using a previously known zero_factor
  tft.setTextSize(2);
  tft.setTextColor(GRAY);
  tft.setCursor(20, 160);
  tft.print("Wait then place known weight on scale");
  tft.setCursor(20, 200);
  tft.print("Right button: save calibration");
  tft.setCursor(20, 240);
  tft.print("Left button:  cancel");
  tft.setCursor(20, 280);
  tft.print("Rotary dial:  calibrate");
  while (!doneEditing) {
    scale.set_scale(tmpCalibrationFactor); //Adjust to this calibration factor
    tft.setTextSize(3);
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 20);
    tft.print("Weight:");
    tft.setCursor(162, 20);
    tft.setTextColor(WHITE, BLACK);
    tft.print(scale.get_units(), 1);
    tft.setCursor(244, 20);
    tft.print("lbs");
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 70);
    tft.print("Calibration:");
    tft.setCursor(252, 70);
    tft.setTextColor(LIGHTGREEN, BLACK);
    tft.print(int(tmpCalibrationFactor));
    if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(ROTARYBUTTON) == LOW)) {
      if ((millis() - buttonTimer) > buttonDelay) {
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    } else if (digitalRead(BUTTON2) == HIGH) {
      if ((millis() - buttonTimer) > buttonDelay) {
        scaleCalibrationFactor = tmpCalibrationFactor;
        EEPROM.put(30, scaleCalibrationFactor);
        scale.set_scale(scaleCalibrationFactor);
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    } else if (updateRotaryLeft == true) {
      tmpCalibrationFactor += 10;
      updateRotaryLeft = false;
    } else if (updateRotaryRight == true) {
      tmpCalibrationFactor -= 10;
      updateRotaryRight = false;
    }    
  }
}

void ZeroScale() {
  bool doneEditing = false;
  float tmpEmptyKegWeight = emptyKegWeight;
  scale.tare(); // zero scale
  tft.setTextSize(2);
  tft.setTextColor(GRAY);
  tft.setCursor(20, 200);
  tft.print("Wait then place empty keg on scale");
  tft.setCursor(20, 240);
  tft.print("Right button: save weight");
  tft.setCursor(20, 280);
  tft.print("Left button:  cancel");
  while (!doneEditing) {
    tmpEmptyKegWeight = scale.get_units();
    tft.setTextSize(3);
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 20);
    tft.print("Previous:");
    tft.setCursor(198, 20);
    tft.setTextColor(WHITE, BLACK);
    tft.print(emptyKegWeight, 1);
    tft.setCursor(280, 20);
    tft.print("lbs");
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 70);
    tft.print("Empty Keg:");
    tft.setCursor(216, 70);
    tft.setTextColor(LIGHTGREEN, BLACK);
    tft.print(tmpEmptyKegWeight, 1);
    tft.setTextColor(WHITE, BLACK);
    tft.setCursor(298, 70);
    tft.print("lbs");
    if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(ROTARYBUTTON) == LOW)) {
      if ((millis() - buttonTimer) > buttonDelay) {
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    } else if (digitalRead(BUTTON2) == HIGH) {
      if ((millis() - buttonTimer) > buttonDelay) {
        emptyKegWeight = tmpEmptyKegWeight;
        EEPROM.put(40, emptyKegWeight);
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    }    
  }  
}

void CalibrateFlowmeter() { // Modify pulses/sec up to decrease flow reading, down to increase reading (8.8 to start)
  bool doneEditing = false;
  float tmpFlowmeterPulsesPerSecond = flowmeterPulsesPerSecond;
  tft.setTextSize(2);
  tft.setTextColor(GRAY);
  tft.setCursor(20, 160);
  tft.print("Increase value to decrease pour rate");
  tft.setCursor(20, 200);
  tft.print("Right button: save calibration");
  tft.setCursor(20, 240);
  tft.print("Left button:  cancel");
  tft.setCursor(20, 280);
  tft.print("Rotary dial:  calibrate");
  while (!doneEditing) {
    tft.setTextSize(3);
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 20);
    tft.print("Current:");
    tft.setCursor(180, 20);
    tft.setTextColor(WHITE, BLACK);
    tft.print(flowmeterPulsesPerSecond, 1);
    tft.setTextColor(GRAY, BLACK);
    tft.setCursor(20, 70);    tft.print("Calibration:");
    tft.setCursor(252, 70);
    tft.setTextColor(LIGHTGREEN, BLACK);
    tft.print(tmpFlowmeterPulsesPerSecond, 1);
    if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(ROTARYBUTTON) == LOW)) {
      if ((millis() - buttonTimer) > buttonDelay) {
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    } else if (digitalRead(BUTTON2) == HIGH) {
      if ((millis() - buttonTimer) > buttonDelay) {
        flowmeterPulsesPerSecond = tmpFlowmeterPulsesPerSecond;
        EEPROM.put(45, flowmeterPulsesPerSecond);
        buttonTimer = millis();
        tft.fillScreen(BLACK);
        doneEditing = true;
      }
    } else if (updateRotaryLeft == true) {
      tmpFlowmeterPulsesPerSecond -= 0.1;
      updateRotaryLeft = false;
    } else if (updateRotaryRight == true) {
      tmpFlowmeterPulsesPerSecond += 0.1;
      updateRotaryRight = false;
    }    
  }  
}

void ResetNewKeg() {
  tappedDuration = 0;
  tappedDays = 0;
  EEPROM.put(26, tappedDays);
  beersRemaining = 40.8;
  EEPROM.put(30, beersRemaining);
  randomSeed(analogRead(2));
  luckyBeer = random(1,40);
  luckyBeerFound = false;
  ResetNormalDisplay();
}

void DisplayPouringMode() {
  if (clearPouringMode == true) {
    tft.fillScreen(BLACK);
    clearPouringMode = false;
    tft.setTextSize(4);
    if ((luckyBeer == long(beersRemaining)) && (luckyBeerFound == false)) {
      tft.setCursor(75, 40);
      tft.setTextColor(LIGHTGREEN, BLACK);
      tft.print("You found the");  
      tft.setCursor(75, 100);
      tft.print("LUCKY BEER!!!");
      luckyBeerFound = true;
    } else {
      tft.setCursor(115, 80);
      tft.setTextColor(WHITE, BLACK);
      tft.print("Pouring...");  
    }
  }
  if ((millis() - updatePouringDelay) > 100) {
    updatePouringDelay = millis();
    tft.setTextSize(4);
    tft.setCursor(185, 180);
    tft.setTextColor(WHITE, BLACK);
    if (displayMode == POURING) {
      tft.print(int(floor(currentPour)));  
      tft.print("oz");
    }
  }
}

void GetBeersRemaining() {
  float zeroedWeight;
  currentKegWeight = scale.get_units(); //Get a baseline zero reading with no weight
  zeroedWeight = currentKegWeight - emptyKegWeight;
  if (zeroedWeight < 0.0) {
    zeroedWeight = 0.0;
  }
  beersRemaining = (zeroedWeight / 1.043); // 1.043lbs per 16oz beer
  DrawBeersRemaining();
  DrawKegWeight();
}

// Handle rotary encoder interrupts
void RotaryA(){
  rotaryReading = PIND & 0xC; // read all eight pin values then strip away all but pinA and pinB's values
  if(rotaryReading == B00001100 && rotaryAFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    updateRotaryRight = true;
    rotaryBFlag = 0; //reset flags for the next turn
    rotaryAFlag = 0; //reset flags for the next turn
  }
  else if (rotaryReading == B00000100) {
    rotaryBFlag = 1; //signal that we're expecting pinB to signal the transition to detent from free rotation
  }
}

void RotaryB(){
  rotaryReading = PIND & 0xC; //read all eight pin values then strip away all but pinA and pinB's values
  if (rotaryReading == B00001100 && rotaryBFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    updateRotaryLeft = true;
    rotaryBFlag = 0; //reset flags for the next turn
    rotaryAFlag = 0; //reset flags for the next turn
  }
  else if (rotaryReading == B00001000) {
    rotaryAFlag = 1; //signal that we're expecting pinA to signal the transition to detent from free rotation
  }
}

// Flowmeter Interrupt is called once a millisecond, looks for any pulses from the flowmeter!
SIGNAL(TIMER0_COMPA_vect) {
  uint8_t x = digitalRead(FLOWSENSOR);  
  if (x == lastflowpinstate) {
    lastflowratetimer++;
    if ((displayMode == POURING) && ((millis() - pouringModeDuration) > 2000)) { // haven't poured for 2 seconds
      prevpulses = pulses;
      currentPour = 0.0;
      updatePouringDelay = 0;
      displayMode = NORMAL;
      tftResetNeeded = true;
    }
    return; // nothing changed!
  }
  if (x == HIGH) { //low to high transition!
    pulses++;
    if (displayMode != POURING) {
      displayMode = POURING;
      clearPouringMode = true;
    }
    pouringModeDuration = millis();
    currentPour = pulses - prevpulses;
    currentPour /= flowmeterPulsesPerSecond;  //Increase 7.5 for lower output pour reading, decrease for higher output reading. (try 8.8 to start) 
    currentPour /= 60.0;
    currentPour /= .0295735; // convert to 16 ounce beers
  }
  lastflowpinstate = x;
  flowrate = 1000.0;
  flowrate /= lastflowratetimer;  // in hertz
  lastflowratetimer = 0;
}

void useInterrupt(boolean v) {
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
  }
}
