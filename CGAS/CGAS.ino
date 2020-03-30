#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <LowPower.h> //get library from: https://github.com/lowpowerlab/lowpower
//writeup here: http://www.rocketscream.com/blog/2011/07/04/lightweight-low-power-arduino-library/
#include <EEPROM.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(10, 11); // RX, TX


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
#define ACK_TIME      30  // max # of ms to wait for an ack
#define ONBOARDLED     9  // Moteinos have LEDs on D9
#define LED            5  // MotionOLEDMote has an external LED on D5
#define MOTION_PIN     3  // D3
#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
#define BUTTON_PIN     4
#define GAS_MONITOR   A2
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_CYCLES  450 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define BATT_FORMULA(reading) reading * 0.00322 * 1.51 // >>> fine tune this parameter to match your voltage when fully charged
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

volatile boolean motionDetected=false;
float batteryVolts = 5;
char BATstr[20]; //longest battery voltage reading message = 9chars
char sendBuf[32];
byte sendLen;
void motionIRQ(void);
void checkBattery(void);
bool ntermino = true;
long lastPeriod = 0;
int bCycleCount=0;
int cycleCount=BATT_CYCLES;
uint16_t batteryReportCycles=0;

void setup()
{
    Serial.begin(SERIAL_BAUD);
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
    #ifdef IS_RFM69HW
        radio.setHighPower(); //uncomment only for RFM69HW!
    #endif
    //radio.encrypt(ENCRYPTKEY);
    //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
    //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
    //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
    //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
    /*#ifdef ENABLE_ATC
        radio.enableAutoPower(-90);
    #endif*/
    pinMode(MOTION_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    attachInterrupt(MOTION_IRQ, motionIRQ, FALLING);
    char buff[50];
    pinMode(ONBOARDLED, OUTPUT);
    pinMode(LED, OUTPUT);
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
void checkBattery()
{
    if (cycleCount++ == BATT_CYCLES) //only read battery every BATT_CYCLES sleep cycles
    {
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
    }
}
int checkGas()
{
    unsigned int readings=0;
    for (byte i=0; i<10; i++) //take 10 samples, and average
        readings+=analogRead(GAS_MONITOR);
    return readings / 10.0;
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
                motionDetected=true;
                ntermino=false;
            }
            Serial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;

            if (sendWithRtry("1:254-iD:GAS LOGIN"))
                Serial.print(" OOK!");
            else Serial.print(" nothing...");
            Serial.println();
            delay(200);
            Blink(LED,3);
        }
    }
    if (motionDetected)
    {
        while(digitalRead(MOTION_PIN)==LOW)
        {
            digitalWrite(LED, HIGH);
            delay(500);
            sprintf(sendBuf, "iD:GAS G:1 A:%i V:%s", checkGas(), BATstr);
            DEBUG(sendBuf);
            sendLen = strlen(sendBuf);
            mySerial.println(sendBuf);
            /*if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
            {
                DEBUG("GAS ACK:OK! RSSI:");
                DEBUG(radio.RSSI);
                batteryReportCycles = 0;
            }
            else DEBUG("GAS ACK:NOK...");
            radio.sleep();*/
            digitalWrite(LED, LOW);
        }
        sprintf(sendBuf, "iD:GAS G:0 V:%s", BATstr);
        DEBUG(sendBuf);
        sendLen = strlen(sendBuf);
        mySerial.println(sendBuf);
        /*if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
        {
            DEBUG("NOGAS ACK:OK! RSSI:");
            DEBUG(radio.RSSI);
            batteryReportCycles = 0;
        }
        else DEBUG("NOGAS ACK:NOK...");*/
        batteryReportCycles = 0;
    }
    motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    batteryReportCycles++;
}
