#define TOUCH_MODULES_CST_SELF
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "TouchLib.h"
#include "Wire.h"
#include "pin_config.h"
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "esp_sleep.h"
#include "fonts.h"
#include "MapFloat.h"
#include <TickTwo.h>
#include "thingsProperties.h"


WiFiMulti wifiMulti;


int Zeilenabstand = 26; //normal = 20; bei Schriftgröße 4 passen 8 Zeilen auf das Display. Bei 7 Zeilen auf 25 gehen
int Pixeloffset  = +10; //Verschiebung nach oben


//-----------------Arrays für Spannungswandler-Wiederholungen-------------
const int ANALOG_PINS[] = {PIN_BAT_VOLT, PIN_ADC2_CH0}; //2 Array für die Analogwerte aufbeuaen, 4=, 11=
const int ARRAY_SIZES[] = {20, 100};  // Größen der Arrays
const int NUM_ARRAYS = sizeof(ARRAY_SIZES) / sizeof(ARRAY_SIZES[0]);
int* arrays[NUM_ARRAYS];  // Array von Pointern
float mittelwert[NUM_ARRAYS];
int reduced_ARRAY_SIZES[NUM_ARRAYS];
unsigned long executionTime;


//-----------------Akkuanzeige ADC1----------------------------------
int Vbattindikatorposition;
int Spannungsfarbe=TFT_ORANGE;
float Vbatt ;
float Vbattprozent ;
int Batteriebetrieb_Max ;
int Batteriebetrieb_Min ;
int Ladevorgang_Akkuvoll;
int Ladevorgang_Akkuleer;
int Ladeprozente;

//----------Variablen für ADC2----------------------------------
int Wert_PIN_ADC2_CH0;
float Spannung_PIN_ADC2_CH0;

// Declaration für TickTwo
void calc_battery_symbol ();
void readout_battery ();
void Display_Brightness ();

TickTwo timer4(calc_battery_symbol, 300); // batteriesymbol alle 200ms aufrischen um scrolleffekt zu erreichen
TickTwo timer5(readout_battery, 1000); // alle 2s batterie auslesen und mitteln
TickTwo timer6(Display_Brightness, 500); // alle 1s Display_Brightness berechnen und PWM setzen

// Display
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS820_SLAVE_ADDRESS, PIN_TOUCH_RES);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);


// ----------------------------PWM setzen DIsplay Helligkeit ----------------------------
void Display_Brightness() {
  int Helligkeit=mapFloat(Vbatt, 1480, 2400, 60, 250);
  if (Helligkeit >255) {Helligkeit=250;}
  if (Helligkeit <60) {Helligkeit=60;}
  analogWrite(PIN_LCD_BL, Helligkeit); //PWM setzen 
}


//-----------------------Ganzes Array auf Serielle ausgeben
void printArray(int *a, int x)
{
  for (int i = 0; i < x; i++)
  {
    Serial.print(String(i+1) + "=");
    Serial.print(a[i], DEC);
    Serial.print(" ");
  }
  Serial.println();
}


//------------------------Wertefelder auslesen der Analogwandler---------------------
void readout_battery (){
  unsigned long startTime = millis();  // Startzeit erfassen

  // Arrays definieren-----------
  for (int i = 0; i < NUM_ARRAYS; i++) {
    arrays[i] = new int[ARRAY_SIZES[i]];
  }

  for (int i = 0; i < NUM_ARRAYS; i++) { //
      for (int j = 0; j < ARRAY_SIZES[i]; j++) {
      arrays[i][j] = analogRead(ANALOG_PINS[i]);
    }
  // printArray(arrays[i], ARRAY_SIZES[i]); //passt, werte da
  }

  for (int i = 0; i < NUM_ARRAYS; i++) { //alle arrays nun sortieren
    qsort(arrays[i], ARRAY_SIZES[i], sizeof(int), cmpfunc); //Sortieren Sie das Array in aufsteigender Reihenfolge
    
    int numOutliers = round(0.4 * ARRAY_SIZES[i]);// löschen 40% Ausreißer (jeweils vorne und hinten), damit nur 60% aller Werte überbleiben
    
    for (int j = numOutliers; j < ARRAY_SIZES[i]-numOutliers; j++) { //Entferne aller Ausreißerwerte vorne und hinten aus dem Array
       arrays[i][j-numOutliers] = arrays[i][j]; //Shifte die ausraiese vor an erste stelle
     } 
    reduced_ARRAY_SIZES[i] = ARRAY_SIZES[i]-numOutliers*2; 
    // printArray(arrays[i], ARRAY_SIZES[i]); //tut perfekt
    // Serial.print("cutted Arraysize=");
    // Serial.println(reduced_ARRAY_SIZES[i]);

    int sum = 0; //------------Mittelwertberechung
    for (int j = 0; j < reduced_ARRAY_SIZES[i]; j++) { //danach Entferne aller Ausreißerwerte hinten
      sum += arrays[i][j];
    }
    mittelwert[i] = static_cast<float>(sum) / reduced_ARRAY_SIZES[i];  
    Serial.print("Mittelwert");
    Serial.print(i);
    Serial.print("=");
    Serial.println(mittelwert[i]);
  }
  Vbatt = mittelwert[0];
  
  Wert_PIN_ADC2_CH0 = mittelwert[1];
  Spannung_PIN_ADC2_CH0 = mapFloat(Wert_PIN_ADC2_CH0, 369, 1822, 12, 50);

  for (int i = 0; i < NUM_ARRAYS; i++) { //Speicher cleanup
    delete[] arrays[i];  // Speicher freigeben
  }

  unsigned long endTime = millis();  // Endzeit erfassen 
  executionTime = endTime - startTime;  // Verarbeitungszeit berechnen
  // Serial.print("Verarbeitungszeit: ");
  // Serial.print(executionTime);
  // Serial.println(" Millisekunden");

}  



// comparison function used by qsort
int cmpfunc(const void* a, const void* b) {
   return (*(int*)a - *(int*)b);
}
 

void calc_battery_symbol () {
  Ladevorgang_Akkuvoll = 2853; //2853
  Ladevorgang_Akkuleer = 2656; //2656
  Batteriebetrieb_Max = 2470;  //2470 Analogwert wenn Akku ganz voll
  Batteriebetrieb_Min = 1480;  //1480 Analogwert, wenn Display zu zittern beginnt
  Vbattprozent = mapFloat(Vbatt, Batteriebetrieb_Min, Batteriebetrieb_Max, 0, 100);
  Ladeprozente = mapFloat(Vbatt, Ladevorgang_Akkuleer, Ladevorgang_Akkuvoll, 0, 100);
  Serial.println("Vbatt%: "+ String(Vbatt,0) + " " + String(Vbattprozent,0));

  // Spannungindikator
  if (Vbatt>=2550)  {    // USB Ladung erkannt da Vbatt > 2500
      Vbattindikatorposition = (Vbattindikatorposition >= 20) ? 0 : Vbattindikatorposition + 4;  //Scrolle um 2 pixel weiter bis 20, dann wieder 0
      Spannungsfarbe=TFT_GREEN; Vbattprozent = Ladeprozente; 
            }
  else if ((Vbattprozent<=100) && (Vbattprozent>=90)) { Vbattindikatorposition=18; //10-50% grün
      Spannungsfarbe=TFT_GREEN;}
  else if ((Vbattprozent  <90) && (Vbattprozent>=80)) { Vbattindikatorposition=16;  //10-50% grün
      Spannungsfarbe=TFT_GREEN;}
  else if ((Vbattprozent  <80) && (Vbattprozent>=70)) { Vbattindikatorposition=14; //10-50% grün
      Spannungsfarbe=TFT_GREEN;}
  else if ((Vbattprozent  <70) && (Vbattprozent>=60)) { Vbattindikatorposition=12; //10-50% grün
      Spannungsfarbe=TFT_GREEN;}
  else if ((Vbattprozent  <60) && (Vbattprozent>=50)) { Vbattindikatorposition=10; //10-50% grün
      Spannungsfarbe=TFT_GREEN;}
  else if ((Vbattprozent  <50) && (Vbattprozent>=30)) { Vbattindikatorposition=5;  //30-49% orange
      Spannungsfarbe=TFT_ORANGE;}
  else if (Vbattprozent<30)                           { Vbattindikatorposition=0;  //<30 leer
      Spannungsfarbe=TFT_RED;    }
  
  if (Vbattprozent > 100) { Vbattprozent = 100; }

   // Batteriezeichen----------------
  Pixeloffset=-5;
  sprite.fillRect(205, 0, 80, 18, color_LCD);
  sprite.drawString(String(Vbatt,0) + " "  + String(Vbattprozent,0) + "% ",210,(Zeilenabstand*0),2);
  // sprite.drawString(String(Vbattprozent,0) + "%  ",245,(Zeilenabstand*0),2);
  sprite.drawCircle(10+280,18+Pixeloffset*2,7,TFT_BLACK);
  sprite.drawCircle(10+300,18+Pixeloffset*2,7,TFT_BLACK);
  sprite.fillCircle(10+280,18+Pixeloffset*2,6,color_LCD);
  sprite.fillCircle(10+300,18+Pixeloffset*2,6,color_LCD);
  sprite.drawLine(10+278,11+Pixeloffset*2,10+302,11+Pixeloffset*2,TFT_BLACK);
  sprite.drawLine(10+278,25+Pixeloffset*2,10+302,25+Pixeloffset*2,TFT_BLACK);
  sprite.fillRect(10+278,12+Pixeloffset*2,24,13,color_LCD);
  sprite.fillCircle(10+280+Vbattindikatorposition,18+Pixeloffset*2,4,Spannungsfarbe); 
  sprite.pushSprite(0,0);   
}


void setup() {
  
  Serial.begin(115200);
  delay(10);
  
  wifiMulti.addAP("xx", "xx");  
  wifiMulti.addAP("xxx", "xxx");
  wifiMulti.addAP("xx-xx", "xx-xx");
  
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH); // Akku an
  pinMode(PIN_TOUCH_RES, OUTPUT);
  digitalWrite(PIN_TOUCH_RES, LOW);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  delay(500);
  digitalWrite(PIN_TOUCH_RES, HIGH);
  pinMode(PIN_BUTTON_1,INPUT_PULLUP);
  pinMode(PIN_BUTTON_2,INPUT_PULLUP);

  tft.begin();
  tft.setRotation(3);
  sprite.createSprite(320,170);
  sprite.setTextColor(TFT_BLACK,color_LCD);
  sprite.fillRect(0, 0, 320, 170, color_LCD);
 

  if(wifiMulti.run() == WL_CONNECTED) {
      }
  Serial.print("rebooted...");

// ----------------------Arduino Cloud Activaion-----------------------
  initProperties();// Defined in thingProperties.h
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);// Connect to Arduino IoT Cloud
  setDebugMessageLevel(0);   //The default is 0 (only errors). Maximum is 4
  ArduinoCloud.printDebugInfo();

  timer4.start();
  timer5.start();
  timer6.start(); //Display_Brightness

  // analogReadResolution(12);  // Setzt die Auflösung des ADC auf 12 Bit (Standardwert)
  // analogSetAttenuation(ADC_6db);  // Schaltet die Verstärkung auf 6 dB um

  readout_battery ();
  calc_battery_symbol ();
}


void loop() {
  
  // ReadSonoff ();
  
  timer4.update();  //  4 calc_battery_symbol ();
  timer5.update();  //  5 readout_battery ();
  timer6.update();  //  6 Display_Brightness
  
  
  sensorValue = Wert_PIN_ADC2_CH0;
  voltage = Spannung_PIN_ADC2_CH0;
  bat_percentage = mapFloat(Spannung_PIN_ADC2_CH0, 21, 27.6, 0, 100); //22V as System Cut off Voltage & 27.6 as floating SOC=100
  
 
  if (bat_percentage >= 100) {bat_percentage = 100;}
  if (bat_percentage <= 0)  {bat_percentage = 0 ;}
  
  ArduinoCloud.update(); 
  
  Pixeloffset=20;
  sprite.fillRect(189, (Zeilenabstand*1+Pixeloffset), 101, 130, color_LCD);
  sprite.drawString("ADC Pin "+String(PIN_ADC2_CH0)+"=",10,(Zeilenabstand*1+Pixeloffset),4);
  sprite.drawString(String(Wert_PIN_ADC2_CH0),190,(Zeilenabstand*1+Pixeloffset),4);

  sprite.drawString("Volt (mapped)=",10,(Zeilenabstand*2+Pixeloffset),4);
  sprite.drawString(String(Spannung_PIN_ADC2_CH0,1)+"V",190,(Zeilenabstand*2+Pixeloffset),4);

  sprite.drawString("SOC =",10,(Zeilenabstand*3+Pixeloffset),4);
  sprite.drawString(String(bat_percentage)+"%",190,(Zeilenabstand*3+Pixeloffset),4);

  sprite.drawString("ct",10,(Zeilenabstand*4+Pixeloffset),4);
  sprite.drawString(String(executionTime)+"ms",190,(Zeilenabstand*4+Pixeloffset),4);
 
  
  if(digitalRead(PIN_BUTTON_1)==0) {
     digitalWrite(PIN_POWER_ON, LOW); // Akku off
     esp_deep_sleep_start();
  }
    
  if(digitalRead(PIN_BUTTON_2)==0) {
     digitalWrite(PIN_POWER_ON, LOW); // Akku off
     esp_deep_sleep_start();
  }
  
  sprite.pushSprite(0,0);
  delay(1000);   
}
