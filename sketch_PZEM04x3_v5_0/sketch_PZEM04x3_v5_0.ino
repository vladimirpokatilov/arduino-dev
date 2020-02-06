/***************************************************************************
 * 3-Phase power meter for Arduino MEGA2560
 * 
 * Modules: SSD1036_128_64, W5500, PZEM04 x3
 * 
 * 
 * Version: 5.0
 * 
 * v5.0 - start
 ***************************************************************************/

// ********************************* OLED Display  ---------------------------------
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire);

// ********************************** Eth & MQTT --------------------------------------------------------------
#include <SPI.h>
#include <Ethernet.h>
//#include <Ethernet2.h>       // For non W5500 eth module

#define CLIENT_VERSION "ASC_ESBEARA639_SSD1306_W5500_MQTT_2.2"

// --- ETH ------
EthernetClient ethClient;
int NET_ERROR_FLAG=0;
uint8_t mac[6] = {0xF4, 0x16, 0x3E, 0x12, 0xD8, 0x30}; // MAC for SET F4-16-3E-12-D8-30


// --- MQTT -----
#include <PubSubClient.h>
PubSubClient mqttClient;

uint8_t mqtt_ip[4] = {192,168,17,170}; //IP 192.168.17.1170
int mqtt_port = 1883;
char mqtt_id[17]="asc-01";

int MQTT_EEPROM_addr=32; // Адрес конфигурации в MQTT в EEPROM


//********************* MOSFET  ******************************************************************
#define MSFT_lines 12                                     // количество линий
byte MSFT_pin[MSFT_lines]={2,3,4,5,6,7,8,9,10,11,44,45}; // pwm pins 
byte MSFT_val[MSFT_lines]={1,2,3,4,5,6,7,8,9,10,11,12}; // pwm start voltage, must be read from EEPROM
float MSFT_voltage=10.1;                               // DC VCC IN from Power Adapter 10V recomended
int MSFT_EEPROM_addr=0;                               //Адресс начала масива в EEPROM

//********************** LED ********************************************************************
byte LED_pin=13;    // LED GPIO pin 
int led_now = 0;     // how bright the LED is
int led_last = 0;     // how many points to fade the LED by
int led_bright = 150;  // led bright


//********************* Buttons *****************************************************************
//#define BUTTON_PIN 10       // кнопка подключена сюда (PIN --- КНОПКА --- GND)
//#include "GyverButton.h"
//GButton butt1(BUTTON_PIN);

//********************** RELAY 24V **************************************************************
byte RELAY_24V_pin=12;
byte RELAY_24V_status=0;

//********************** EEPROM *****************************************************************
#include <EEPROM.h>


// ***************  Global SETs ****************************************************** 
long CIRCLE_TIMER_1 = 60; // send mqtt data in sec
long CIRCLE_TIMER_2 = 90; // dhcp renewal in sec
long CIRCLE_TIMER_3 = 5;  // refrash lcd
long CIRCLE_TIMER_4 = 62; // reserv
long CIRCLE_TIMER_5 = 100; // led blinker in ms
//int  CIRCLE_STATE = 0;  //reserv

long CIRCLE_LASTMSG_1 = 0; // send mqtt data
long CIRCLE_LASTMSG_2 = 0; // reserv
long CIRCLE_LASTMSG_3 = 0; // reserv
long CIRCLE_LASTMSG_4 = 0; // reserv
long CIRCLE_LASTMSG_5 = 0; // reserv

long  upTime = 0; // Uptime counter in seconds

byte DEBUG_LEVEL=0;


/********************************* Shell Setup *********************************************************************************************************/
#include <Shell.h>
Shell shell;
char strPrompt[20]="";

void(* resetFunc) (void) = 0;


void cmdSHOW_MQTT(Shell &shell, int argc, const ShellArguments &argv)
{
   // if (argc > 1 && !strcmp(argv[1], "on"))
   //     digitalWrite(13, HIGH);
   // else
   //     digitalWrite(13, LOW);

        Serial.println("*** MQTT Configuration ***");
        Serial.print("Client   ID: ");  Serial.println(mqtt_id);
        Serial.print("MQTT Server: ");  Serial.println(mqttIpToStr());
        Serial.print("MQTT   Port: ");  Serial.println(mqtt_port);
       
}

void cmdSET_MQTT_IP(Shell &shell, int argc, const ShellArguments &argv)
{
  String strIP(argv[1]);
  int Parts[4] = {0,0,0,0};
  int Part = 0;
  
  if (argc > 1) {
    
   for ( byte i=0; i<strIP.length(); i++ )
   {
     char c = strIP[i];
     if ( c == '.' )  {
         Part++;
        continue;
     }
     Parts[Part] *= 10;
     Parts[Part] += c - '0';
    } //for
    mqtt_ip[0]=Parts[0]; mqtt_ip[1]=Parts[1]; mqtt_ip[2]=Parts[2]; mqtt_ip[3]=Parts[3];
    EEPROM_MQTT_conf_write(MQTT_EEPROM_addr);
    mqttClient.setServer(mqtt_ip, mqtt_port);
    
  } else {Serial.println("ERROR: Bad parametrs"); }; 
}

void cmdSET_MQTT_PORT(Shell &shell, int argc, const ShellArguments &argv)
{
  String portStr(argv[1]);
  
  if (argc = 2 && portStr.toInt()>0 && portStr.toInt()<=65535) {
   
    mqtt_port=portStr.toInt();
    EEPROM_MQTT_conf_write(MQTT_EEPROM_addr);
    mqttClient.setServer(mqtt_ip, mqtt_port);
    
  } else {   Serial.println("ERROR: Bad parametrs"); }
  
}

void cmdSET_MQTT_ID(Shell &shell, int argc, const ShellArguments &argv)
{
  if (argc > 1 && strlen(argv[1])<20 ) {
       
       strcpy(mqtt_id,argv[1]);
       EEPROM_MQTT_conf_write(MQTT_EEPROM_addr);
       sprintf(strPrompt,"%s> ",mqtt_id); 
       shell.setPrompt(strPrompt);
       
  } else { Serial.println("ERROR: Bad parametrs or lenght !!!");};
 
}

ShellCommand(show_mqtt, "- Turns the status LED on or off", cmdSHOW_MQTT);
ShellCommand(set_mqtt_ip, "- Set IP of MQTT broker", cmdSET_MQTT_IP);
ShellCommand(set_mqtt_port, "- Set port of MQTT broker", cmdSET_MQTT_PORT);
ShellCommand(set_mqtt_id, "- Set ID of MQTT client", cmdSET_MQTT_ID);

void cmdSHOW_PWMLINES(Shell &shell, int argc, const ShellArguments &argv)
{
   
        Serial.println("*** PWM Lines Status ***");
        for (byte i=0; i<MSFT_lines; i++){
           Serial.print("PWM pin["); Serial.print(MSFT_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println(MSFT_val[i]);
          }     
}
ShellCommand(show_pwmlines, "- Show state of PWM lines", cmdSHOW_PWMLINES);



void cmdSET_PWMLINE(Shell &shell, int argc, const ShellArguments &argv)
{
  String numStr(argv[1]);
  String valStr(argv[2]);

  if (argc = 3 && numStr.toInt()>=1 && numStr.toInt()<=MSFT_lines && valStr.toInt()>=0 && valStr.toInt()<=255)
       MSFT_set(numStr.toInt()-1, valStr.toInt());
   else
       Serial.println("ERROR: Bad parametrs");

  //MSFT_set(numStr.toInt()-1, valStr.toInt());
}
ShellCommand(set_pwmline, "- Set pwm line number in value. Syn: set_pwmline number value", cmdSET_PWMLINE);



void cmdSHOW_IP(Shell &shell, int argc, const ShellArguments &argv)
{
   Serial.println("*** ETHERNET Config ***");
   Serial.print("IP  addr: "); Serial.println(Ethernet.localIP());
   Serial.print("NET mask: "); Serial.println(Ethernet.subnetMask());
   Serial.print("Gateway : "); Serial.println(Ethernet.gatewayIP());
   Serial.print("MAC addr: "); Serial.println(macToStr(mac)); 
}
ShellCommand(show_ip, "- Show interface IP adress ", cmdSHOW_IP);



void cmdSET_RELAY24V(Shell &shell, int argc, const ShellArguments &argv)
{
   if (argc > 1 && !strcmp(argv[1], "on"))
        RELAY_24V_on(); //ON 24V POWER
    else
        RELAY_24V_off(); //ON 24V POWER
}
ShellCommand(set_relay24v, "- Turns the status Relay24v on or off", cmdSET_RELAY24V);



void cmdREBOOT(Shell &shell, int argc, const ShellArguments &argv)
{
  resetFunc();
}

ShellCommand(reboot, "- Software reboot controller", cmdREBOOT);

/**************END SHELL SECTION ********* END SHELL SECTION ***************************************************/



//**************************************************************************************************
//*************************<<< MAIN SECTION >>>*****************************************************
//**************************************************************************************************
void setup() {
  byte i;

  Serial.begin(9600);
  Serial.println("Boiler Actuator & Pump Controller System");
  Serial.print("Version: ");  Serial.println(CLIENT_VERSION);
  Serial.println("------------------------------------------------------------------");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // инициализация дисплея по интерфейсу I2C, адрес 0x3C
  // Show image buffer on the display hardware.
  // Since the buffer is intialized with an Adafruit splashscreen
  // internally, this will display the splashscreen.
  display.display();

  //You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(53);    // ATMEGA 2560 Core from RobotDyn
  //Ethernet.init(10);  // Most Arduino shields
  //Ethernet.init(5);   // MKR ETH shield
  //Ethernet.init(0);   // Teensy 2.0
  //Ethernet.init(20);  // Teensy++ 2.0
  //Ethernet.init(15);  // ESP8266 with Adafruit Featherwing Ethernet
  //Ethernet.init(33);  // ESP32 with Adafruit Featherwing Ethernet
  
  delay(1000);
  
  display.clearDisplay(); // очистка дисплея
  display.display();
  display.setTextSize(2); // установка размер шрифта
  display.setTextColor(WHITE); // установка цвета текста
  display.setCursor(0, 0); // установка курсора в позицию X = 0; Y = 0
  display.setTextColor(BLACK, WHITE);
  display.println (" Actuator "); 
  display.println ("  System  ");
  display.println ("   Control");
  display.display();// записываем в буфер дисплея нашу фразу? переходим на след. строку
  display.setTextColor(WHITE, BLACK);
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.println (" ");
  display.println("<<<<<<<-- 2.0 -->>>>>>");
  display.println("   bob@ra-home.net");
  display.display(); // и её выводим на экран
  delay(2000);

  display.clearDisplay(); // очистка дисплея
  display.display();
  display.setTextSize(1); // установка размер шрифта
  display.setTextColor(WHITE); // установка цвета текста
  display.setCursor(0, 0); // установка курсора в позицию X = 0; Y = 0
  display.println ("Ethernet address:");  display.display();


  display.print ("MAC "); 
  display.println (macToStr(mac));
  display.display(); display.println ("");
  display.println ("Waiting IP by DHCP...");  display.display();
  Serial.print(millis()); Serial.println(": ETH: Waiting IP by DHCP...");

  
//  ---- Setup Ethernet communication using DHCP -------------------------------------------------------------------------------------------
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Unable to configure Ethernet using DHCP !!!");
    display.println ("Error: Unable to get IP using DHCP !!!");  display.display();
    NET_ERROR_FLAG=1;

    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
        NET_ERROR_FLAG=2;
        delay(10000);
    }
    
    //Check for Ethernet Link
    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected.");
        display.println ("Ethernet cable is not connected"); display.display();
         NET_ERROR_FLAG=3;
         delay(10000);
    }
  } // if Ethernet.begin()


  Serial.print(millis()); Serial.print(": MAC address: "); Serial.println(macToStr(mac));
  Serial.print(millis()); Serial.print(": IP address: "); Serial.println(Ethernet.localIP());
  display.print ("IP: "); display.println (Ethernet.localIP()); display.display();
  for (byte i=0; i<5; i++){Serial.print("!"); display.print ("!"); display.display(); delay(1000);};
  Serial.println("!");

// MSFT SETUP part ----------------------------------------------------------------------------------------------------------

  MSFT_setup(); // SET UP PWM
     
  display.println ("!!! Attantion !!!"); display.println ("Now 24V will be ON"); display.print ("Wait 10 sec"); display.display();
  Serial.print("!!! Attantion !!! Now 24V will be ON. Wait 10 sec...");
  for (byte i=0; i<10;i++){delay(1000); Serial.print("!"); display.print ("!"); display.display();}
  Serial.println("!");
  pinMode(RELAY_24V_pin, OUTPUT);  
  RELAY_24V_on(); //ON 24V POWER
  delay(5000);

// Start NET services --------------------------------------------------------------------------------------------------------
  display.clearDisplay(); // очистка дисплея
  display.display();
  display.setTextSize(1); // установка размер шрифта
  display.setTextColor(WHITE); // установка цвета текста
  display.setCursor(0, 0);

  
  // MQTT client setup -------------------
  EEPROM_MQTT_conf_read(MQTT_EEPROM_addr);
  mqttClient.setClient(ethClient);
  mqttClient.setServer(mqtt_ip, mqtt_port);
  mqttClient.setCallback(mqtt_callback);
  Serial.print(millis()); Serial.print(": MQTT: Client start connect to "); Serial.print(mqttIpToStr());Serial.print(":");Serial.println(mqtt_port);
  display.println("MQTT Client start..."); display.print(mqttIpToStr());display.print(":");display.println(mqtt_port); display.display();
  MQTT_chk_conect();
 
  for (byte i=0; i<5;i++){delay(1000); display.print ("!"); display.display();}

  sprintf(strPrompt,"%s> ",mqtt_id); 
  shell.setPrompt(strPrompt);
  shell.begin(Serial, 5);
  
} //setup

/***************************************************************************************************************************************/
/******************************* <<<  MAIN CIRCLE >>>>> *****************************************************************************************/
/***************************************************************************************************************************************/
void loop() {
  
  long CIRCLE_NOW_1 = millis(); long CIRCLE_NOW_2 = millis(); long CIRCLE_NOW_3 = millis(); long CIRCLE_NOW_4 = millis(); long CIRCLE_NOW_5 = millis();

 
  mqttClient.loop();  // MQTT lisen replay from server 
  shell.loop();
  
  //serialShell_loop();  // Read serial command
  //check_web_connections(); // web server work
  //butt1.tick();  // обязательная функция отработки. Должна постоянно опрашиваться 

  // =========== CIRCLE 1 =============================================================================== 
  //MQTT SEND Data   
   CIRCLE_NOW_1 = millis(); 
  
   if (CIRCLE_NOW_1 - CIRCLE_LASTMSG_1 > (CIRCLE_TIMER_1*1000)) { 
    CIRCLE_LASTMSG_1 = CIRCLE_NOW_1;
    Serial.print(millis()); Serial.println(": CIRCLE 1:  Start ....");
    sendMQTTData();
    Serial.print(millis()); Serial.println(": CIRCLE 1:  Finished!");
   } //if
  //==================================================================================================== 

  // =========== CIRCLE 2 =============================================================================== 
  // CHECK DHCP renewal   
   CIRCLE_NOW_2 = millis(); 
  
   if (CIRCLE_NOW_2 - CIRCLE_LASTMSG_2 > (CIRCLE_TIMER_2*1000)) { 
    CIRCLE_LASTMSG_2 = CIRCLE_NOW_2;
    Serial.print(CIRCLE_NOW_2); Serial.print(": ");
    Serial.print("DHCP Requesting ... "); 
    Serial.print(Ethernet.maintain());
    Serial.print(" New IP:");  
    Serial.println(Ethernet.localIP());
   }  
  //==================================================================================================== 

  // =========== CIRCLE 3 =============================================================================== 
  // Refresh LCD   
   CIRCLE_NOW_3 = millis(); 
  
   if (CIRCLE_NOW_3 - CIRCLE_LASTMSG_3 > (CIRCLE_TIMER_3*1000)) { 
    CIRCLE_LASTMSG_3 = CIRCLE_NOW_3;
    if ( DEBUG_LEVEL > 0 ) {Serial.print(CIRCLE_NOW_3); Serial.print(": "); Serial.println("LCD Refreshing ... "); };
    refresh_LCD_main();
   }  
  //==================================================================================================== 
  
  // =========== CIRCLE 5 =============================================================================== 
  // LED BLINK  
  CIRCLE_NOW_5 = millis();
  
  if (CIRCLE_NOW_5 - CIRCLE_LASTMSG_5 > CIRCLE_TIMER_5) {
    CIRCLE_LASTMSG_5 = CIRCLE_NOW_5;

    if (led_last == 0 ) led_now =1;
    if (led_last == 1 ) led_now =0;
    digitalWrite(LED_pin, led_now);
    //analogWrite(LED_pin, led_now*led_bright);
    led_last = led_now;
  }
  //=====================================================================================================
} //loop











/************************************************************************
 *  MOSFET SETUP LINES
 ***********************************************************************/
void MSFT_setup()
{
      
  display.clearDisplay(); // очистка дисплея
  display.display();
  display.setTextSize(1); // установка размер шрифта
  display.setTextColor(WHITE); // установка цвета текста
  display.setCursor(0, 0); // установка курсора в позицию X = 0; Y = 0
  display.println ("MSFT SETUP:"); display.println ("------------------"); display.display();
  
  EEPROM_MSFT_val_read_all(MSFT_EEPROM_addr);
  
  for (byte i = 0; i < (MSFT_lines); i++) {
    Serial.print(millis()); Serial.print(": MSFT: Setup PWM pin["); Serial.print(MSFT_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println(MSFT_val[i]);
    display.print (" L"); display.print (i+1); 
    display.display();

    pinMode(MSFT_pin[i], OUTPUT);
    //analogWrite(MSFT_pin[i], MSFT_val[i]);
    //analogWrite(MSFT_pin[i], map(MSFT_val[i], 0, 100, 0, 255));
    MSFT_set(i,MSFT_val[i]);
    
    delay(50);
  
  };// for i

  display.println (" ");
  display.println ("------------------");

 
} //MSFT_setup()

/************************************************************************
 *  MOSFET SET LINE line_num value line_val
 ***********************************************************************/
void MSFT_set(int line_num, int line_val ){
    
  if (line_val>=0 && line_val<=255) { MSFT_val[line_num]=line_val;
  } else {
    if (line_val<0) MSFT_val[line_num]=0;
    if (line_val>255) MSFT_val[line_num]=255;
    
  }
 
  EEPROM_MSFT_val_write(MSFT_EEPROM_addr,line_num);
  analogWrite(MSFT_pin[line_num], MSFT_val[line_num]);
}

/************************************************************************
 *  Включение питания 24 Вольта
 ***********************************************************************/
void RELAY_24V_on()
{
  //pinMode(RELAY_24V_pin, OUTPUT);
 
  Serial.print(millis()); Serial.println(": RELAY: Try ON 24V adapter relay !");

  digitalWrite(RELAY_24V_pin, LOW);
  RELAY_24V_status=1; //ON
  
}//RELAY_24_on  

/************************************************************************
 *  Выключение питания 24 Вольта
 ***********************************************************************/
void RELAY_24V_off()
{
  //pinMode(RELAY_24V_pin, OUTPUT);
 
  Serial.print(millis()); Serial.println(": RELAY: Try OFF 24V adapter relay !");

  digitalWrite(RELAY_24V_pin, HIGH);
  RELAY_24V_status=0; //ON
  
}//RELAY_24_off  

//*******************************EEPROM WORKING ***********************************************************************************
/************************************************************************
 *  Чтение значений актуаторов из памяти
 ***********************************************************************/
void EEPROM_MSFT_val_read_all(int addr) {    
 
  for(byte i = 0; i < MSFT_lines; i++) {
    MSFT_val[i] = EEPROM.read(addr+i);
    Serial.print(millis()); Serial.print(": EEPROM: Read MSFT PWM line["); Serial.print(i+1); Serial.print("] value = "); Serial.println(MSFT_val[i]);
  }
  
} // EEPROM_MSFT_val_read(int addr)


/************************************************************************
 *  Запись значений актуаторов в память
 ***********************************************************************/
void EEPROM_MSFT_val_write_all(int addr) {

  for(byte i = 0; i < MSFT_lines; i++) {
    EEPROM.write(addr+i, MSFT_val[i]);
    Serial.print(millis()); Serial.print(": EEPROM: Write MSFT PWM line["); Serial.print(i+1); Serial.print("] value = "); Serial.println(MSFT_val[i]);
  }
  
}//EEPROM_MSFT_val_write(int addr)

/************************************************************************
 *  Запись значения актуатора в память
 ***********************************************************************/
void EEPROM_MSFT_val_write(int addr, int line_num) {

    if (MSFT_val[line_num] != EEPROM.read(addr+line_num)){
     EEPROM.write(addr+line_num, MSFT_val[line_num]);
     Serial.print(millis()); Serial.print(": EEPROM: Write MSFT PWM line["); Serial.print(line_num+1); Serial.print("] value = "); Serial.println(MSFT_val[line_num]);
    }
  
}//EEPROM_MSFT_val_write

/************************************************************************
 *  Чтение значения актуатора из памяти
 ***********************************************************************/
byte EEPROM_MSFT_val_read(int addr, int line_num) {
  byte r;
    
     
     r = EEPROM.read(addr+line_num);
     Serial.print(millis()); Serial.print(": EEPROM: Read MSFT PWM line["); Serial.print(line_num+1); Serial.print("] value = "); Serial.println(r);
    
  return r;
  
}//EEPROM_MSFT_val_read

/************************************************************************
 *  MQTT Chk Connetc to Server
 ***********************************************************************/
int MQTT_chk_conect() {
  char msgParam[128];
  
  if (!mqttClient.connected()){
    Serial.print(millis()); Serial.print(": ");
    Serial.println("MQTT: Client not connected! Reconnect ....");
    mqttClient.connect(mqtt_id);
    for(byte i = 0; i < MSFT_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"pwmline",i+1);
      mqttClient.subscribe(msgParam);
      Serial.print(millis()); Serial.print(": "); Serial.print("MQTT: Subscribe on "); Serial.println(msgParam);
    }//for
  }; // нужно бы вынести в отдельную функцию !!!

  if (mqttClient.connected()) {
    return 1;
    }else{
      return 0;
    }
}

/************************************************************************
 *  Send Data 2 MQTT Server
 ***********************************************************************/
void sendMQTTData() {

  char msgVal[20];
  char msgBuffer[20];
  char msgParam[128];
  int p_val; 
  
  long upTimeMS = millis();
  upTime = upTimeMS / 1000;
  
  
  display.print("D2S>"); display.display();
  
  //MQTT_chk_conect();
     
  if ( MQTT_chk_conect() == 1 ){
    Serial.print(upTimeMS); Serial.print(": ");
    Serial.print("MQTT: Sending DATA -> MQTT Server; Uptime: "); Serial.println(upTime);
    
    
    display.println("MQTT"); display.display();
    
    sprintf(msgParam,"%s/%s",mqtt_id,"version"); mqttClient.publish(msgParam, CLIENT_VERSION);
    sprintf(msgParam,"%s/%s",mqtt_id,"mac");     mqttClient.publish(msgParam, macToStr(mac).c_str());
    sprintf(msgParam,"%s/%s",mqtt_id,"ip");      mqttClient.publish(msgParam, MyIpToStr().c_str());
    sprintf(msgParam,"%s/%s",mqtt_id,"uptime");  mqttClient.publish(msgParam, deblank(dtostrf(upTime, 6, 0, msgBuffer)));

    for(byte i = 0; i < MSFT_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"pwmline",i+1); 
      //p_val = map(MSFT_val[i], 0, 255, 0, 100);
      sprintf(msgVal,"%d",MSFT_val[i]); 
      //sprintf(msgVal,"%d",p_val); 
      Serial.print(msgParam); Serial.print(" "); Serial.println(msgVal);
      mqttClient.publish( msgParam, msgVal);
    }//for
    
    Serial.print(upTimeMS); Serial.print(": ");
    Serial.println("MQTT: Data was send OK !");
  }
}

/************************************************************************
 *  FOR sendMQTTData
 ***********************************************************************/
char * deblank(char *str) {
  char *out = str;
  char *put = str;
  
  for (; *str != '\0'; ++str) {
    if (*str != ' ') {
      *put++ = *str;
    }
  }
  *put = '\0';
  return out;
}

/************************************************************************
 *  MQTT Callback function
 ***********************************************************************/
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  char msgParam[100];
  long p_val;
  
  byte* p = (byte*)malloc(length+1);
  memcpy(p,payload,length+1);
  p[length] = '\0';

  String strTopic = String(topic);
  String strPayload = String((char*)p);

  Serial.print(millis()); Serial.print(": MQTT: Receive "); Serial.print(strTopic); Serial.print(" "); Serial.println(strPayload);
  for(byte i = 0; i < MSFT_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"pwmline",i+1);

      if ( String(msgParam) == strTopic ) {
         p_val = strPayload.toInt();
         //p_val = map(p_val, 0, 100, 0, 255);
         Serial.print(millis()); Serial.print(": MSFT: Line "); Serial.print(i+1); Serial.print(" set to "); Serial.println(p_val);
         MSFT_set(i,p_val);
        }
       
  }//for

  free(p);// Free the memory
}

/************************************************************************
 *  MAC to String
 ***********************************************************************/
String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

/************************************************************************
 *  My IP to String
 ***********************************************************************/
String MyIpToStr()
{
String my_ip="";
for (byte i = 0; i < 4; i++) {  
  my_ip = my_ip + String (Ethernet.localIP()[i]); 
  if (i<3) { my_ip = my_ip + "."; }
  };
return my_ip;
} 

/************************************************************************
 *  Mqtt IP to String
 ***********************************************************************/
String mqttIpToStr()
{
String my_ip="";
for (byte i = 0; i < 4; i++) {  
  my_ip = my_ip + String (mqtt_ip[i]); 
  if (i<3) { my_ip = my_ip + "."; }
  };
return my_ip;
}   
  

/************************************************************************
 *  Refresh LCD main screen
 ***********************************************************************/
void refresh_LCD_main()
{
  char msgParam[8];
  char msgVal[8];
  upTime = millis() / 1000;
  
  display.clearDisplay(); // очистка дисплея
  display.setTextSize(1); // установка размер шрифта
  display.setTextColor(WHITE, BLACK); // установка цвета текста
  display.setCursor(0, 0); // установка курсора в позицию X = 0; Y = 0

  display.print("Uptime:"); display.print(upTime); display.println("s"); display.println("--------------------");

  for(byte i = 0; i < MSFT_lines; i++) {
    sprintf(msgParam,"%02d",i+1); 
    sprintf(msgVal,"%03d",MSFT_val[i]); 
    display.print(msgParam); display.print(":"); display.print(msgVal);
  if (i==2 || i==5 || i==8 || i==11 ){display.println(" ");} else {display.print(" ");};
  
  }//for 
  
  display.println("--------------------");
  display.setTextColor(BLACK, WHITE);
  if (Ethernet.linkStatus() == LinkON){display.print("ETH ");}; 
  if (mqttClient.connected()){display.print("MQTT ");};
  
  display.display();
  
}//refresh_LCD_main()

/************************************************************************
 *  Запись MQTT конфиг в память
 ***********************************************************************/
void EEPROM_MQTT_conf_write(int addr) {

     EEPROM.put(addr, mqtt_ip);
     EEPROM.put(addr+4, mqtt_port);
     EEPROM.put(addr+6, mqtt_id);
     
     Serial.print(millis()); Serial.print(": EEPROM: Write MQTT config -> ID:"); Serial.print(mqtt_id); Serial.print(" Broker IP: "); Serial.print(mqttIpToStr()); Serial.print(":"); Serial.println(mqtt_port);
    
  
}//EEPROM_MQTT_conf_write

/************************************************************************
 *  Чтение MQTT конфига из флаш памяти
 ***********************************************************************/
void EEPROM_MQTT_conf_read(int addr) {
  
     EEPROM.get(addr, mqtt_ip);
     EEPROM.get(addr+4, mqtt_port);
     EEPROM.get(addr+6, mqtt_id);
     
     Serial.print(millis()); Serial.print(": EEPROM: Read MQTT config -> ID:"); Serial.print(mqtt_id); Serial.print(" Broker IP: "); Serial.print(mqttIpToStr()); Serial.print(":"); Serial.println(mqtt_port);

}//EEPROM_MQTT_conf_read
