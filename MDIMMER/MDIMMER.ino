#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>
#include <SPIFlash.h> //get it here: https://www.github.com/lowpowerlab/spiflash
#include <LowPower.h>
#include <EEPROM.h>

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
#define LED          5 // Moteinos have LEDs on D9
#define DIM1_PIN     3
#define DIM2_PIN     4
#define DIM3_PIN     6
#define FLASH_SS      8 // and FLASH SS on D8
#define BUTTON_PIN     7
#define DIM_MONITOR  A5
//#define SERIAL_EN             //comment this out when deploying to an installed SM to save a few KB of sketch size
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
bool ntermino = true;
long lastPeriod = 0;


int DimState=0;

void setup()
{
    //Serial.begin(SERIAL_BAUD);
    //Serial.println(EEPROM.read(0));
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
    //radio.setFrequency(919000000); //set frequency to some custom frequency
    //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
    //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
    //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
    //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
    #ifdef ENABLE_ATC
        radio.enableAutoPower(-70);
    #endif
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    pinMode(LED, OUTPUT);
    pinMode(DIM1_PIN, OUTPUT);
    pinMode(DIM2_PIN, OUTPUT);
    pinMode(DIM3_PIN,OUTPUT);
    char buff[50];
    Blinkr(LED,300,3);
}
void Blink(byte PIN, int DELAY_MS)
{
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN,HIGH);
    delay(DELAY_MS);
    digitalWrite(PIN,LOW);
}
void Blinkr(byte PIN, int DELAY_MS, int times)
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

int bCycleCount=0;
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

void checkDim()
{

      int val = analogRead(DIM_MONITOR);
      //128

      if(val<128)
      {
        setDim(0);
      }
      else if(val>=128 && val<256)
      {
        setDim(1);
      }
      else if(val>=256 && val<384)
      {
        setDim(2);
      }
      else if(val>=384 && val<512)
      {
        setDim(3);
      }
      else if(val>=512 && val<640)
      {
        setDim(4);
      }
      else if(val>=640 && val<768)
      {
        setDim(5);
      }
      else if(val>=768 && val<896)
      {
        setDim(6);
      }
      else
      {
         setDim(7);
      }


}
void setDim(int level)
{
  int prevDim=DimState;
switch (level) {
    case 0:
      DimState=0;
      digitalWrite(DIM1_PIN, LOW);
      digitalWrite(DIM2_PIN, LOW);
      digitalWrite(DIM3_PIN, LOW);
      break;
    case 1:
      DimState=1;
      digitalWrite(DIM1_PIN, LOW);
      digitalWrite(DIM2_PIN, LOW);
      digitalWrite(DIM3_PIN, HIGH);
      break;
    case 2:
      DimState=2;
      digitalWrite(DIM1_PIN, LOW);
      digitalWrite(DIM2_PIN, HIGH);
      digitalWrite(DIM3_PIN, LOW);
      break;
    case 3:
      DimState=3;
      digitalWrite(DIM1_PIN, LOW);
      digitalWrite(DIM2_PIN, HIGH);
      digitalWrite(DIM3_PIN, HIGH);
      break;
    case 4:
      DimState=4;
      digitalWrite(DIM1_PIN, HIGH);
      digitalWrite(DIM2_PIN, LOW);
      digitalWrite(DIM3_PIN, LOW);
      break;
    case 5:
      DimState=5;
      digitalWrite(DIM1_PIN, HIGH);
      digitalWrite(DIM2_PIN, LOW);
      digitalWrite(DIM3_PIN, HIGH);
      break;
    case 6:
      DimState=6;
      digitalWrite(DIM1_PIN, HIGH);
      digitalWrite(DIM2_PIN, HIGH);
      digitalWrite(DIM3_PIN, LOW);
      break;
    case 7:
      DimState=7;
      digitalWrite(DIM1_PIN, HIGH);
      digitalWrite(DIM2_PIN, HIGH);
      digitalWrite(DIM3_PIN, HIGH);
      break;
  }
  if(prevDim!=DimState)
  {
    motionDetected=true;
  }
}
void loop()
{
    checkButton();
    checkDim();

    while(ntermino)
    {
        //check for any received packets
        if (radio.receiveDone())
        {
            //Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            char textoLlega[(int)radio.DATALEN];
            for (byte i = 0; i < radio.DATALEN; i++)
            {
                textoLlega[i]=(char)radio.DATA[i];
            }
            String textoFull(textoLlega);
            //Serial.print(textoFull);
            //Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            if (radio.ACKRequested())
            {
                radio.sendACK();
                //Serial.print(" - ACK sent");
            }
            Blink(LED,3);
            if(textoFull.toInt()>1 && textoFull.toInt()<250 )
            {
                EEPROM.write(0,textoFull.toInt());
                EEPROM.write(1,textoFull.substring(textoFull.indexOf(",")+1).toInt());
                //Serial.println(EEPROM.read(0));
                radio.setAddress(textoFull.toInt());
                radio.setNetwork(textoFull.substring(textoFull.indexOf(",")+1).toInt());
                motionDetected=true;
                ntermino=false;
                sprintf(sendBuf, "(%d)iD:DIM D:0", EEPROM.read(1));
                sendLen = strlen(sendBuf);
                radio.sendWithRetry(GATEWAYID, sendBuf, sendLen);
            }
            //Serial.println();
        }
        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            Blinkr(LED,300,3);
            lastPeriod=currPeriod;
            if (radio.sendWithRetry(GATEWAYID, "(XXX)iD:DIM LOGIN", 17))
            {
                //Serial.print(" ok!");
            }
            else
            {
              //Serial.print(" nothing...");
            }
            //Serial.println();
            Blink(LED,3);
        }
    }
    //check for any received packets
    for(int i=0;i<1;i++)
    {
        String inputstr = "";
        if (radio.receiveDone())
        {
            //Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            for (byte i = 0; i < radio.DATALEN; i++)
            {
                //Serial.print((char)radio.DATA[i]);
                char inChar = (char)radio.DATA[i];
                inputstr += inChar;
            }
            //Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            if (radio.ACKRequested())
            {
                radio.sendACK();
                //Serial.print(" - ACK sent");
            }
            Blink(LED,3);
            //Serial.println();
        }

        if (motionDetected)
        {
            digitalWrite(LED, HIGH);
            //Serial.println("OFFM");
            sprintf(sendBuf, "(%d)iD:DIM D:(%d)", EEPROM.read(1),DimState);
            sendLen = strlen(sendBuf);
            radio.sendWithRetry(GATEWAYID, sendBuf, sendLen);
            //digitalWrite(RELAY_PIN, LOW);
            digitalWrite(LED, LOW);
        }
        if (inputstr != "")
        {
            //Serial.println(inputstr);
            digitalWrite(LED, HIGH);
            if(inputstr.toInt()>=0 && inputstr.toInt()<=7)
            {
                //Serial.println("OFF");
                sprintf(sendBuf, "(%d)iD:DIM D:(%d)", EEPROM.read(1),DimState);
                sendLen = strlen(sendBuf);
                radio.sendWithRetry(GATEWAYID, sendBuf, sendLen);
                setDim(inputstr.toInt());
                //digitalWrite(RELAY_PIN, LOW);
                //relayState=false;
            }
            digitalWrite(LED, LOW);
        }
        motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    }
}
