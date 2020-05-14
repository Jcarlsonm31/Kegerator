#include <DHT.h>
#include <EEPROM.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"

#define TEMP 3        // temp sensor pin
#define DHTTYPE DHT22 // temp sensor type DHT 22  (AM2302), AM2321
#define TFTBACKLIGHT 13  // TFT LED backlight
#define PIR 7         // PIR infrared sensor
#define FLOWSENSOR 24 // flow meter
#define BUTTON1 5     // lighted button
#define BUTTON1LED 11
#define BUTTON2 6     // lighted button
#define BUTTON2LED 12
#define ROTARYBUTTON 4      // rotary pushbutton
#define ROTARYCLK A0
#define ROTARYDAT A1
#define SCALECLK 14  // load sensor based scale
#define SCALEDOUT 15
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
#define NUMMENUITEMS 3
#define NORMAL 0 // display modes
#define MENU  1 
#define POURING 2 
#define NOOP 0 // rotary encoder return codes
#define RIGHT 1
#define LEFT 2

char mainMenuChoices[NUMMENUITEMS][15] = { "Beer Name", "Reset", "Zero Scale" };
char mainMenuChoice = 0;
unsigned long rotaryCurrentTime;
unsigned long rotaryLoopTime;
unsigned char rotaryEncoder_A;
unsigned char rotaryEncoder_B;
unsigned char rotaryEncoder_A_prev=0;
char alphabet[57] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!?$%&*',.[]()<>/=+ ";
char beerName[21] = "STREET DOG IPA";
int pirState = HIGH;   // we start, assuming no motion detected
int pirVal = 0;       // state variable for reading the PIR pin status
unsigned long pirDelay = 0; // time to wait until dimming backlight
unsigned long tappedDuration = 0; // count of days since last reset (new keg tapped)
unsigned long checkTempDelay = 0;
volatile unsigned long updatePouringDelay = 0;
int currentTemp = 0;
int prevTemp = 0;
float beersRemaining = 40.8;
volatile int luckyBeer = 0;
volatile boolean luckyBeerFound = false;
volatile float currentPour = 0.0;
int tappedDays = 0;
unsigned long rotaryButtonDelay = 0;
unsigned long buttonDelay = 0;
volatile char displayMode = NORMAL;
volatile unsigned long pouringModeDuration = 0;
int cursorPosition = 0;
int alphabetCursorPosition = -1;
volatile boolean tftUpdateNeeded = true;
volatile boolean tftClearNeeded = false;
// use volatile because of intterupt usage
volatile uint16_t pulses = 0;
volatile uint16_t prevpulses = 0;
volatile uint8_t lastflowpinstate; // track the state of the pulse pin
volatile uint32_t lastflowratetimer = 0; // you can try to keep time of how long it is between pulses
volatile float flowrate; // and use that to calculate a flow rate
// Initialize DHT temp sensor.
DHT dht(TEMP, DHTTYPE);

void setup() {
  Serial.begin(9600);
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
  pinMode(ROTARYCLK, INPUT);
  digitalWrite(ROTARYCLK, HIGH);
  pinMode(ROTARYDAT, INPUT);
  digitalWrite(ROTARYDAT, HIGH);
  pinMode(ROTARYBUTTON, INPUT);
  digitalWrite(ROTARYBUTTON, HIGH);
  rotaryCurrentTime = millis();
  rotaryLoopTime = rotaryCurrentTime; 
  lastflowpinstate = digitalRead(FLOWSENSOR);
  useInterrupt(true); // flowmeter
  pirDelay = (millis() / 1000);
  randomSeed(analogRead(2));
  luckyBeer = random(1,40);
  luckyBeerFound = false;
  dht.begin();
  EEPROM.get(0, beerName);
  EEPROM.get(26, tappedDays);
  EEPROM.get(30, beersRemaining);
  tappedDuration = 0; //timeNow.unixtime();
  ResetNormalDisplay();
}

void loop() {
  if (displayMode == MENU) {
    MenuMode();
  } 
  else if (displayMode == POURING) {
    if (tftClearNeeded == true) {
      //lcd.clear();
      //delay(20);
      //tftClearNeeded = false;
    }
    DisplayPouringMode();
  }
  else {
    if ((millis() - checkTempDelay) > 5000) {
      GetTemperature();
      checkTempDelay = millis();
    } 
    CheckMenuMode();    
    CheckPIRSensor();
    //GetTappedDaysAgo();
    //GetInfraredSensor();
  }
}

// 18px char to char
void ResetNormalDisplay() {
  displayMode = NORMAL;
  mainMenuChoice = 0;
  tft.fillScreen(BLACK);
  DrawBeerName();
  DrawBeersRemaining();
  DrawTemp();
  DrawTappedDays();
  DrawKegWeight();
}

void DrawBeerName() {
  tft.setTextSize(3);
  tft.setCursor(10, 20);
  tft.setTextColor(LIGHTGREEN, BLACK);
  tft.print(beerName);
}

void DrawBeersRemaining() {
  tft.setTextSize(3);
  tft.setCursor(10, 80);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Beers remaining:");  
  tft.setCursor(324, 80);
  tft.setTextColor(WHITE, BLACK);
  tft.print(int (beersRemaining));
}

void DrawTemp() {
  tft.setTextSize(3);
  tft.setCursor(10, 140);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Temp:");
  tft.setCursor(126, 140);
  tft.setTextColor(WHITE, BLACK);
  tft.print(currentTemp);
  tft.setCursor(162, 140);
  tft.print("F");
}

void DrawTappedDays() {
  tft.setTextSize(3);
  tft.setCursor(10, 200);
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
      tft.setTextColor(GRAY);
      if (tappedDays == 1) {
        tft.print(" day ago");
      } else {  
        tft.print(" days ago");
      }  
  }
}

void DrawKegWeight() {
  tft.setTextSize(3);
  tft.setCursor(10, 260);
  tft.setTextColor(GRAY, BLACK);
  tft.print("Keg weight:");  
  tft.setCursor(216, 260);
  tft.setTextColor(WHITE, BLACK);
  tft.print("41.14");
}

void MenuMode() {
  DisplayMainMenu();
  MainMenuInput();
  if (digitalRead(ROTARYBUTTON) == LOW) {
    if ((millis() - rotaryButtonDelay) > 500) {
      rotaryButtonDelay = millis();
      ResetNormalDisplay();
    }
  }  
}

void DisplayMainMenu() {
  for (int x=0; x < NUMMENUITEMS; x++) {
    DrawMainMenuChoices(x, mainMenuChoice);  
  }
}

void DrawMainMenuChoices(int currentItem, int menuChoice) {
  tft.setTextSize(3);
  if (currentItem == menuChoice) {
    tft.setTextColor(LIGHTGREEN);    
  } else {
    tft.setTextColor(WHITE);
  }
  tft.setCursor(10, (20+(currentItem * 50)));
  tft.print(currentItem+1);     
  tft.setCursor(22, (20+(currentItem * 50)));
  tft.print(". ");     
  tft.setCursor(46, (20+(currentItem * 50)));
  tft.print(mainMenuChoices[currentItem]);
}

void MainMenuInput() {
  if ((digitalRead(BUTTON1) == HIGH) || (digitalRead(BUTTON2) == HIGH)) {
    if ((millis() - buttonDelay) > 500) {
      buttonDelay = millis();
      switch(mainMenuChoice) {
        case 0 :
          EditBeerName();
          break;
        case 1 :
          ResetNewKeg();          
          break;
        case 2 :
          ZeroScale();
          break;
      }
    }
  } else {
    switch(CheckRotaryEncoder()) {
    case NOOP :
      break;
    case RIGHT :
      mainMenuChoice--;
      if (mainMenuChoice < 0) {
        mainMenuChoice = NUMMENUITEMS-1;
      }
    case LEFT :
      mainMenuChoice++;
      if (mainMenuChoice > NUMMENUITEMS-1) {
        mainMenuChoice = 0;
      }
      break;
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

void GetTappedDaysAgo() {
/*
  timeNow = rtc.now();
  if ((timeNow.unixtime() - tappedDuration) > 86400) {
    tappedDays += 1;
    EEPROM.put(26, tappedDays);
    tappedDuration = timeNow.unixtime();
    tftUpdateNeeded = true;
  }
*/
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
    for (int x=0; x<51; x++) {
      LEDBrightness = LEDBrightness + LEDFadeAmount;
      TFTBrightness = TFTBrightness + TFTFadeAmount;
      analogWrite(BUTTON1LED, LEDBrightness);
      analogWrite(BUTTON2LED, LEDBrightness);
      analogWrite(TFTBACKLIGHT, TFTBrightness);
      delay(30);
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
    digitalWrite(TFTBACKLIGHT, LOW);
  }
}

void CheckMenuMode() { //menu entry mode
  if (digitalRead(ROTARYBUTTON) == LOW) {
    if ((millis() - rotaryButtonDelay) > 500) {
      rotaryButtonDelay = millis();
      displayMode = MENU;
      tft.fillScreen(BLACK);
    }
  }
}

void EditBeerName() {
/*
  lcd.setCursor(cursorPosition,0);
  rotaryCurrentTime = millis();
  if (digitalRead(ROTARYBUTTON) == LOW) {
    if ((millis() - textEntryDuration) > 1000) { // hold for 1 second to exit text entry mode
      textEntryMode = false; 
      lcd.noBlink();
      EEPROM.put(0, beerName);
      tftUpdateNeeded = true;
    } else {
      lcd.setCursor(0, 0);
      lcd.print(beerName);
      cursorPosition += 1;
      if (cursorPosition > 19) {
        cursorPosition = 0;
      }
      alphabetCursorPosition = -1;
      lcd.setCursor(cursorPosition,0);
      delay(300);
    }
  } else if (rotaryCurrentTime >= (rotaryLoopTime + 5)) { // rotary encoder
      // 5ms since last check of encoder = 200Hz  
      rotaryEncoder_A = digitalRead(ROTARYDAT);    // Read encoder pins
      rotaryEncoder_B = digitalRead(ROTARYCLK);   
      if((!rotaryEncoder_A) && (rotaryEncoder_A_prev)){
        // A has gone from high to low 
        if(rotaryEncoder_B) {
          // B is high so clockwise
          alphabetCursorPosition += 1;
          if (alphabetCursorPosition > 55) {
            alphabetCursorPosition = 0;
          }
        }   
        else {
          // B is low so counter-clockwise      
          alphabetCursorPosition -= 1;
          if (alphabetCursorPosition < 0) {
            alphabetCursorPosition = 55;
          }
        }   
        beerName[cursorPosition] = char(alphabet[alphabetCursorPosition]);
        lcd.setCursor(0, 0);
        lcd.print(beerName);
      }   
      rotaryEncoder_A_prev = rotaryEncoder_A;     // Store value of A for next time    
      rotaryLoopTime = rotaryCurrentTime;  // Updates loopTime
  } else {
    textEntryDuration = millis();
  }
*/
}

void ZeroScale() {
  
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
/*
  if ((millis() - updatePouringDelay) > 100) {
    updatePouringDelay = millis();
    if (luckyBeer == int(beersRemaining)) {
      lcd.setCursor(0, 0);    
      lcd.print("YOU GOT THE");
      lcd.setCursor(0, 1);    
      lcd.print("LUCKY BEER!!!");
      luckyBeerFound = true;
    } else {
       lcd.setCursor(0, 0);
       lcd.print("Pouring...");
    }
    lcd.setCursor(0, 2);
    lcd.print("Ounces:");
    lcd.setCursor(8, 2);
    lcd.print(int(floor(currentPour)));
  }
*/
}

void GetBeersRemaining() {
  if (prevpulses != pulses) {
    volatile float liters = pulses - prevpulses;
    liters /= 7.5;
    liters /= 60.0;
    liters /= .473176; // convert to 16 ounce beers
    liters *= .66; // fudge factor to account for too high reading when pouring
    beersRemaining -= liters;
    if (beersRemaining < 0.0) {
      beersRemaining = 0.0;
    }
    prevpulses = pulses;
    EEPROM.put(30, beersRemaining);
  }
}

int CheckRotaryEncoder() {  
  rotaryCurrentTime = millis();
  if (rotaryCurrentTime >= (rotaryLoopTime + 5)) { // rotary encoder
    // 5ms since last check of encoder = 200Hz  
    rotaryEncoder_A = digitalRead(ROTARYDAT);    // Read encoder pins
    rotaryEncoder_B = digitalRead(ROTARYCLK);   
    rotaryLoopTime = rotaryCurrentTime;  // Updates loopTime
    if((!rotaryEncoder_A) && (rotaryEncoder_A_prev)){
      rotaryEncoder_A_prev = rotaryEncoder_A;     // Store value of A for next time    
      // A has gone from high to low 
      if(rotaryEncoder_B) {
        // B is high so clockwise
         return RIGHT;
       } else {
         // B is low so counter-clockwise      
         return LEFT;
       }   
    } else {
      rotaryEncoder_A_prev = rotaryEncoder_A;     // Store value of A for next time    
      return NOOP;   
    }
  }
}
  
// Interrupt is called once a millisecond, looks for any pulses from the flowmeter!
SIGNAL(TIMER0_COMPA_vect) {
  uint8_t x = digitalRead(FLOWSENSOR);
  
  if (x == lastflowpinstate) {
    lastflowratetimer++;
    if ((displayMode == POURING) && ((millis() - pouringModeDuration) > 3000)) { // haven't poured for 5 seconds
      displayMode = NORMAL;
      currentPour = 0.0;
      updatePouringDelay = 0;
      if (luckyBeerFound == true) {
        luckyBeer = 100;      
      }
      GetBeersRemaining();
      tftUpdateNeeded = true;
    }
    return; // nothing changed!
  }
  
  if (x == HIGH) {
    //low to high transition!
    pulses++;
    if (displayMode != POURING) {
      displayMode = POURING;
      tftClearNeeded = true;
    }
    pouringModeDuration = millis();
    currentPour = pulses - prevpulses;
    currentPour /= 7.5;
    currentPour /= 60.0;
    currentPour /= .0295735; // convert to 16 ounce beers
    currentPour *= .85; // fudge factor to account for too high reading when pouring
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
