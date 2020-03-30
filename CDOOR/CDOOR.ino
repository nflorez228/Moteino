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
#define FLASH_SS      8 // and FLASH SS on D8
#define MOTION_PIN     3  // D3
#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
#define BUTTON_PIN     4
#define BATEN_PIN     6
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_CYCLES  450 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define BATT_FORMULA(reading) reading * 0.0039 * 1.51 // >>> fine tune this parameter to match your voltage when fully charged
                                                       // details on how this works: https://lowpowerlab.com/forum/index.php/topic,1206.0.html

#define SERIAL_EN             //comment this out when deploying to an installed SM to save a few KB of sketch size
#define SERIAL_BAUD    115200
#ifdef SERIAL_EN
#define DEBUG(input)   {mySerial.print(input); delay(1);}
#define DEBUGln(input) {mySerial.println(input); delay(1);}
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
long lastPeriod = 0;
int cycleCount=BATT_CYCLES;
uint16_t batteryReportCycles=0;
int bCycleCount=0;

void setup()
{
    mySerial.begin(SERIAL_BAUD);
    Serial.begin(38400);
    //mySerial.println(EEPROM.read(0));
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
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    attachInterrupt(MOTION_IRQ, motionIRQ, CHANGE);
    pinMode(LED, OUTPUT);
    pinMode(BATEN_PIN, OUTPUT);
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
        digitalWrite(BATEN_PIN,HIGH);
        unsigned int readings=0;
        for (byte i=0; i<10; i++) //take 10 samples, and average
            readings+=analogRead(BATT_MONITOR);
        batteryVolts = BATT_FORMULA(readings / 10.0);
        batteryVolts = batteryVolts - 3.3;
        batteryVolts = batteryVolts*100;
        if(batteryVolts>100)
            batteryVolts=100;
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
        Serial.println(toSend);
        delay(100);
        while (Serial.available() && !stringComplete)
        {
              // get the new byte:
              char inChar = (char)Serial.read();
              if (inChar == '\n')
              {
                  stringComplete = true;
              }
              else
              {
                  inputString += inChar;
              }
        }
        mySerial.print(inputString.substring(0,6));
        if(inputString.substring(0,6)=="ACK:OK")
        {
          mySerial.print(" ok!");
            noack = false;
        }
        else
        {
          mySerial.print(" nothing...");
          delay(1500);
        }
        mySerial.println();
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
        if (Serial.available())
        {
            //mySerial.print('[');mySerial.print(radio.SENDERID, DEC);mySerial.print("] ");
            String textoFull;
            while (Serial.available())
            {
                textoFull+=(char)Serial.read();
            }
            mySerial.println(textoFull.toInt());
            //mySerial.print("   [RX_RSSI:");mySerial.print(radio.RSSI);mySerial.print("]");
            /*if (radio.ACKRequested())
            {
                radio.sendACK();
                mySerial.print(" - ACK sent");
            }*/
            Blink(LED,3);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                mySerial.println(EEPROM.read(0));
                //radio.setAddress(textoFull.toInt());
                //radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                motionDetected=true;
                ntermino=false;
            }
            mySerial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;

            if (sendWithRtry("1:254-iD:PYV LOGIN"))
                mySerial.print(" OOK!");
            else mySerial.print(" nothing...");
            mySerial.println();
            delay(200);
            Blink(LED,3);
        }
    }

    if (motionDetected)
    {
        digitalWrite(LED, HIGH);
        if(digitalRead(MOTION_PIN)==LOW)
        {
            sprintf(sendBuf, "iD:PYV D:0 V:%s", BATstr);
        }
        else
        {
            sprintf(sendBuf, "iD:PYV D:1 V:%s", BATstr);
        }
        DEBUG(sendBuf);
        sendLen = strlen(sendBuf);
        Serial.println(sendBuf);
        /*if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
        {
            DEBUG("PYV ACK:OK! RSSI:");
            DEBUG(radio.RSSI);
            batteryReportCycles = 0;
        }
        else DEBUG("PYV ACK:NOK...");
        radio.sleep();*/
        digitalWrite(LED, LOW);
    }
    motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    batteryReportCycles++;
}
