#include <SPI.h>
#include <SPIFlash.h> //get it here: https://www.github.com/lowpowerlab/spiflash
#include <LowPower.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(10, 11); // RX, TX

//**********************************************
//************ IMPORTANT SETTINGS  *************
//**********************************************
#define NODEID        253  //must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define NETWORKID     253  //the same on all nodes that talk to each other (range up to 255)
#define GATEWAYID     1
#define FREQUENCY     RF69_915MHZ
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
//**********************************************
#define LED           9 // Moteinos have LEDs on D9
#define RELAY_PIN     5
#define RELAY2_PIN     7
#define FLASH_SS      8 // and FLASH SS on D8
#define MOTION2_PIN    6  // D3
#define MOTION_PIN     3  // D3
#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
#define BUTTON_PIN     4
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_CYCLES  450 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define BATT_FORMULA(reading) reading * 0.0039 * 1.51 // >>> fine tune this parameter to match your voltage when fully charged
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
char payload[] = "123 ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char buff[20];
byte sendSize=0;
boolean requestACK = false;
SPIFlash flash(FLASH_SS, 0xEF30); //EF30 for 4mbit  Windbond chip (W25X40CL)

volatile boolean motionDetected=false;
float batteryVolts = 5;
char BATstr[20]; //longest battery voltage reading message = 9chars
char sendBuf[32];
byte sendLen;
void motionIRQ(void);
void checkBattery(void);
bool ntermino = true;
int bCycleCount=0;
int cycleCount=BATT_CYCLES;
long lastPeriod = 0;
uint16_t batteryReportCycles=0;

bool relay1State=false;
bool relay2State=false;


String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

void setup()
{
    Serial.begin(SERIAL_BAUD);
    //Serial.println(EEPROM.read(0));
    mySerial.begin(38400);
    if(EEPROM.read(0)==255 || EEPROM.read(1)==255)
    {
        //radio.initialize(FREQUENCY,NODEID,NETWORKID);
    }
    else
    {
        ntermino=false;
        //radio.initialize(FREQUENCY,EEPROM.read(0),EEPROM.read(1));
    }
    pinMode(MOTION_PIN, INPUT);
    digitalWrite(MOTION_PIN,HIGH);
    pinMode(MOTION2_PIN, INPUT);
    digitalWrite(MOTION2_PIN,HIGH);
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    attachInterrupt(MOTION_IRQ, motionIRQ, CHANGE);
    pinMode(LED, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);
    char buff[50];
}
void motionIRQ()
{
    motionDetected=true;
    DEBUGln("IRQ");
}
void Blink(byte PIN, int DELAY_MS)
{
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN,HIGH);
    delay(DELAY_MS);
    digitalWrite(PIN,LOW);
}
void checkBattery()
{
    if (cycleCount++ == BATT_CYCLES) //only read battery every BATT_CYCLES sleep cycles
    {
        unsigned int readings=0;
        for (byte i=0; i<10; i++) //take 10 samples, and average
            readings+=analogRead(BATT_MONITOR);
        batteryVolts = BATT_FORMULA(readings / 10.0);
        dtostrf(batteryVolts, 3,2, BATstr); //update the BATStr which gets sent every BATT_CYCLES or along with the MOTION message
        cycleCount = 0;
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
            //radio.setAddress(NODEID);
            //radio.setNetwork(NETWORKID);
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
bool sendWithRtry(String toSend)
{
    boolean noack = true;
    while(noack)
    {
        String inputString="";
        bool stringComplete=false;
        mySerial.println(toSend);
        delay(100);
        while (mySerial.available() && !stringComplete)
        {
              // get the new byte:
              char inChar = (char)mySerial.read();
              if (inChar == '\n')
              {
                  stringComplete = true;
              }
              else
              {
                  inputString += inChar;
              }
        }
        Serial.print(inputString.substring(0,6));
        if(inputString.substring(0,6)=="ACK:OK")
        {
          Serial.print(" ok!");
            noack = false;
        }
        else
        {
          Serial.print(" nothing...");
          delay(1500);
        }
        Serial.println();
    }
    return true;
}
void loop()
{
    checkBattery();
    checkButton();

    while(ntermino)
    {
        //check for any received packets
        if (mySerial.available())
        {
            //Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            String textoFull;
            while (mySerial.available())
            {
                textoFull+=(char)mySerial.read();
            }
            Serial.println(textoFull.toInt());
            //Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            /*if (radio.ACKRequested())
            {
                radio.sendACK();
                Serial.print(" - ACK sent");
            }*/
            Blink(LED,3);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                Serial.println(EEPROM.read(0));
                //radio.setAddress(textoFull.toInt());
                //radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                ntermino=false;
            }
            Serial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;

            if (sendWithRtry("1:254-iD:OUD LOGIN"))
                Serial.print(" OOK!");
            else Serial.print(" nothing...");
            Serial.println();
            delay(200);
            Blink(LED,3);
        }
    }
    String inputstr = "";
    if (mySerial.available())
    {
        Serial.print('[');Serial.print("1");Serial.print("] ");
        char inChar = (char)mySerial.read();
        while (mySerial.available() && !stringComplete)
        {
              // get the new byte:
              inChar = (char)mySerial.read();
              if (inChar == '\n')
              {
                  stringComplete = true;
              }
              else
              {
                  Serial.print(inChar);
                  inputstr += inChar;
              }
        }
        Serial.print("   [RX_RSSI:");Serial.print("CB");Serial.print("]");
        /*if (radio.ACKRequested())
        {
            radio.sendACK();
            Serial.print(" - ACK sent");
        }*/
        Blink(LED,3);
        Serial.println();
    }
    if (motionDetected)
    {
        digitalWrite(LED, HIGH);
        if(digitalRead(MOTION_PIN)==HIGH)
        {
            relay1State=false;
            Serial.println("OFFM");
            mySerial.println("iD:OUD O1:OFF");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O1:OFF", 13);
            digitalWrite(RELAY_PIN, LOW);
        }
        else if(digitalRead(MOTION_PIN)==LOW)
        {
            relay1State=true;
            Serial.println("ONM");
            mySerial.println("iD:OUD O1:ON");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O1:ON", 12);
            digitalWrite(RELAY_PIN, HIGH);
        }
        digitalWrite(LED, LOW);
    }
    if (digitalRead(MOTION2_PIN) != relay2State)
    {
        digitalWrite(LED, HIGH);
        if(digitalRead(MOTION2_PIN)==HIGH)
        {
            relay2State=false;
            Serial.println("OFFM2");
            mySerial.println("iD:OUD O2:OFF");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O2:OFF", 13);
            digitalWrite(RELAY2_PIN, LOW);
        }
        else if(digitalRead(MOTION2_PIN)==LOW)
        {
            relay2State=true;
            Serial.println("ONM2");
            mySerial.println("iD:OUD O2:ON");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O2:ON", 12);
            digitalWrite(RELAY2_PIN, HIGH);
        }
        digitalWrite(LED, LOW);
    }
    if (inputstr != "")
    {
        Serial.println(inputstr);
        digitalWrite(LED, HIGH);
        if(inputstr == "OFF1")
        {
            Serial.println("OFF");
            mySerial.println("iD:OUD O1:OFF");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O1:OFF", 13);
            digitalWrite(RELAY_PIN, LOW);
        }
        else if(inputstr == "ON1")
        {
            Serial.println("ON");
            mySerial.println("iD:OUD O1:ON");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O1:ON", 12);
            digitalWrite(RELAY_PIN, HIGH);
        }
        else if(inputstr == "OFF2")
        {
            Serial.println("OFF2");
            mySerial.println("iD:OUD O2:OFF");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O2:OFF", 13);
            digitalWrite(RELAY2_PIN, LOW);
        }
        else if(inputstr == "ON2")
        {
            Serial.println("ON2");
            mySerial.println("iD:OUD O2:ON");
            //radio.sendWithRetry(GATEWAYID, "iD:OUD O2:ON", 12);
            digitalWrite(RELAY2_PIN, HIGH);
        }
        digitalWrite(LED, LOW);
    }
    motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    batteryReportCycles++;
}
