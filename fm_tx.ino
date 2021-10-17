#include <Wire.h>
#include <Adafruit_Si4713.h>
#include "rgb_lcd.h"
#include <EEPROM.h>

#define RESETPIN A3
#define LEDPIN   13

#define ENCAPIN  3
#define ENCBPIN  4
#define ENTERPIN 5
#define GREENPIN 6
#define BLUEPIN  7
#define REDPIN   8

#define EEPROMVALID 0x55aa

#define RELSTART 102

#define FSTART   10230      // 10230 == 102.30 MHz
#define FLOW     7600
#define FHIGH    10800
#define FSTEP    5

#define DEVSTART 6600       // 66kHz audio + RDS + Pilot = 75kHz
#define DEVLOW   0
#define DEVHIGH  8000
#define DEVSTEP  10

void setFreq();
void setProcessor();
void setRDS();
void setDeviation();
void setPreEmph();
void setDefaults();

void setLimiterReleaseTime();

void showMain();

Adafruit_Si4713 tx;
rgb_lcd lcd;

typedef struct
{
    unsigned int txFreq;
    unsigned int preEmph;
    unsigned int audioDeviation;
    unsigned int relTime;
    unsigned int dataValid;
} EEPROMDATA;

typedef struct
{
    char *name;
    void (*fn)(void);
} MENUITEM;

typedef struct
{
    char *name;
    unsigned int val;
} MENUOPTION;

MENUITEM topMenu[] =
{
    { "Frequency", setFreq },
    { "Processor", setProcessor },
    { "Pre-emph ", setPreEmph },
    { "RDS      ", setRDS },
    { "Deviation", setDeviation },
    { "Default  ", setDefaults },
    { "Exit     ", NULL }
};

MENUITEM procMenu[] =
{
    { "RelTime  ", setLimiterReleaseTime },
    { "Exit     ", NULL }
};

MENUOPTION relTimes[] =
{
    { " 100", 5 },
    { "  75", 7 },
    { "  30", 17 },
    { "  20", 25 },
    { "  10", 51 },
    { "   5", 102 },
    { "   2", 255 },
    { "   1", 510 },
    { " 0.5", 1000 },
    { "0.25", 2000 },
    { NULL, 0 }
};

MENUOPTION preEmph[] =
{
    { "50us", 1 },
    { "75us", 0 },
    { "none", 2 },
    { NULL, 0 }
};

EEPROMDATA eeprom;

volatile boolean encUp;
volatile boolean encDown;

void encoderInterrupt()
{
    if(digitalRead(ENCAPIN) == digitalRead(ENCBPIN))
    {
        encUp = true;
    }
    else
    {
        encDown = true;
    }
}

void encoderSetup(int aPin, int bPin)
{
    encUp = false;
    encDown = false;
    pinMode(aPin, INPUT);
    pinMode(bPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(aPin), encoderInterrupt, FALLING);
}

unsigned char calcChecksum(unsigned char *dPtr, unsigned int dataSize)
{
    unsigned char checksum;

    checksum = 0;
    while(dataSize)
    {
        checksum = checksum - *dPtr;
        dPtr++;
        dataSize--;
    }
}

void defaultEeprom()
{
    eeprom.txFreq = FSTART;
    eeprom.preEmph = 1;
    eeprom.audioDeviation = DEVSTART;
    eeprom.relTime = RELSTART;
    eeprom.dataValid = EEPROMVALID;
}

void readEeprom(EEPROMDATA *ePtr)
{
    EEPROM.get(0, ePtr);
    if(ePtr -> dataValid != EEPROMVALID)
    {
        Serial.println("EEPROM loaded with defaults");
        defaultEeprom();
    }
    else
    {
        Serial.println("Got configuration from EEPROM");
    }
}

void writeEeprom()
{
    unsigned char eepromCs;
    unsigned char configCs;
    EEPROMDATA eData;

    readEeprom(&eData);
    eepromCs = calcChecksum((unsigned char *)&eData, sizeof(EEPROMDATA));

    configCs = calcChecksum((unsigned char *)&eeprom, sizeof(EEPROMDATA));

    if(configCs != eepromCs)
    {
        EEPROM.put(0, eeprom);
    }
}

void blankLine(int line)
{
    int c;
    
    lcd.setCursor(0, line);
    for(c = 0; c < 16; c++)
    {
        lcd.print(" ");
    }

    lcd.setCursor(0, line);
}

void fToString(int f, char *fStr)
{
    sprintf(fStr, "%03d.%02d MHz", f / 100, f % 100);
}

void homeScreen()
{
    char fStr[16];
    
    tx.readTuneStatus();
    fToString(tx.currFreq, fStr);

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(fStr);
}

void txSetup()
{
    char fStr[16];
    unsigned int propVal;

    tx.setTXpower(115);  // dBuV, 88-115 max

    fToString(eeprom.txFreq, fStr);    
    Serial.print("Frequency ");
    Serial.println(fStr);
    tx.tuneFM(eeprom.txFreq); // 102.3 mhz

    // This will tell you the status in case you want to read it from the chip
    //tx.readTuneStatus();
    //Serial.print("\tCurr freq: "); 
    //Serial.println(tx.currFreq);
    //Serial.print("\tCurr ANTcap:"); 
    //Serial.println(tx.currAntCap);

    // begin the RDS/RDBS transmission
    tx.beginRDS();
    tx.setRDSstation("ArseFM");
    tx.setRDSbuffer( "Isle of Man");

    // 50us pre-emphasis
    Serial.print("Pre-emphasis ");
    sprintf(fStr, "%d", eeprom.preEmph);
    Serial.println(fStr);
    tx.setProperty(SI4713_PROP_TX_PREEMPHASIS, eeprom.preEmph);

    // audio deviation
    Serial.print("Audio deviation - ");
    sprintf(fStr, "%d", eeprom.audioDeviation);
    Serial.println(fStr);
    tx.setProperty(SI4713_PROP_TX_AUDIO_DEVIATION, eeprom.audioDeviation);

    // RDS deviation
    Serial.print("RDS deviation - ");
    tx.setProperty(SI4713_PROP_TX_RDS_DEVIATION, 200);
    propVal = tx.getProperty(SI4713_PROP_TX_RDS_DEVIATION);
    sprintf(fStr, "%d", propVal);
    Serial.println(fStr);

    // Pilot deviation
    Serial.println("7kHz pilot deviation");
    tx.setProperty(SI4713_PROP_TX_PILOT_DEVIATION, 700);
    propVal = tx.getProperty(SI4713_PROP_TX_PILOT_DEVIATION);
    sprintf(fStr, "%d", propVal);
    Serial.println(fStr);

    // Enable limimter
    tx.setProperty(SI4713_PROP_TX_LIMITER_RELEASE_TIME, eeprom.relTime);
    propVal = tx.getProperty(SI4713_PROP_TX_ACOMP_ENABLE) | 0x0002;
    tx.setProperty(SI4713_PROP_TX_ACOMP_ENABLE, propVal);
}

void setup()
{
    Serial.begin(9600);
    //Serial.println("Si4713 Transmitter");

    lcd.begin(16, 2);

    tx = Adafruit_Si4713(RESETPIN);
    if(!tx.begin())
    {
        Serial.println("Couldn't find radio?");
        lcd.print("Can't find radio");
        while(1);
    }

    Serial.print("Found Si47");
    Serial.println(tx.getRev());

    readEeprom(&eeprom);
    txSetup();
 
    pinMode(ENTERPIN, INPUT);

    encoderSetup(ENCAPIN, ENCBPIN);

    pinMode(LEDPIN, OUTPUT);
    pinMode(REDPIN, OUTPUT);
    pinMode(GREENPIN, OUTPUT);
    pinMode(BLUEPIN, OUTPUT);
    digitalWrite(LEDPIN, LOW);
    digitalWrite(REDPIN, HIGH);
    digitalWrite(GREENPIN, HIGH);
    digitalWrite(BLUEPIN, HIGH);

    homeScreen();
}

boolean encoderDown()
{
    if(encDown == true)
    {
        encDown = false;
        return true;
    }
    else
    {
        return false;
    }
}

boolean encoderUp()
{
    if(encUp == true)
    {
        encUp = false;
        return true;  
    }
    else
    {
        return false;
    }
}

boolean enterPressed()
{
    if(digitalRead(ENTERPIN) == HIGH)
    {
        while(digitalRead(ENTERPIN) == HIGH)
        {
            delay(10);
        }
        
        return true;
    }
    else
    {
        return false;
    }
}

void doASQ()
{
    unsigned char lvl;
    char meter[17];
    int c;
    
    tx.readASQ();

    // overmod
    if((tx.currASQ & 0x01) == 0x01)
    {
        digitalWrite(REDPIN, HIGH);
        digitalWrite(GREENPIN, LOW);
    }
    else
    {
        digitalWrite(REDPIN, LOW);
        digitalWrite(GREENPIN, HIGH);
    }
 
    // value returned is in 2's complement
    lvl = ~tx.currInLevel + 1;

    lvl = map(lvl, 0, 64, 16, 0);

    c = 0;
    while(lvl)
    {
        meter[c] = '>';
        lvl--;
        c++;
    }
    while(c < 16)
    {
        meter[c] = ' ';
        c++;
    }
    meter[17] = '\0';

    lcd.setCursor(0, 1);
    lcd.print(meter);
}

void setDefaults()
{
    defaultEeprom();
    txSetup();
}

unsigned setProperty(MENUOPTION *m, unsigned int property, char *prompt, char *unit)
{
    int opt;
    boolean done;
    unsigned int initProp;
    boolean propChanged;

    blankLine(0);
    
    initProp = tx.getProperty(property);

    opt = 0;
    while(m[opt].name != NULL && initProp != m[opt].val)
    {
        opt++; 
    }
    
    if(m[opt].name == NULL)
    {
        opt = 0;
    }

    propChanged = false;
    
    done = false;
    do
    {
         lcd.setCursor(0, 0);
         lcd.print(prompt);
         lcd.print(m[opt].name);
         lcd.print(unit);

         if(encoderUp() == true)
         {
             propChanged = true;
             opt++;
             if(m[opt].name == NULL)
             {
                opt--;
             }
         }
         else
         {
            if(encoderDown() == true)
            {
                propChanged = true;
                if(opt != 0)
                {
                    opt--;
                }
            }
            else
            {
                if(enterPressed() == true)
                {
                    done = true;
                }
            }

            if(propChanged == true)
            {
                propChanged = false;
                tx.setProperty(property, m[opt].val);
            }
         }
    }
    while(done == false);

    return m[opt].val;
}

void doMenu(MENUITEM *menu)
{
    int c;
    boolean done;

    lcd.clear();

    done = false;
    c = 0;
    do
    {
        lcd.setCursor(0, 0);
        lcd.print(">");
        lcd.print(menu[c].name);
        lcd.print("<");
        if(encoderUp() == true)
        {
            if(menu[c].fn == NULL)
            {
                c = 0;
            }
            else
            {
                c++;
            }
        }
        else
        {
            if(encoderDown() == true)
            {
                if(c == 0)
                {
                    while(menu[c].fn != NULL)
                    {
                        c++;
                    }
                }
                else
                {
                    c--;
                }
            }
            else
            {
                if(enterPressed() == true)
                {
                    if(menu[c].fn == NULL)
                    {
                        done = true;
                    }
                    else
                    {
                        menu[c].fn();
                    }
                }
            }
        }
    }
    while(done == false);
}

void setFreq()
{
    int f;
    boolean done;
    char fStr[16];

    tx.readTuneStatus();
    f = tx.currFreq;

    lcd.clear();

    done = false;
    do
    {
        fToString(f, fStr);
      
        lcd.setCursor(0, 0);
        lcd.print(fStr);

        if(encoderUp() == true)
        {
            if(f < FHIGH)
            {
                f = f + FSTEP;
            }
            else
            {
                f = FLOW;
            }
        }
        else
        {
            if(encoderDown() == true)
            {
                if(f > FLOW)
                {
                    f = f - FSTEP;
                }
                else
                {
                    f = FHIGH;
                }
            }
            else
            {
                if(enterPressed() == true)
                {
                    done = true;
                }
            }
        }
    }
    while(done == false);

    eeprom.txFreq = f;

    tx.tuneFM(f);
}

void setDeviation()
{
    int dev;
    boolean done;
    boolean devChanged;
    char devStr[16];

    dev = tx.getProperty(SI4713_PROP_TX_AUDIO_DEVIATION); 

    lcd.clear();

    done = false;
    devChanged = false;
    do
    {
        lcd.setCursor(0, 0);
        sprintf(devStr, "%02d.%02d kHz", dev / 100, dev % 100);
        lcd.print(devStr);

        devChanged = false;

        if(encoderUp() == true)
        {
            if(dev < DEVHIGH)
            {
                dev = dev + DEVSTEP;
                devChanged = true;
            }
        }
        else
        {
            if(encoderDown() == true)
            {
                if(dev > DEVLOW)
                {
                    dev = dev - DEVSTEP;
                    devChanged = true;
                }
            }
            else
            {
                if(enterPressed() == true)
                {
                    done = true;
                }
            }

            if(devChanged == true)
            {
                devChanged = false;
                eeprom.audioDeviation = dev;
                tx.setProperty(SI4713_PROP_TX_AUDIO_DEVIATION, dev);
            }
        }
    }
    while(done == false);
}

void setLimiterReleaseTime()
{
    eeprom.relTime = setProperty(relTimes, SI4713_PROP_TX_LIMITER_RELEASE_TIME, "", "ms");
}

void setPreEmph()
{
    eeprom.preEmph = setProperty(preEmph, SI4713_PROP_TX_PREEMPHASIS, "", "");
}

void setProcessor()
{
    doMenu(procMenu); 
}

void setRDS()
{
}

void loop()
{
    boolean encChanged;
    boolean entNow;

    if(enterPressed() == true)
    {
        doMenu(topMenu);
        writeEeprom();
        homeScreen();
    }
    else
    {
        doASQ();
    }   
}
