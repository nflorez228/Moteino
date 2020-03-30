/*
 * IRrecord: record and play back IR signals as a minimal 
 * An IR detector/demodulator must be connected to the input RECV_PIN.
 * An IR LED must be connected to the output PWM pin 3.
 * A button must be connected to the input BUTTON_PIN; this is the
 * send button.
 * A visible LED can be connected to LED to provide status.
 *
 * The logic is:
 * If the button is pressed, send the IR code.
 * If an IR code is received, record it.
 *
 * Version 0.11 September, 2009
 * Copyright 2009 Ken Shirriff
 * http://arcfn.com
 */

#include <IRremote.h>
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
#define LED           9 // Moteinos have LEDs on D9
#define FLASH_SS      8 // and FLASH SS on D8
#define RECV_PIN      6  // D3
#define BUTTON_PIN    4
#define BUTTOND_PIN   5
#define IRLED_PIN     3
#define BATEN_PIN     7
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_CYCLES  450 // read and report battery voltage every this many sleep cycles (ex 30cycles * 8sec sleep = 240sec/4min). For 450 cyclesyou would get ~1 hour intervals
#define BATT_FORMULA(reading) 0.006082183 * reading - 0.227388 // >>> fine tune this parameter to match your voltage when fully charged
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

#ifdef ENABLE_ATC
  RFM69_ATC radio;
#else
  RFM69 radio;
#endif

float batteryVolts = 5;
char BATstr[20]; //longest battery voltage reading message = 9chars
char sendBuf[32];
byte sendLen;
void checkBattery(void);
bool ntermino = true;
IRrecv irrecv(RECV_PIN);
IRsend irsend;
decode_results results;
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

// Storage for the recorded code
int codeType = -1; // The type of code
unsigned long codeValue; // The code value if not raw
unsigned int rawCodes[RAWBUF]; // The durations if raw
int codeLen; // The length of the code
int toggle = 0; // The RC5/6 toggle state

int lastButtonState;
long lastPeriod = 0;
uint16_t batteryReportCycles=0;
int cycleCount=BATT_CYCLES;
int bCycleCount=0;
bool firsttimeB=true;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    Serial.println(EEPROM.read(0));
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

    irrecv.enableIRIn(); // Start the receiver
    pinMode(BUTTON_PIN, INPUT);
    digitalWrite(BUTTON_PIN,HIGH);
    pinMode(BUTTOND_PIN, INPUT);
    digitalWrite(BUTTOND_PIN,HIGH);
    pinMode(LED, OUTPUT);

    pinMode(BATEN_PIN, OUTPUT);

    char buff[50];
}

// Stores the code for later playback
// Most of this code is just logging
void storeCode(decode_results *results)
{
    codeType = results->decode_type;
    int count = results->rawlen;
    if (codeType == UNKNOWN)
    {
        Serial.println("Received unknown code, saving as raw");
        codeLen = results->rawlen - 1;
        // To store raw codes:
        // Drop first value (gap)
        // Convert from ticks to microseconds
        // Tweak marks shorter, and spaces longer to cancel out IR receiver distortion
        for (int i = 1; i <= codeLen; i++)
        {
            if (i % 2)
            {
                // Mark
                rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK - MARK_EXCESS;
                Serial.print(" m");
            }
            else
            {
                // Space
                rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK + MARK_EXCESS;
                Serial.print(" s");
            }
            Serial.print(rawCodes[i - 1], DEC);
        }
        Serial.println("");
    }
    else
    {
        if (codeType == NEC)
        {
            Serial.print("Received NEC: ");
            if (results->value == REPEAT)
            {
                // Don't record a NEC repeat value as that's useless.
                Serial.println("repeat; ignoring.");
                return;
            }
        }
        else if (codeType == SONY)
        {
            Serial.print("Received SONY: ");
        }
        else if (codeType == RC5)
        {
            Serial.print("Received RC5: ");
        }
        else if (codeType == RC6)
        {
            Serial.print("Received RC6: ");
        }
        else
        {
            Serial.print("Unexpected codeType ");
            Serial.print(codeType, DEC);
            Serial.println("");
        }
        Serial.println(results->value, HEX);
        codeValue = results->value;
        codeLen = results->bits;

        char tosend[29];
        String rta = "T:"+String(codeType,DEC)+"L:"+String(codeLen,DEC)+"E:"+String(codeValue,HEX);
        rta.toCharArray(tosend,29);
        sprintf(sendBuf, "(%d)iD:CTR %s",EEPROM.read(1),tosend);
        sendLen = strlen(sendBuf);
        DEBUG(sendBuf)
        if (radio.sendWithRetry(GATEWAYID, sendBuf, sendLen))
        {
            DEBUG("CTR ACK:OK! RSSI:");
            DEBUG(radio.RSSI);
            //batteryReportCycles = 0;
        }
        else DEBUG("CTR ACK:NOK...");
    }
}
void sendCode(int repeat)
{
    if (codeType == NEC)
    {
        if (repeat)
        {
            irsend.sendNEC(REPEAT, codeLen);
            Serial.println("Sent NEC repeat");
        }
        else
        {
            irsend.sendNEC(codeValue, codeLen);
            Serial.print("Sent NEC ");
            Serial.println(codeValue, HEX);
        }
    }
    else if (codeType == SONY)
    {
        irsend.sendSony(codeValue, codeLen);
        Serial.print("Sent Sony ");
        Serial.println(codeValue, HEX);
    }
    else if (codeType == RC5 || codeType == RC6)
    {
        if (!repeat)
        {
            // Flip the toggle bit for a new button press
            toggle = 1 - toggle;
        }
        // Put the toggle bit into the code to send
        codeValue = codeValue & ~(1 << (codeLen - 1));
        codeValue = codeValue | (toggle << (codeLen - 1));
        if (codeType == RC5)
        {
            Serial.print("Sent RC5 ");
            Serial.println(codeValue, HEX);
            irsend.sendRC5(codeValue, codeLen);
        }
        else
        {
            irsend.sendRC6(codeValue, codeLen);
            Serial.print("Sent RC6 ");
            Serial.println(codeValue, HEX);
        }
    }
    else if (codeType == UNKNOWN /* i.e. raw */)
    {
        // Assume 38 KHz
        irsend.sendRaw(rawCodes, codeLen, 38);
        Serial.println("Sent raw");
    }
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
    if(digitalRead(BUTTOND_PIN)==LOW)
    {
        if(bCycleCount == 0)
        {
            digitalWrite(LED, HIGH);
            DEBUGln("Boton presionado");
            EEPROM.write(0,255);
            EEPROM.write(1,255);
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
  checkBattery();
  checkButton();
    if (digitalRead(BUTTOND_PIN)==LOW)
    {
        //Serial.println("Pressed, sending");
        digitalWrite(LED, HIGH);
        sendCode(0);
        digitalWrite(LED, LOW);
        delay(50); // Wait a bit between retransmissions
        //irrecv.enableIRIn(); // Re-enable receiver
    }
    else if(digitalRead(BUTTON_PIN)==LOW)
    {
        irrecv.resume();
        digitalWrite(LED, HIGH);
        delay(200);
        digitalWrite(LED, LOW);
        delay(200);
        digitalWrite(LED, HIGH);
        delay(200);
        digitalWrite(LED, LOW);
        boolean configured=false;
        for(int i=0;i<150;i++)
        {
            if (irrecv.decode(&results))
            {
                digitalWrite(LED, HIGH);
                storeCode(&results);
                irrecv.resume(); // resume receiver
                digitalWrite(LED, LOW);
                i=150;
                configured=true;
            }
            else
            {
                delay(100);
                Serial.print(".");
            }
        }
        if(configured)
        {
            digitalWrite(LED, HIGH);
            delay(1000);
            digitalWrite(LED, LOW);
        }
        else
        {
            Serial.println("");
            Serial.println("No signal found");
            digitalWrite(LED, HIGH);
            delay(200);
            digitalWrite(LED, LOW);
            delay(200);
            digitalWrite(LED, HIGH);
            delay(200);
            digitalWrite(LED, LOW);
            delay(200);
            digitalWrite(LED, HIGH);
            delay(200);
            digitalWrite(LED, LOW);
        }
    }

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
                ntermino=false;
                radio.sendWithRetry(GATEWAYID, "(%d)iD:CTR T:0L:0E:0",EEPROM.read(1), 20);
            }
            Serial.println();
        }

        int currPeriod = millis()/TRANSMITPERIOD;
        if (currPeriod != lastPeriod)
        {
            lastPeriod=currPeriod;
            if (radio.sendWithRetry(GATEWAYID, "(XXX)iD:CTR LOGIN", 17))
                Serial.print(" ok!");
            else Serial.print(" nothing...");

            Serial.println();
            Blink(LED,3);
        }
    }

    //check for any received packets
    for(int i=0;i<2;i++)
    {
        String inputstr = "";
        if (radio.receiveDone())
        {
            Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
            for (byte i = 0; i < radio.DATALEN; i++)
            {
                Serial.print((char)radio.DATA[i]);
                char inChar = (char)radio.DATA[i];
                inputstr += inChar;
            }
            Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
            if (radio.ACKRequested())
            {
                radio.sendACK();
                Serial.print(" - ACK sent");
            }
            Blink(LED,3);
            Serial.println();
        }

        if (inputstr != "")
        {
            Serial.println(inputstr);
            digitalWrite(LED, HIGH);
            if(inputstr.substring(0,3) == "SEND")
            {
                codeType=inputstr.substring(inputstr.indexOf(' '),inputstr.indexOf(',')).toInt();
                codeLen=inputstr.substring(inputstr.indexOf(','),inputstr.indexOf(',',inputstr.indexOf(','))).toInt();
                codeValue=inputstr.substring(inputstr.indexOf(',',inputstr.indexOf(','))).toInt();
                //Serial.println("Pressed, sending");
                digitalWrite(LED, HIGH);
                sendCode(0);
                digitalWrite(LED, LOW);
                delay(50); // Wait a bit between retransmissions
                irrecv.enableIRIn(); // Re-enable receiver
            }
            if(inputstr == "SET")
            {
                irrecv.resume();
                digitalWrite(LED, HIGH);
                delay(200);
                digitalWrite(LED, LOW);
                delay(200);
                digitalWrite(LED, HIGH);
                delay(200);
                digitalWrite(LED, LOW);
                boolean configured=false;
                for(int i=0;i<50;i++)
                {
                    if (irrecv.decode(&results))
                    {
                        digitalWrite(LED, HIGH);
                        storeCode(&results);
                        irrecv.resume(); // resume receiver
                        digitalWrite(LED, LOW);
                        i=50;
                        configured=true;
                    }
                    else
                    {
                        delay(100);
                        Serial.println("Try Again");
                    }
                }
                if(configured)
                {
                    digitalWrite(LED, HIGH);
                    delay(1000);
                    digitalWrite(LED, LOW);
                }
                else{
                    digitalWrite(LED, HIGH);
                    delay(200);
                    digitalWrite(LED, LOW);
                    delay(200);
                    digitalWrite(LED, HIGH);
                    delay(200);
                    digitalWrite(LED, LOW);
                    delay(200);
                    digitalWrite(LED, HIGH);
                    delay(200);
                    digitalWrite(LED, LOW);
                }
            }
            digitalWrite(LED, LOW);
        }
        //motionDetected=false; //do NOT move this after the SLEEP line below or motion will never be detected
    }
    // LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    batteryReportCycles++;
}
