#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <LowPower.h> //get library from: https://github.com/lowpowerlab/lowpower
//writeup here: http://www.rocketscream.com/blog/2011/07/04/lightweight-low-power-arduino-library/
#include <EEPROM.h>
#include "DHT.h"

//*********************************************
//************ IMPORTANT SETTINGS *************
//*********************************************
#define NODEID        253    //unique for each node on same network
#define NETWORKID     253  //the same on all nodes that talk to each other
#define GATEWAYID     1
#define FREQUENCY     RF69_915MHZ
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
#define DHTTYPE DHT11   // DHT 11

//*********************************************************************************************
#define ACK_TIME      30  // max # of ms to wait for an ack
#define ONBOARDLED     9  // Moteinos have LEDs on D9
#define LED            5  // MotionOLEDMote has an external LED on D5
#define MOTION_PIN     3  // D3
#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
#define BUTTON_PIN     4
#define BATEN_PIN     6
#define SENSEN_PIN     7
DHT dht(MOTION_PIN, DHTTYPE);
#define CHARGING_MONITOR  A0
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_CYCLES  73 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define REPORT_CYCLES  73 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define BATT_FORMULA(reading) 0.006082183 * reading - 0.227388 // >>> fine tune this parameter to match your voltage when fully charged
#define BATT2_FORMULA(reading) 0.1788 * log10(reading) + 3.1861 // >>> fine tune this parameter to match your voltage when fully charged
                   // details on how this works: https://lowpowerlab.com/forum/index.php/topic,1206.0.html
#define SERIAL_EN             //comment this out when deploying to an installed SM to save a few KB of sketch size
#define SERIAL_BAUD    115200
#ifdef SERIAL_EN
    #define DEBUG(input)   {Serial.print(input); delay(1);}
    #define DEBUGln(input) {Serial.println(input); delay(1);}
#else
    #define DEBUG(input);
    #define DEBUGln(input);
#endif

int TRANSMITPERIOD = 1500; //transmit a packet to gateway so often (in ms)


#ifdef ENABLE_ATC
    RFM69_ATC radio;
#else
    RFM69 radio;
#endif

volatile boolean motionDetected=false;
float batteryVolts = 5;
char BATstr[20]; //longest battery voltage reading message = 9chars
char sendBuf[32];
byte sendLen;
void checkBattery(void);
bool ntermino = true;
int bCycleCount=0;
int cycleCount=BATT_CYCLES;
long lastPeriod = 0;
int batteryReportCycles=BATT_CYCLES;
int tempReportCycles=REPORT_CYCLES;
bool firsttimeB=true;
bool firsttimeS=true;
void setup()
{

    Serial.begin(SERIAL_BAUD);
    if(EEPROM.read(0)==255 || EEPROM.read(1)==255)
    {
        radio.initialize(FREQUENCY,NODEID,NETWORKID);
    }
    else
    {
        ntermino=false;
        radio.initialize(FREQUENCY,EEPROM.read(0),EEPROM.read(1));
    }
    #ifdef IS_RFM69HW
        radio.setHighPower(); //uncomment only for RFM69HW!
    #endif
    radio.encrypt(ENCRYPTKEY);
    //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
    //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
    //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
    //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
    #ifdef ENABLE_ATC
        radio.enableAutoPower(-90);
    #endif
    dht.begin();
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    char buff[50];
    pinMode(ONBOARDLED, OUTPUT);
    pinMode(LED, OUTPUT);
    pinMode(BATEN_PIN, OUTPUT);
    pinMode(SENSEN_PIN, OUTPUT);
    Blink(LED,300,3);
}
void Blink(byte PIN, int DELAY_MS, int times)
{
   pinMode(PIN, OUTPUT);
   for (byte i = 0; i < times; i++)
    {
        digitalWrite(PIN,HIGH);
        delay(DELAY_MS);
        digitalWrite(PIN,LOW);
        delay(DELAY_MS);
    }
}
void checkBattery()
{
    if (++cycleCount == BATT_CYCLES || firsttimeB) //only read battery every BATT_CYCLES sleep cycles
    {
      firsttimeB=false;
        digitalWrite(BATEN_PIN,HIGH);
        unsigned int readings=0;
        for (byte i=0; i<10; i++) //take 10 samples, and average
            readings+=analogRead(BATT_MONITOR);
        DEBUGln(readings/10.0);
        batteryVolts = BATT_FORMULA(readings / 10.0);
        DEBUGln(batteryVolts);
        batteryVolts = batteryVolts - 3.3;
        batteryVolts=batteryVolts*100/0.8;
        //batteryVolts = batteryVolts*100;
        DEBUGln(batteryVolts);
        if(batteryVolts>100)
        {

            batteryVolts=100;

        DEBUGln(batteryVolts);
        }
        dtostrf(batteryVolts, 3,2, BATstr); //update the BATStr which gets sent every BATT_CYCLES or along with the MOTION message
        cycleCount = 0;
        digitalWrite(BATEN_PIN,LOW);
    }
}
void checkButton()
{
    if(digitalRead(BUTTON_PIN)==LOW)
    {
        if(bCycleCount == 0)
        {
            digitalWrite(LED, HIGH);
            DEBUGln("Boton presionado");
            EEPROM.write(0,255);
            radio.setAddress(NODEID);
            radio.setNetwork(NETWORKID);
            ntermino=true;
            digitalWrite(LED, LOW);
            bCycleCount=2;
        }
        else
        {
            bCycleCount--;
        }
    }
    else
    {
        bCycleCount=0;
    }
}

void fade(byte PIN, int DELAY_MS, int times)
{
  for (byte i = 0; i < times; i++)
    {
      for (int fadeValue = 0 ; fadeValue <= 255; fadeValue += 5) {
        // sets the value (range from 0 to 255):
        analogWrite(PIN, fadeValue);
        // wait for 30 milliseconds to see the dimming effect
        delay(30);
      }

      // fade out from max to min in increments of 5 points:
      for (int fadeValue = 255 ; fadeValue >= 0; fadeValue -= 5) {
        // sets the value (range from 0 to 255):
        analogWrite(PIN, fadeValue);
        // wait for 30 milliseconds to see the dimming effect
        delay(30);
      }
      delay(DELAY_MS);
    }
}

void checkCharging()
{
  int val = analogRead(CHARGING_MONITOR);
  if(val<1000)
  {
    fade(LED,30,2);
  }
  else
  {
    Blink(LED,300,5);
  }
}

void loop()
{
    checkBattery();
    checkButton();
    checkCharging();
    while(ntermino)
    {
      //Blink(LED,100,1);
        //check for any received packets
        if (radio.receiveDone())
        {
            Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            char textoLlega[(int)radio.DATALEN];
            for (byte i = 0; i < radio.DATALEN; i++)
            {
                textoLlega[i]=(char)radio.DATA[i];
            }
            String textoFull(textoLlega);
            Serial.print(textoFull);
            Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            if (radio.ACKRequested())
            {
                radio.sendACK();
                Serial.print(" - ACK sent");
            }
            Blink(LED,3,1);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                Serial.println(EEPROM.read(0));
                radio.setAddress(textoFull.toInt());
                radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                motionDetected=true;
                ntermino=false;
            }
            Serial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;
            if (radio.sendWithRetry(GATEWAYID, "(XXX)iD:TEM LOGIN", 17))
                Serial.print(" ok!");
            else Serial.print(" nothing...");
            Serial.println();
            Blink(LED,3,1);
        }
    }

    if (tempReportCycles == REPORT_CYCLES || firsttimeS)
    {
      firsttimeS=false;
        digitalWrite(SENSEN_PIN,HIGH);
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        float f = dht.readTemperature(true);
        if (isnan(h) || isnan(t) || isnan(f))
        {
            DEBUGln("Failed to read from DHT sensor!");
            delay(100);
            //return;
        }
        else
        {
            char tosend[29];
            String rta;
            rta+="C:"+String(t,0);
            rta+="F:"+String(f,0);
            rta+="H:"+String(h,0);
            //DEBUGln(rta);
            rta.toCharArray(tosend,29);
            //digitalWrite(LED, HIGH);
            sprintf(sendBuf, "(%d)iD:TEM %s V:%s",EEPROM.read(1),tosend,BATstr);
            sendLen = strlen(sendBuf);
            DEBUG(sendBuf)
            if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
            {
                DEBUG("TEM ACK:OK! RSSI:");
                DEBUG(radio.RSSI);
                //batteryReportCycles = 0;
            }
            else DEBUG("TEM ACK:NOK...");
            DEBUG(" VIN: ");
            DEBUGln(BATstr);
            radio.sleep();
            //digitalWrite(LED, LOW);
        }
        tempReportCycles=0;
        digitalWrite(SENSEN_PIN,LOW);
    }
    motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    radio.sleep();
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    tempReportCycles++;
    batteryReportCycles++;
}
