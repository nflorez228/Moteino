#include <SoftwareSerial.h>


#define SERIAL_BAUD   38400

SoftwareSerial mySerial1(13, 12); // RX, TX
SoftwareSerial mySerial2(11, 10); // RX, TX
SoftwareSerial mySerial3(9, 8); // RX, TX
SoftwareSerial mySerial4(7, 6); // RX, TX
SoftwareSerial mySerial5(5, 4); // RX, TX
SoftwareSerial mySerial6(3, 2); // RX, TX

String inputstr = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
String inputstr1 = "";         // a string to hold incoming data
boolean stringComplete1 = false;  // whether the string is complete
String inputstr2 = "";         // a string to hold incoming data
boolean stringComplete2 = false;  // whether the string is complete
String inputstr3 = "";         // a string to hold incoming data
boolean stringComplete3 = false;  // whether the string is complete
String inputstr4 = "";         // a string to hold incoming data
boolean stringComplete4 = false;  // whether the string is complete
String inputstr5 = "";         // a string to hold incoming data
boolean stringComplete5 = false;  // whether the string is complete
String inputstr6 = "";         // a string to hold incoming data
boolean stringComplete6 = false;  // whether the string is complete


void setup() {
  // put your setup code here, to run once:
    Serial.begin(SERIAL_BAUD);
    mySerial1.begin(38400);
    mySerial2.begin(38400);
    mySerial3.begin(38400);
    mySerial4.begin(38400);
    mySerial5.begin(38400);
    mySerial6.begin(38400);
}


void checkSerial()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        if (inChar == '\n')
            stringComplete = true;
        else
            inputstr += inChar;
    }
}

void checkMySerial1()
{
    while (mySerial1.available())
    {
        char inChar = (char)mySerial1.read();
        if (inChar == '\n')
            stringComplete1 = true;
        else
            inputstr1 += inChar;
    }
}
void checkMySerial2()
{
    while (mySerial2.available())
    {
        char inChar = (char)mySerial2.read();
        if (inChar == '\n')
            stringComplete2 = true;
        else
            inputstr2 += inChar;
    }
}
void checkMySerial3()
{
    while (mySerial3.available())
    {
        char inChar = (char)mySerial3.read();
        if (inChar == '\n')
            stringComplete3 = true;
        else
            inputstr3 += inChar;
    }
}
void checkMySerial4()
{
    while (mySerial4.available())
    {
        char inChar = (char)mySerial4.read();
        if (inChar == '\n')
            stringComplete4 = true;
        else
            inputstr4 += inChar;
    }
}
void checkMySerial5()
{
    while (mySerial5.available())
    {
        char inChar = (char)mySerial5.read();
        if (inChar == '\n')
            stringComplete5 = true;
        else
            inputstr5 += inChar;
    }
}
void checkMySerial6()
{
    while (mySerial6.available())
    {
        char inChar = (char)mySerial6.read();
        if (inChar == '\n')
            stringComplete6 = true;
        else
            inputstr6 += inChar;
    }
}

void loop() {
  checkSerial();
  mySerial1.listen();
  delay(40);
  checkMySerial1();
  mySerial2.listen();
  delay(40);
  checkMySerial2();
  mySerial3.listen();
  delay(40);
  checkMySerial3();
  mySerial4.listen();
  delay(40);
  checkMySerial4();
  mySerial5.listen();
  delay(40);
  checkMySerial5();
  mySerial6.listen();
  delay(40);
  checkMySerial6();
    if (stringComplete)
    {

        inputstr = "";
        stringComplete = false;
    }
    if (stringComplete1)
    {
      int inicio = inputstr1.indexOf(':')+1;
      int fin = inputstr1.indexOf('-');
      String rta = inputstr1.substring(inicio,fin);
      inputstr1.replace(rta,"1f_"+rta);
        Serial.println(inputstr1);
        inputstr1 = "";
        stringComplete1 = false;
    }
    if (stringComplete2)
    {
      int inicio = inputstr2.indexOf(':')+1;
      int fin = inputstr2.indexOf('-');
      String rta = inputstr2.substring(inicio,fin);
      inputstr2.replace(rta,"1e_"+rta);
        Serial.println(inputstr2);
        inputstr2 = "";
        stringComplete2 = false;
    }
    if (stringComplete3)
    {
      int inicio = inputstr3.indexOf(':')+1;
      int fin = inputstr3.indexOf('-');
      String rta = inputstr3.substring(inicio,fin);
      inputstr3.replace(rta,"1d_"+rta);
        Serial.println(inputstr3);
        inputstr3 = "";
        stringComplete3 = false;
    }
    if (stringComplete4)
    {
      int inicio = inputstr4.indexOf(':')+1;
      int fin = inputstr4.indexOf('-');
      String rta = inputstr4.substring(inicio,fin);
      inputstr4.replace(rta,"1c_"+rta);
        Serial.println(inputstr4);
        inputstr4 = "";
        stringComplete4 = false;
    }
    if (stringComplete5)
    {
      int inicio = inputstr5.indexOf(':')+1;
      int fin = inputstr5.indexOf('-');
      String rta = inputstr5.substring(inicio,fin);
      inputstr5.replace(rta,"1b_"+rta);
        Serial.println(inputstr5);
        inputstr5 = "";
        stringComplete5 = false;
    }
    if (stringComplete6)
    {
      int inicio = inputstr6.indexOf(':')+1;
      int fin = inputstr6.indexOf('-');
      String rta = inputstr6.substring(inicio,fin);
      inputstr6.replace(rta,"1a_"+rta);
        Serial.println(inputstr6);
        inputstr6 = "";
        stringComplete6 = false;
    }
}