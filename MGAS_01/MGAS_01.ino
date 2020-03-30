#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <LowPower.h> //get library from: https://github.com/lowpowerlab/lowpower
//writeup here: http://www.rocketscream.com/blog/2011/07/04/lightweight-low-power-arduino-library/
#include <EEPROM.h>

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
        radio.enableAutoPower(-70);
    #endif
    pinMode(MOTION_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    attachInterrupt(MOTION_IRQ, motionIRQ, FALLING);
    char buff[50];
    pinMode(ONBOARDLED, OUTPUT);
    pinMode(LED, OUTPUT);
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
void loop()
{

    checkButton();
    while(ntermino)
    {
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
            Blink(LED,3);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                Serial.println(EEPROM.read(0));
                radio.setAddress(textoFull.toInt());
                radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                motionDetected=true;
                ntermino=false;
                motionDetected=true;
            }
            Serial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;
            if (radio.sendWithRetry(GATEWAYID, "(XXX)iD:GAS LOGIN", 17))
                Serial.print(" ok!");
            else Serial.print(" nothing...");
            Serial.println();
            Blink(LED,3);
        }
    }
    //check for any received packets
    for(int i=0;i<2;i++)
    {
        if (radio.receiveDone())
        {
            Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            for (byte i = 0; i < radio.DATALEN; i++)
                Serial.print((char)radio.DATA[i]);
            Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            if (radio.ACKRequested())
            {
                radio.sendACK();
                Serial.print(" - ACK sent");
            }
            Blink(LED,3);
            Serial.println();
        }
        if (motionDetected)
        {
            while(digitalRead(MOTION_PIN)==LOW)
            {
                digitalWrite(LED, HIGH);
                delay(500);
                sprintf(sendBuf, "(%d)iD:GAS G:1 A:%i V:%s",EEPROM.read(1), checkGas(), BATstr);
                DEBUG(sendBuf);
                sendLen = strlen(sendBuf);
                if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
                {
                    DEBUG("GAS ACK:OK! RSSI:");
                    DEBUG(radio.RSSI);
                    batteryReportCycles = 0;
                }
                else DEBUG("GAS ACK:NOK...");
                radio.sleep();
                digitalWrite(LED, LOW);
            }
            sprintf(sendBuf, "(%d)iD:GAS G:0 V:%s",EEPROM.read(1), BATstr);
            DEBUG(sendBuf);
            sendLen = strlen(sendBuf);
            if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
            {
                DEBUG("NOGAS ACK:OK! RSSI:");
                DEBUG(radio.RSSI);
                batteryReportCycles = 0;
            }
            else DEBUG("NOGAS ACK:NOK...");
            radio.sleep();
        }
        motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    }
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    batteryReportCycles++;
}
