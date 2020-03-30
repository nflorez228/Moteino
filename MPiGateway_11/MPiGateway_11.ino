#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <SPIFlash.h> //get it here: https://www.github.com/lowpowerlab/spiflash
#include <EEPROM.h>
#include <math.h>

//*********************************************
//************ IMPORTANT SETTINGS *************
//*********************************************
#define NODEID        1    //unique for each node on same network
#define NETWORKID     253  //the same on all nodes that talk to each other
#define FREQUENCY     RF69_915MHZ
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define BUTTON_PIN     4
#define RESET_PIN      3
//#define IS_RFM69HW    //uncomment only for RFM69HW! Leave out if you have RFM69W!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
//*********************************************

#define SERIAL_BAUD   38400
#define LED           9 // Moteinos have LEDs on D9
#define FLASH_SS      8 // and FLASH SS on D8

#ifdef ENABLE_ATC
    RFM69_ATC radio;
#else
    RFM69 radio;
#endif

SPIFlash flash(FLASH_SS, 0xEF30); //EF30 for 4mbit  Windbond chip (W25X40CL)
bool promiscuousMode = false; //set to 'true' to sniff all packets on the same network
bool ntermino = true;

String inputstr = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
byte ackCount=0;
uint32_t packetCount = 0;
byte buff[61];
String toSendLogin="";
int bCycleCount=0;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(10);
    if(EEPROM.read(0)==255)
    {
        int netid = 0;
        while (netid==0)
        {
          netid=random(analogRead(A1));
          Serial.println(netid);
          if(netid>250)
          {
            netid=0;
          }
        }
        EEPROM.write(0,netid);
        radio.initialize(FREQUENCY,NODEID,netid);
        Serial.println(netid);
    }
    else
    {
        radio.initialize(FREQUENCY,NODEID,EEPROM.read(0));
        Serial.println(EEPROM.read(0));
    }
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    pinMode(RESET_PIN, INPUT);
    digitalWrite(RESET_PIN,HIGH);
    #ifdef IS_RFM69HW
        radio.setHighPower(); //only for RFM69HW!
    #endif
    radio.encrypt(ENCRYPTKEY);
    radio.promiscuous(promiscuousMode);
    #ifdef ENABLE_ATC
        //Serial.println("RFM69_ATC Enabled (Auto Transmission Control)");
    #endif
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
            Serial.println("INFO:SearchingNewNode");
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
void checkReset()
{
    if(digitalRead(RESET_PIN)==LOW)
    {
        if(bCycleCount == 0)
        {
            digitalWrite(LED, HIGH);
            Serial.println("INFO:RESET");
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
    checkReset();
    while (Serial.available())
    {
        // get the new byte:
        char inChar = (char)Serial.read();
        if (inChar == '\n')
        {
            stringComplete = true;
        }
        else
        {
            inputstr += inChar;
        }
    }
    if (stringComplete)
    {
        inputstr.toUpperCase();
        if (inputstr.equals("KEY?"))
        {
            Serial.print("ENCRYPTKEY:");
            Serial.print(ENCRYPTKEY);
        }
        byte targetId = inputstr.toInt(); //extract ID if any
        int targetIdInt = inputstr.toInt();
        byte colonIndex = inputstr.indexOf(":"); //find position of first colon
        if (targetId > 0) inputstr = inputstr.substring(colonIndex+1); //trim "ID:" if any
        if (targetId > 0 && targetId != NODEID && targetId != RF69_BROADCAST_ADDR && colonIndex>0 && colonIndex<4 && inputstr.length()>0)
        {
            if(inputstr.substring(0,5)=="LOGIN")
            {
                toSendLogin = toSendLogin + inputstr.substring(6) + "," + EEPROM.read(0);
                inputstr=toSendLogin;
                ntermino=false;
            }
            else
            {
                toSendLogin = inputstr;
            }
            toSendLogin.getBytes(buff, 61);
            Serial.println((char*)buff);
            if (radio.sendWithRetry(targetId, buff, toSendLogin.length()))
            {
                Serial.println("ACK:OK");
            }
            else
                Serial.println("ACK:NOK");

            if(!ntermino)
            {
                radio.setNetwork(EEPROM.read(0));
            }
        }
        if (targetId == NODEID && targetId != RF69_BROADCAST_ADDR && colonIndex>0 && colonIndex<4 && inputstr.length()>0)
        {
            if(inputstr.substring(0,7)=="RESETOK")
            {
                int netid = 0;
                while (netid==0)
                {
                  netid=random(analogRead(A1));
                  Serial.println(netid);
                  if(netid>250)
                  {
                    netid=0;
                  }
                }
                EEPROM.write(0,netid);
                radio.initialize(FREQUENCY,NODEID,netid);
                Serial.println(netid);
            }
        }
        toSendLogin="";
        inputstr = "";
        stringComplete = false;
    }

    if (radio.receiveDone())
    {
      String networkidllega="";
      boolean terminonetidllega=false;
      byte finalnumi=0;
      for (byte i = 1; i < 5; i++)
        {
          if((char)radio.DATA[0]=='(' && (char)radio.DATA[i]!=')' && !terminonetidllega)
          {
            networkidllega+=((char)radio.DATA[i]);
          }
          else if((char)radio.DATA[i]==')')
          {
            finalnumi=i+1;
            terminonetidllega=true;
          }
        }
      if(networkidllega.toInt()==EEPROM.read(0) || networkidllega=="XXX")
      {

        Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
        if (promiscuousMode)
        {
            Serial.print("to [");Serial.print(radio.TARGETID, DEC);Serial.print("] ");
        }
        for (byte i = finalnumi; i < radio.DATALEN; i++)
            Serial.print((char)radio.DATA[i]);
        Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
        if (radio.ACKRequested())
        {
            byte theNodeID = radio.SENDERID;
            radio.sendACK();
            Serial.print(" - ACK sent.");
        }
        Serial.println();
        Blink(LED,3);
      }
    }
}
