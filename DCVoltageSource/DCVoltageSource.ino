/*
MIT License

Copyright (c) 2024 Tom Wilkinson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Version 1.0.0
// NOTE: whenever you see volt it is millivolts
// This is my first C++ program and I was in a hurry, don't assume anyting here is the corect C++ way

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include "Adafruit_AD569x.h" // Adafruit AD5693R 16-Bit DAC Breakout Board
#include <SPI.h>
#include <Fonts/FreeSans12pt7b.h>
#include <cmath>
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>


const bool DEBUG = true;

#define SS_SWITCH        24
#define SS_NEOPIX        6

#define SEESAW_ADDR      0x37

#define DAC_PORT 0x4C // If A0 jumper is set high, use 0x4E

const uint16_t FULL_SCALE = 65536;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);
Adafruit_AD569x ad5693;

const int INIT_VOLTS = 0;
const int  LOW_VOLTS = 0;
const int HIGH_VOLTS = 2500;

static int voltsInc = 100;
static int currentVolts = INIT_VOLTS;
static bool   voltsChanged = true;
static uint16_t dacD;
static uint16_t offset;

enum mode { Normal, ValueList, ValueListOffsetAdjuct };

static mode currentMode = Normal;
static mode previousMode = Normal;

struct VoltsOffset
{
    int volts;
    uint16_t offset;
};

#define TESTVALUESSIZE 12

static VoltsOffset TestVoltages[TESTVALUESSIZE] = { 
  { 1, 21 }, 
  { 5, 21 },
  { 10, 21 },
  { 25, 22 },
  { 50, 21 },
  { 100, 21 },
  { 250, 19 },
  { 500, 18 },
  { 1000, 15 },
  { 1500, 11 },
  { 2000, 8 },
  { 2500, 0 }  
};

static int testValueIndex = 0;

const long double dacVoltsPerBit = 0.038146973;

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

void setup() {

  //Serial.begin(9600);
  //delay(1000);
  //while (!Serial) delay(10);

  // turn on backlite
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // turn on the TFT / I2C power supply
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // initialize TFT
  tft.init(135, 240); // Init ST7789 240x135
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  canvas.setFont(&FreeSans12pt7b);
  canvas.setTextColor(ST77XX_WHITE); 

  if (! ss.begin(SEESAW_ADDR) || ! sspixel.begin(SEESAW_ADDR)) {
    errMessage("seesaw config fail");
    halt();
  }

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    errMessage("seesaw bad firmware " + version);
    halt();
  }

   // use a pin for the built in encoder switch
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);

  delay(10);
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
  ss.enableEncoderInterrupt();

  if (!ad5693.begin(DAC_PORT, &Wire)) { 
    errMessage("DAC Missing");
    halt();
  }
  ad5693.reset();                // UseRef, 2x
  if (!ad5693.setMode(NORMAL_MODE, true, false)) {
    errMessage("DAC config fail");
    halt(); // Halt
  }
  Wire.setClock(800000);
  if (!ad5693.writeUpdateDAC(0)) {
    errMessage("DAC updt fail");
    halt();
  }  
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLDOWN);
  pinMode(2, INPUT_PULLDOWN);

  sspixel.setBrightness(5);
  sspixel.setPixelColor(0, 0, 255, 0);
  sspixel.show();

  updateScreen();
}

void errMessage(String x) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(0, 24);
  canvas.setTextSize(1);
  canvas.print(x);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);  
}

void halt() {
  while (1) delay(100); // Halt
}

void updateScreen() {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextColor(ST77XX_WHITE);
  /* if (DEBUG) {
    canvas.setCursor(0, 25); 
    canvas.setTextSize(1);  
    canvas.print(message);    
  } */
  canvas.setCursor(0, 75);
  canvas.setTextSize(2);
  canvas.print("mV: ");
  canvas.println(currentVolts);
  canvas.setTextSize(1);
  if (DEBUG) {
    canvas.print((uint16_t) dacD);
    canvas.print("  ");
    canvas.print(offset);
    canvas.print("  "); 
  }  
  canvas.print(voltsInc);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
}

void normalModeProcess(bool modeSwitch) {
  if (!ss.digitalRead(SS_SWITCH)) {
    if (voltsInc == 1) {
      voltsInc = 10;
    } else if (voltsInc == 10) {
      voltsInc = 100;
    } else if (voltsInc == 100) {
      voltsInc = 1000;
    } else if (voltsInc == 1000) {
      voltsInc = 1;
    } else {
      voltsInc = 1;
    }
    updateScreen();
  } 
  int32_t encoder = ss.getEncoderDelta();  
  if (encoder > 0) {
    if (currentVolts <= HIGH_VOLTS - voltsInc) {
      currentVolts = currentVolts + voltsInc;
      voltsChanged = true;      
    }    
  }       
  if (encoder < 0) {
    if (currentVolts >= LOW_VOLTS + voltsInc) {
      currentVolts = currentVolts - voltsInc;
      voltsChanged = true;         
    }
  }
  if (voltsChanged || modeSwitch) {
    uint16_t o = 0;    
    if (currentVolts == 11) {
      o = 22;    
    } else if (currentVolts == 16) {
      o = 22;
    } else if (currentVolts == 20) {
      o = 22;      
    } else if (currentVolts == 25) {
      o = 22;      
    } else if (currentVolts == 30) {
      o = 22;      
    } else if (currentVolts < 180) {
      o = 21;                
    } else if (currentVolts <= 240) {
      o = 20;
    } else if (currentVolts < 400) {
      o = 19;        
    } else if (currentVolts < 600) {
      o = 18;
    } else if (currentVolts < 800) {
      o = 17;
    } else if (currentVolts <= 1000) {
      o = 15;      
    } else if (currentVolts < 1220) {
      o = 14;
    } else if (currentVolts <= 1300) {
      o = 13;      
    } else if (currentVolts <= 1500) {
      o = 12;
    } else if (currentVolts < 1800) {
      o = 10;    
    } else if (currentVolts < 2000) {
      o = 9;        
    } else if (currentVolts <= 2100) {
      o = 8;
    } else if (currentVolts < 2200) {
      o = 7;
    } else {
      o = 6;        
    }        
    setVoltage(o);
    updateScreen();  
  }  
}

void valueListProces(bool modeSwitch) {
  bool change = false;
  int32_t encoder = ss.getEncoderDelta();   
  /* if (encoder > 0) {
    if (testValueIndex >= (TESTVALUESSIZE-1)) {
      testValueIndex = 0;
    } else {
      testValueIndex++;
    }
    change = true;    
  }  */
  if (encoder > 0) {
    if (testValueIndex < (TESTVALUESSIZE-1)) {
      testValueIndex++;
      change = true;
    }
  } 
  if (encoder < 0) {
    if (testValueIndex > 0) {
      testValueIndex--;  
      change = true;          
    }    
  } 
  if (change || modeSwitch) {
    currentVolts = TestVoltages[testValueIndex].volts;
    setVoltage(TestVoltages[testValueIndex].offset);
    updateScreen();
  }  
} 

void valueListOffsetAdjuctProcess() {
  bool change = false;  
  int32_t encoder = ss.getEncoderDelta();
  if (encoder > 0) {
    TestVoltages[testValueIndex].offset++;   
    change = true;   
  }
  if (encoder < 0) {
    TestVoltages[testValueIndex].offset--;   
    change = true;     
  }
  if (change) {
    setVoltage(TestVoltages[testValueIndex].offset);
    updateScreen();
  }
}

void setVoltage(uint16_t o) {
  if (currentVolts == 0) {
    dacD = 0; 
    offset = 0;   
  } else if (currentVolts == HIGH_VOLTS) {
    dacD = FULL_SCALE - 2;
    offset = 0;      
  } else {
    dacD = (uint16_t) round(currentVolts/dacVoltsPerBit);
    offset = o;    
  }
  ad5693.writeUpdateDAC(dacD + offset);                
  updateScreen();
  voltsChanged = false;
}

void loop() {
  if(!digitalRead(0)) {
    currentMode = Normal;
  } else if (digitalRead(1)) {
    currentMode = ValueList; 
  } else if (digitalRead(2) && currentMode == ValueList) {
    currentMode = ValueListOffsetAdjuct;    
  } 
  bool modeChange = currentMode != previousMode;
  switch(currentMode) {
    case Normal:
      if (modeChange) {
        sspixel.setPixelColor(0, 0, 255, 0);
        sspixel.show();            
      }    
      normalModeProcess(modeChange);
      break;
    case ValueList:
      if (modeChange) {
        sspixel.setPixelColor(0, 0, 0, 255);
        sspixel.show();            
      }

      valueListProces(modeChange);
      break;
    case ValueListOffsetAdjuct: 
      if (modeChange) {
        sspixel.setPixelColor(0, 255, 0, 0);
        sspixel.show();           
      }
      valueListOffsetAdjuctProcess();
  }  
  previousMode = currentMode;
    delay(200);
}
