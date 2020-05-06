// include the library code:
#include <DHT.h>
#include <EEPROM.h>
#include <SPI.h>

#define TEMP 3        // temp sensor pin
#define DHTTYPE DHT22 // temp sensor type DHT 22  (AM2302), AM2321
#define BACKLIGHT 14  // LED backlight
#define PIR 26         // PIR infrared sensor
#define ROTARYBUTTON 5      // rotary pushbutton
#define FLOWSENSORPIN 24
#define ROTARYCLK A0
#define ROTARYDAT A1
#define BACKLIGHTDURATION 60  // time to leave backlight on once triggered

unsigned long rotaryCurrentTime;
unsigned long rotaryLoopTime;
unsigned char rotaryEncoder_A;
unsigned char rotaryEncoder_B;
unsigned char rotaryEncoder_A_prev=0;
char alphabet[57] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!?$%&*',.[]()<>/=+ ";
char beerName[21] = "STREET DOG IPA";
int rotaryButtonState = HIGH;
int pirState = LOW;   // we start, assuming no motion detected
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
boolean textEntryMode = false;
unsigned long textEntryDuration = 0;
volatile boolean pouringMode = false;
volatile unsigned long pouringModeDuration = 0;
int cursorPosition = 0;
int alphabetCursorPosition = -1;
volatile boolean displayUpdateNeeded = true;
volatile boolean lcdClearNeeded = false;
// use volatile because of intterupt usage
volatile uint16_t pulses = 0;
volatile uint16_t prevpulses = 0;
volatile uint8_t lastflowpinstate; // track the state of the pulse pin
volatile uint32_t lastflowratetimer = 0; // you can try to keep time of how long it is between pulses
volatile float flowrate; // and use that to calculate a flow rate

// initialize the library with the numbers of the interface pins

// Initialize DHT temp sensor.
DHT dht(TEMP, DHTTYPE);

void setup() {
  Serial.begin(9600);
  pinMode(BACKLIGHT, OUTPUT);
  pinMode(PIR, INPUT);
  digitalWrite(BACKLIGHT, LOW);
  pinMode(FLOWSENSORPIN, INPUT);
  digitalWrite(FLOWSENSORPIN, HIGH);
  pinMode(ROTARYCLK, INPUT);
  digitalWrite(ROTARYCLK, HIGH);
  pinMode(ROTARYDAT, INPUT);
  digitalWrite(ROTARYDAT, HIGH);
  pinMode(ROTARYBUTTON, INPUT);
  digitalWrite(ROTARYBUTTON, HIGH);
  rotaryCurrentTime = millis();
  rotaryLoopTime = rotaryCurrentTime; 
  lastflowpinstate = digitalRead(FLOWSENSORPIN);
  useInterrupt(true); // flowmeter
  pirDelay = (millis() / 1000);
  randomSeed(analogRead(2));
  luckyBeer = random(1,40);
  luckyBeerFound = false;
  dht.begin();
  EEPROM.get(0, beerName);
  EEPROM.get(21, tappedDays);
  EEPROM.get(24, beersRemaining);
  checkTempDelay = millis();
  tappedDuration = timeNow.unixtime();
}

void loop() {
  if (textEntryMode == true) {
    BeerEntryMode();
  } 
  else if (pouringMode == true) {
    
    if (lcdClearNeeded == true) {
      lcd.clear();
      delay(20);
      lcdClearNeeded = false;
    }
    DisplayPouringMode();
  }
  else {
    if (displayUpdateNeeded == true) {
      UpdateDisplay();
    }  
    if ((millis() - checkTempDelay) > 1000) {
      GetTemperature();
      GetTappedDaysAgo();
      checkTempDelay = millis();
    }
    GetInfraredSensor();
    CheckBeerNameEntryButton();
    CheckResetButton();
  }
}


void GetTemperature() {
  currentTemp = int(dht.readTemperature(true)); // Read temperature as Fahrenheit (isFahrenheit = true)
  // Check if temp read failed
  if ((isnan(currentTemp)) || (currentTemp == 0)) {
    currentTemp = prevTemp;
  }
  if (currentTemp != prevTemp) {
    prevTemp = currentTemp;
    displayUpdateNeeded = true;
  }
}

void GetTappedDaysAgo() {
  timeNow = rtc.now();
  if ((timeNow.unixtime() - tappedDuration) > 86400) {
    tappedDays += 1;
    EEPROM.put(21, tappedDays);
    tappedDuration = timeNow.unixtime();
    displayUpdateNeeded = true;
  }
}

void GetInfraredSensor() {
  pirVal = digitalRead(PIR);  // read input value
  if (pirVal == HIGH) {            // check if the input is HIGH
    if (pirState == LOW) {
      pirState = HIGH;
      digitalWrite(BACKLIGHT, HIGH);  // turn LED ON
      pirDelay = (millis() / 1000);      
    }
  } else {
      if ((millis() / 1000) - pirDelay > BACKLIGHTDURATION) {
        if (pirState == HIGH){
          digitalWrite(BACKLIGHT, LOW); // turn LED OFF
          pirState = LOW;
        }
      }
  }
}

void CheckBeerNameEntryButton() {
  rotaryButtonState = digitalRead(ROTARYBUTTON);
  if (rotaryButtonState == LOW) {
    if ((millis() - textEntryDuration) > 3000) { // delay from exiting text entry mode before entering again
      textEntryMode = true;
      lcd.clear();
      delay(20);
      lcd.print(beerName);
      lcd.blink();
      cursorPosition = 19 ;
      textEntryDuration = millis();
    }
  }
}

void BeerEntryMode() {
  lcd.setCursor(cursorPosition,0);
  rotaryButtonState = digitalRead(ROTARYBUTTON);
  rotaryCurrentTime = millis();
  if (rotaryButtonState == LOW) {
    if ((millis() - textEntryDuration) > 1000) { // hold for 1 second to exit text entry mode
      textEntryMode = false; 
      lcd.noBlink();
      EEPROM.put(0, beerName);
      displayUpdateNeeded = true;
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
}

void Reset() {
      timeNow = rtc.now();
      tappedDuration = timeNow.unixtime();
      beersRemaining = 40.8;
      EEPROM.put(24, beersRemaining);
      tappedDays = 0;
      EEPROM.put(21, tappedDays);
      randomSeed(analogRead(2));
      luckyBeer = random(1,40);
      luckyBeerFound = false;
      displayUpdateNeeded = true;
}

void UpdateDisplay() {
  lcd.begin(20, 4);
  delay(20);
  lcd.print(beerName);
  lcd.setCursor(0, 1);
  lcd.print(int (beersRemaining));
  lcd.setCursor(3, 1);
  lcd.print("beers remaining");  
  lcd.setCursor(0, 2);
  lcd.print("Tapped ");
  lcd.setCursor(7, 2);
  if (tappedDays == 0) {
    lcd.print("today");
  } else {
      lcd.print(tappedDays);
      if (tappedDays > 99) {
        lcd.setCursor(10, 2);
      } else if (tappedDays > 9) {
        lcd.setCursor(9, 2);
      } else {  
        lcd.setCursor(8, 2);  
      }
      if (tappedDays == 1) {
        lcd.print(" day ago");
      } else {  
        lcd.print(" days ago");
      }
    }  
  lcd.setCursor(0, 3);
  lcd.print("Temp:");
  lcd.setCursor(6, 3);
  lcd.print(int(currentTemp));
  lcd.setCursor(8, 3);
  lcd.print("F");
  if (rtcBatteryDead == true) {
    lcd.setCursor(10, 3);    
    lcd.print("Batt Dead");
  }
  displayUpdateNeeded = false;
}

void DisplayPouringMode() {
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
    EEPROM.put(24, beersRemaining);
  }
}

// Interrupt is called once a millisecond, looks for any pulses from the flowmeter!
SIGNAL(TIMER0_COMPA_vect) {
  uint8_t x = digitalRead(FLOWSENSORPIN);
  
  if (x == lastflowpinstate) {
    lastflowratetimer++;
    if ((pouringMode == true) && ((millis() - pouringModeDuration) > 3000)) { // haven't poured for 5 seconds
      pouringMode = false;
      currentPour = 0.0;
      updatePouringDelay = 0;
      if (luckyBeerFound == true) {
        luckyBeer = 100;      
      }
      GetBeersRemaining();
      displayUpdateNeeded = true;
    }
    return; // nothing changed!
  }
  
  if (x == HIGH) {
    //low to high transition!
    pulses++;
    if (pouringMode == false) {
      pouringMode = true;
      lcdClearNeeded = true;
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
