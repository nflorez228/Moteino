#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <SPIFlash.h> //get it here: https://www.github.com/lowpowerlab/spiflash
#include <LowPower.h> //get library from: https://github.com/lowpowerlab/lowpower
//writeup here: http://www.rocketscream.com/blog/2011/07/04/lightweight-low-power-arduino-library/
#include <EEPROM.h>
#include <SoftwareSerial.h>

//SoftwareSerial mySerial(10, 11); // RX, TX

//*********************************************
//************ IMPORTANT SETTINGS *************
//*********************************************
#define NODEID        253    //unique for each node on same network
#define NETWORKID     253  //the same on all nodes that talk to each other
#define GATEWAYID     1
#define FREQUENCY     RF69_915MHZ
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
//*********************************************************************************************
#define LED            2  // MotionOLEDMote has an external LED on D5
#define FLASH_SS      8 // and FLASH SS on D8
#define MOTION_PIN     3  // D3
#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
#define BUTTON_PIN     4
//#define SERIAL_EN             //comment this out when deploying to an installed SM to save a few KB of sketch size
#define SERIAL_BAUD    38400
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
char sendBuf[32];
byte sendLen;
void motionIRQ(void);
void checkBattery(void);
bool ntermino = true;
long lastPeriod = 0;
uint16_t batteryReportCycles=0;
int bCycleCount=0;
//int cycleCount=BATT_CYCLES;

void setup()
{
    //Serial.begin(SERIAL_BAUD);
    Serial.begin(38400);
    if(EEPROM.read(0)==255 || EEPROM.read(1)==255)
    {
    }
    else
    {
        ntermino=false;
    }
    pinMode(MOTION_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    attachInterrupt(MOTION_IRQ, motionIRQ, CHANGE);
    char buff[50];
    pinMode(LED, OUTPUT);
//    pinMode(BATEN_PIN, OUTPUT);
    //radio.sendWithRetry(GATEWAYID, "START", 5);
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
void checkButton()
{
    if(digitalRead(BUTTON_PIN)==LOW)
    {
        if(bCycleCount == 0)
        {
            digitalWrite(LED, HIGH);
            DEBUGln("Boton presionado");
            EEPROM.write(0,255);
            /*radio.setAddress(NODEID);
            radio.setNetwork(NETWORKID);*/
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
        DEBUG(inputString.substring(0,6));
        if(inputString.substring(0,6)=="ACK:OK")
        {
          DEBUG(" ok!");
            noack = false;
        }
        else
        {
          DEBUG(" nothing...");
          delay(100);
        }
        DEBUGln();
    }
    return true;
}
void loop()
{
    //checkBattery();
    checkButton();
    while(ntermino)
    {
        //check for any received packets
        if (Serial.available())
        {
            //DEBUG('[');DEBUG(radio.SENDERID, DEC);DEBUG("] ");
            String textoFull;
            while (Serial.available())
            {
                textoFull+=(char)Serial.read();
            }
            DEBUGln(textoFull.toInt());
            //DEBUG("   [RX_RSSI:");DEBUG(radio.RSSI);DEBUG("]");
            /*if (radio.ACKRequested())
            {
                radio.sendACK();
                DEBUG(" - ACK sent");
            }*/
            Blink(LED,3);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                DEBUGln(EEPROM.read(0));
                //radio.setAddress(textoFull.toInt());
                //radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                motionDetected=true;
                ntermino=false;
            }
            DEBUGln();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod && ntermino)
        {
            lastPeriod=currPeriod;

            if (sendWithRtry("1:254-iD:MOV LOGIN"))
            {
                DEBUG(" OOK!");
            }
            else{ DEBUG(" nothing...");}
            DEBUGln();
            delay(1200);
            Blink(LED,3);
        }
    }
    if (motionDetected)
    {
        digitalWrite(LED, HIGH);
        if(digitalRead(MOTION_PIN)==LOW)
        {
            sprintf(sendBuf, "iD:MOV M:0");
        }
        else
        {
            sprintf(sendBuf, "iD:MOV M:1");
        }
        DEBUG(sendBuf);
        sendLen = strlen(sendBuf);
        Serial.println(sendBuf);
        /*if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
        {
            DEBUG("MOV ACK:OK! RSSI:");
            DEBUG(radio.RSSI);
            batteryReportCycles = 0;
        }
        else DEBUG("MOV ACK:NOK...");
        radio.sleep();*/
        digitalWrite(LED, LOW);
    }
    motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    //LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    batteryReportCycles++;
}
