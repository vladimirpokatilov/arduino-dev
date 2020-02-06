/***************************************************************************
 * Relay Module Controller for Arduino MEGA2560
 * Modules: W5500, 16 Relay Module
 * 
 * 
 * Version: 1.2
 * 
 * v1.2 - add test
 * v1.1 - Add SHELL command
 * v1.0 - start
 ***************************************************************************/


// ********************************** Eth & MQTT --------------------------------------------------------------
#include <SPI.h>
#include <Ethernet.h>
//#include <Ethernet2.h>       // For non W5500 eth module

#define CLIENT_VERSION "RMC_16RelayModule_W5500_MQTT_1.2"

// --- ETH ------
EthernetClient ethClient;
int NET_ERROR_FLAG=0;
uint8_t mac[6] = {0xF4, 0x16, 0x3E, 0x12, 0xD8, 0x37}; // MAC for SET F4-16-3E-12-D8-30

// --- IP -------
uint8_t ip_addr[4] = { 192, 168, 17, 90 };
uint8_t ip_gw[4] = { 192, 168, 17, 1 };
uint8_t ip_mask[4] = { 255, 255, 255, 0 };
uint8_t ip_dns[4] = { 8, 8, 8, 8 };

int IP_EEPROM_addr=0; // Адрес конфигурации IP в EEPROM

byte DHCP_ENABLE = 1; // DHCP STATUS

// --- MQTT -----
#include <PubSubClient.h>
PubSubClient mqttClient;

uint8_t mqtt_ip[4] = {192,168,17,170}; //IP 192.168.17.1170
int mqtt_port = 1883;
char mqtt_id[17]="rmc-01";

int MQTT_EEPROM_addr=32; // Адрес конфигурации в MQTT в EEPROM


//********************* RELAY Module  ******************************************************************
#define RM_lines 16                                     // количество линий
//byte RM_pin[RM_lines]={46,47,44,45,42,43,40,41,38,39,36,37,34,35,32,33}; // relay pins 
byte RM_pin[RM_lines]={33,32,35,34,37,36,39,38,41,40,43,42,45,44,47,46}; // relay pins
byte RM_val[RM_lines]={HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH}; // relay start, must be read from EEPROM

int RM_EEPROM_addr=64;                               //Адресс начала масива в EEPROM

//********************** LED ********************************************************************
byte LED_pin=13;    // LED GPIO pin 
int led_now = 0;     // how bright the LED is
int led_last = 0;     // how many points to fade the LED by
int led_bright = 150;  // led bright


//********************** EEPROM *****************************************************************
#include <EEPROM.h>


// ***************  Global SETs ****************************************************** 
long CIRCLE_TIMER_1 = 60; // send mqtt data in sec
long CIRCLE_TIMER_2 = 90; // dhcp renewal in sec
long CIRCLE_TIMER_3 = 5;  // reserved for refrash lcd
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

void cmdSHOW_RMLINES(Shell &shell, int argc, const ShellArguments &argv)
{
   
        Serial.println("*** Relay Lines Status ***");
        for (byte i=0; i<RM_lines; i++){
           Serial.print("Relay pin["); Serial.print(RM_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println(RM_val[i]);
          }     
}
ShellCommand(show_rmlines, "- Show state of Relay lines", cmdSHOW_RMLINES);



void cmdSET_RMLINE(Shell &shell, int argc, const ShellArguments &argv)
{
  String numStr(argv[1]);
  String valStr(argv[2]);

  if (argc = 3 && numStr.toInt()>=1 && numStr.toInt()<=RM_lines && valStr.toInt()>=0 && valStr.toInt()<=255)
       RM_set(numStr.toInt()-1, valStr.toInt());
   else
       Serial.println("ERROR: Bad parametrs");

  //RM_set(numStr.toInt()-1, valStr.toInt());
}
ShellCommand(set_rmline, "- Set  relay line number in value. Syn: set_rmline number value", cmdSET_RMLINE);

// --- SET IP COMMANDS -----------------------------------------------------

void cmdSHOW_IP(Shell &shell, int argc, const ShellArguments &argv)
{
   Serial.println("*** ETHERNET Config ***");
   Serial.print("IP  addr: "); Serial.println(Ethernet.localIP());
   Serial.print("NET mask: "); Serial.println(Ethernet.subnetMask());
   Serial.print("Gateway : "); Serial.println(Ethernet.gatewayIP());
   Serial.print("MAC addr: "); Serial.println(macToStr(mac)); 
   Serial.print("DNS serv: "); Serial.println(Ethernet.dnsServerIP());
}

void cmdSET_IP_ADDR(Shell &shell, int argc, const ShellArguments &argv)
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
    ip_addr[0]=Parts[0]; ip_addr[1]=Parts[1]; ip_addr[2]=Parts[2]; ip_addr[3]=Parts[3];
    EEPROM_IP_conf_write(IP_EEPROM_addr);
    Serial.println("INFO: You need redoot device to applay new config !!!");
    
  } else {Serial.println("ERROR: Bad parametrs"); }; 
}

void cmdSET_IP_MASK(Shell &shell, int argc, const ShellArguments &argv)
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
    ip_mask[0]=Parts[0]; ip_mask[1]=Parts[1]; ip_mask[2]=Parts[2]; ip_mask[3]=Parts[3];
    EEPROM_IP_conf_write(IP_EEPROM_addr);
    Serial.println("INFO: You need redoot device to applay new config !!!");
    
  } else {Serial.println("ERROR: Bad parametrs"); }; 
}

void cmdSET_IP_GW(Shell &shell, int argc, const ShellArguments &argv)
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
    ip_gw[0]=Parts[0]; ip_gw[1]=Parts[1]; ip_gw[2]=Parts[2]; ip_gw[3]=Parts[3];
    EEPROM_IP_conf_write(IP_EEPROM_addr);
    Serial.println("INFO: You need redoot device to applay new config !!!");
    
  } else {Serial.println("ERROR: Bad parametrs"); }; 
}

void cmdSET_IP_DNS(Shell &shell, int argc, const ShellArguments &argv)
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
    ip_dns[0]=Parts[0]; ip_dns[1]=Parts[1]; ip_dns[2]=Parts[2]; ip_dns[3]=Parts[3];
    EEPROM_IP_conf_write(IP_EEPROM_addr);
    Serial.println("INFO: You need redoot device to applay new config !!!");
    
  } else {Serial.println("ERROR: Bad parametrs"); }; 
}

ShellCommand(set_ip_addr, "- Show interface IP adress ", cmdSET_IP_ADDR);
ShellCommand(set_ip_mask, "- Show interface IP adress ", cmdSET_IP_MASK);
ShellCommand(set_ip_gw, "- Show interface IP adress ", cmdSET_IP_GW);
ShellCommand(set_ip_dns, "- Show interface IP adress ", cmdSET_IP_DNS);
ShellCommand(show_ip, "- Show interface IP adress ", cmdSHOW_IP);


void cmdREBOOT(Shell &shell, int argc, const ShellArguments &argv)
{
  resetFunc();
}

ShellCommand(reboot, "- Software reboot controller", cmdREBOOT);

void cmdTEST(Shell &shell, int argc, const ShellArguments &argv)
{
  Serial.println("******* ALL RELAY OFF TEST PROCESING !!! *********");
  for (byte i=0; i<5; i++){Serial.print("!"); delay(1000);};
  Serial.println("!");

   for (byte i = 0; i < (RM_lines); i++) {
    Serial.print(millis()); Serial.print(": RM: Setup RELAY pin["); Serial.print(RM_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println("OFF");
    digitalWrite(RM_pin[i], HIGH);
    delay(500);
   };// for i

  Serial.println("******* ALL RELAY ON TEST PROCESING !!! *********");
  for (byte i=0; i<5; i++){Serial.print("!"); delay(1000);};
  Serial.println("!");

   for (byte i = 0; i < (RM_lines); i++) {
    Serial.print(millis()); Serial.print(": RM: Setup RELAY pin["); Serial.print(RM_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println("ON");
    digitalWrite(RM_pin[i], LOW);
    delay(500);
   };// for i

   Serial.println("******* RETURN LAST STATE !!! *********");
   for (byte i=0; i<5; i++){Serial.print("!"); delay(1000);};
   Serial.println("!");
   
   for (byte i = 0; i < (RM_lines); i++) {
    Serial.print(millis()); Serial.print(": RM: Setup RELAY pin["); Serial.print(RM_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println(RM_val[i]);
    digitalWrite(RM_pin[i], HIGH);
    RM_set(i,RM_val[i]);
     delay(100);
   };// for i

}

ShellCommand(test, "- Test on-off all relays", cmdTEST);

/**************END SHELL SECTION ********* END SHELL SECTION ***************************************************/



//**************************************************************************************************
//*************************<<< MAIN SECTION >>>*****************************************************
//**************************************************************************************************
void setup() {
  byte i;

  Serial.begin(9600);
  Serial.println("16 Relay Module Controller System");
  Serial.print("Version: ");  Serial.println(CLIENT_VERSION);
  Serial.println("------------------------------------------------------------------");

  
  //You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(53);    // ATMEGA 2560 Core from RobotDyn
  //Ethernet.init(10);  // Most Arduino shields
  //Ethernet.init(5);   // MKR ETH shield
  //Ethernet.init(0);   // Teensy 2.0
  //Ethernet.init(20);  // Teensy++ 2.0
  //Ethernet.init(15);  // ESP8266 with Adafruit Featherwing Ethernet
  //Ethernet.init(33);  // ESP32 with Adafruit Featherwing Ethernet
  
  delay(1000);
  
  EEPROM_IP_conf_read(IP_EEPROM_addr);

  if (ip_addr[0]==0 && ip_addr[1]==0 && ip_addr[2]==0 && ip_addr[3]==0) {DHCP_ENABLE=1;} else {DHCP_ENABLE=0;};


  if (DHCP_ENABLE == 1) {
   Serial.print(millis()); Serial.println(": ETH: DHCP is ENABLED !!!  ");
   Serial.print(millis()); Serial.println(": ETH: Waiting IP by DHCP...");
   //  ---- Setup Ethernet communication using DHCP -------------------------------------------------------------------------------------------
   if (Ethernet.begin(mac) == 0) {
    Serial.println("Unable to configure Ethernet using DHCP !!!");
    
    NET_ERROR_FLAG=1;

    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet shield was not found.  Sorry, system cannot work correctly without equipment. :(");
        NET_ERROR_FLAG=2;
        delay(10000);
    }
    
    //Check for Ethernet Link
    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected.");
        NET_ERROR_FLAG=3;
        delay(10000);
    }
   } // if Ethernet.begin()
  } else {

   Serial.print(millis()); Serial.println(": ETH: Static IP is enabled !");
   Serial.print(millis()); Serial.println(": ETH: IP configuring .......");
   //  ---- Setup Ethernet communication using DHCP -------------------------------------------------------------------------------------------
   Ethernet.begin(mac,ip_addr,ip_dns, ip_gw, ip_mask);
  
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet shield was not found.  Sorry, system cannot work correctly without equipment. :(");
        NET_ERROR_FLAG=2;
        delay(10000);
    }
    
    //Check for Ethernet Link
    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected.");
        NET_ERROR_FLAG=3;
        delay(10000);
    }
  
  }; //if DHCP_ENABLE

  Serial.print(millis()); Serial.print(": ETH: MAC address: "); Serial.println(macToStr(mac));
  Serial.print(millis()); Serial.print(": ETH: IP address : "); Serial.println(Ethernet.localIP());
  Serial.print(millis()); Serial.print(": ETH: SUBNET mask: "); Serial.println(Ethernet.subnetMask());
  Serial.print(millis()); Serial.print(": ETH: Gateway    : "); Serial.println(Ethernet.gatewayIP());
  Serial.print(millis()); Serial.print(": ETH: DNS server : "); Serial.println(Ethernet.dnsServerIP());
 
  //for (byte i=0; i<5; i++){Serial.print("!"); delay(1000);};
  //Serial.println("!");
  delay(1000);
  
// RM SETUP part ----------------------------------------------------------------------------------------------------------

  RM_setup(); // SET UP PWM
     
  delay(1000);

// Start NET services --------------------------------------------------------------------------------------------------------
   
  // MQTT client setup -------------------
  EEPROM_MQTT_conf_read(MQTT_EEPROM_addr);
  mqttClient.setClient(ethClient);
  mqttClient.setServer(mqtt_ip, mqtt_port);
  mqttClient.setCallback(mqtt_callback);
  Serial.print(millis()); Serial.print(": MQTT: Client start connect to "); Serial.print(mqttIpToStr());Serial.print(":");Serial.println(mqtt_port);
 
  MQTT_chk_conect();
 
  Serial.println("");

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
    if (DHCP_ENABLE == 1) {
      Serial.print(CIRCLE_NOW_2); Serial.print(": ");
      Serial.print("DHCP Requesting ... "); 
      Serial.print(Ethernet.maintain());
      Serial.print(" New IP:");  
      Serial.println(Ethernet.localIP());
    }; // DHCP_ENABLED
   }  
  //==================================================================================================== 

  // =========== CIRCLE 3 =============================================================================== 
  // Refresh LCD   
   CIRCLE_NOW_3 = millis(); 
  
   if (CIRCLE_NOW_3 - CIRCLE_LASTMSG_3 > (CIRCLE_TIMER_3*1000)) { 
    CIRCLE_LASTMSG_3 = CIRCLE_NOW_3;
    if ( DEBUG_LEVEL > 0 ) {Serial.print(CIRCLE_NOW_3); Serial.print(": "); Serial.println("LCD Refreshing ... "); };
    //refresh_LCD_main();
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
void RM_setup()
{
      
 
  
  EEPROM_RM_val_read_all(RM_EEPROM_addr);
  
  for (byte i = 0; i < (RM_lines); i++) {
    Serial.print(millis()); Serial.print(": RM: Setup RELAY pin["); Serial.print(RM_pin[i]); Serial.print("] > L"); Serial.print(i+1); Serial.print(" = "); Serial.println(RM_val[i]);
    

    pinMode(RM_pin[i], OUTPUT);
    digitalWrite(RM_pin[i], HIGH);
    RM_set(i,RM_val[i]);
    
    delay(500);
  
  };// for i

  
} //RM_setup()

/************************************************************************
 *  RELAY SET LINE line_num value line_val
 ***********************************************************************/
void RM_set(int line_num, int line_val ){

   RM_val[line_num]=line_val; 
   EEPROM_RM_val_write(RM_EEPROM_addr,line_num);
   if (RM_val[line_num] == 0) {digitalWrite(RM_pin[line_num], HIGH);} 
   else {digitalWrite(RM_pin[line_num], LOW);};
}


//*******************************EEPROM WORKING ***********************************************************************************
/************************************************************************
 *  Чтение значений RELAY из памяти
 ***********************************************************************/
void EEPROM_RM_val_read_all(int addr) {    
 
  for(byte i = 0; i < RM_lines; i++) {
    RM_val[i] = EEPROM.read(addr+i);
    Serial.print(millis()); Serial.print(": EEPROM: Read RELAY line["); Serial.print(i+1); Serial.print("] value = "); Serial.println(RM_val[i]);
  }
  
} // EEPROM_RM_val_read(int addr)


/************************************************************************
 *  Запись значений RELAY в память
 ***********************************************************************/
void EEPROM_RM_val_write_all(int addr) {

  for(byte i = 0; i < RM_lines; i++) {
    EEPROM.write(addr+i, RM_val[i]);
    Serial.print(millis()); Serial.print(": EEPROM: Write RELAY line["); Serial.print(i+1); Serial.print("] value = "); Serial.println(RM_val[i]);
  }
  
}//EEPROM_RM_val_write(int addr)

/************************************************************************
 *  Запись значения RELAY в память
 ***********************************************************************/
void EEPROM_RM_val_write(int addr, int line_num) {

    if (RM_val[line_num] != EEPROM.read(addr+line_num)){
     EEPROM.write(addr+line_num, RM_val[line_num]);
     Serial.print(millis()); Serial.print(": EEPROM: Write RELAY line["); Serial.print(line_num+1); Serial.print("] value = "); Serial.println(RM_val[line_num]);
    }
  
}//EEPROM_RM_val_write

/************************************************************************
 *  Чтение значения RELAY из памяти
 ***********************************************************************/
byte EEPROM_RM_val_read(int addr, int line_num) {
  byte r;
    
     
     r = EEPROM.read(addr+line_num);
     Serial.print(millis()); Serial.print(": EEPROM: Read RELAY line["); Serial.print(line_num+1); Serial.print("] value = "); Serial.println(r);
    
  return r;
  
}//EEPROM_RM_val_read

/************************************************************************
 *  MQTT Chk Connetc to Server
 ***********************************************************************/
int MQTT_chk_conect() {
  char msgParam[128];
  
  if (!mqttClient.connected()){
    Serial.print(millis()); Serial.print(": ");
    Serial.println("MQTT: Client not connected! Reconnect ....");
    mqttClient.connect(mqtt_id);
    if (mqttClient.connected()){
       Serial.print(millis()); Serial.println(": MQTT: Connection OK!");
      
    for(byte i = 0; i < RM_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"relay",i+1);
      mqttClient.subscribe(msgParam);
      Serial.print(millis()); Serial.print(": "); Serial.print("MQTT: Subscribe on "); Serial.println(msgParam);
    }//for
   }; //mqttClient.connected()
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
  
  
 
  
  //MQTT_chk_conect();
     
  if ( MQTT_chk_conect() == 1 ){
    Serial.print(upTimeMS); Serial.print(": ");
    Serial.print("MQTT: Sending DATA -> MQTT Server; Uptime: "); Serial.println(upTime);
    
    
    
    
    sprintf(msgParam,"%s/%s",mqtt_id,"version"); mqttClient.publish(msgParam, CLIENT_VERSION);
    sprintf(msgParam,"%s/%s",mqtt_id,"mac");     mqttClient.publish(msgParam, macToStr(mac).c_str());
    sprintf(msgParam,"%s/%s",mqtt_id,"ip");      mqttClient.publish(msgParam, MyIpToStr().c_str());
    sprintf(msgParam,"%s/%s",mqtt_id,"uptime");  mqttClient.publish(msgParam, deblank(dtostrf(upTime, 6, 0, msgBuffer)));

    for(byte i = 0; i < RM_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"relay",i+1); 
      //p_val = map(RM_val[i], 0, 255, 0, 100);
      sprintf(msgVal,"%d",RM_val[i]); 
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
  for(byte i = 0; i < RM_lines; i++) {
      sprintf(msgParam,"%s/%s%02d",mqtt_id,"relay",i+1);

      if ( String(msgParam) == strTopic ) {
         p_val = strPayload.toInt();
         //p_val = map(p_val, 0, 100, 0, 255);
         Serial.print(millis()); Serial.print(": RM: Line "); Serial.print(i+1); Serial.print(" set to "); Serial.println(p_val);
         RM_set(i,p_val);
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
 *  IP to String
 ***********************************************************************/
String ipToStr(uint8_t* u_ip)
{
String s_ip="";
for (byte i = 0; i < 4; i++) {  
  s_ip = s_ip + String (u_ip[i]); 
  if (i<3) { s_ip = s_ip + "."; }
  };
return s_ip;
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


/************************************************************************
 *  Запись IP конфиг в память
 ***********************************************************************/
void EEPROM_IP_conf_write(int addr) {

     EEPROM.put(addr, ip_addr);
     EEPROM.put(addr+4, ip_mask);
     EEPROM.put(addr+8, ip_gw);
     EEPROM.put(addr+12, ip_dns);
     
     Serial.print(millis()); Serial.println(": EEPROM: Write IP config  ");
     Serial.print(millis()); Serial.print(": EEPROM: IP  : "); Serial.println(ipToStr(ip_addr)); 
     Serial.print(millis()); Serial.print(": EEPROM: MASK: "); Serial.println(ipToStr(ip_mask)); 
     Serial.print(millis()); Serial.print(": EEPROM: GW  : "); Serial.println(ipToStr(ip_gw));
     Serial.print(millis()); Serial.print(": EEPROM: DNS : "); Serial.println(ipToStr(ip_dns));
     
}//EEPROM_IP_conf_write

/************************************************************************
 *  Чтение IP конфига из флаш памяти
 ***********************************************************************/
void EEPROM_IP_conf_read(int addr) {
  
     EEPROM.get(addr, ip_addr);
     EEPROM.get(addr+4, ip_mask);
     EEPROM.get(addr+8, ip_gw);
     EEPROM.get(addr+12, ip_dns);
      
     Serial.print(millis()); Serial.println(": EEPROM: Read IP config  ");
     Serial.print(millis()); Serial.print(": EEPROM: IP  : "); Serial.println(ipToStr(ip_addr)); 
     Serial.print(millis()); Serial.print(": EEPROM: MASK: "); Serial.println(ipToStr(ip_mask)); 
     Serial.print(millis()); Serial.print(": EEPROM: GW  : "); Serial.println(ipToStr(ip_gw));
     Serial.print(millis()); Serial.print(": EEPROM: DNS : "); Serial.println(ipToStr(ip_dns));

}//EEPROM_IP_conf_read
