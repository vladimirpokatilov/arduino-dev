/***************************************************************************
 * 3 phaze AC meter for Arduino MEGA2560 
 * Modules: PZEM-04, ENC28j60, LCD_20x4_I2C
 * 
 * Hard Serial Vertion
 * Version: 5.0
 * 
 * v5.1 - delete DHCP, set static IP
 * v5.0 - delete WWW,NTP,DNS for stable !!!
 * v4.9 - change MQTT, use mqttClient.conected()
 * v4.8 - change LCD display to kWh
 * v4.7 - dhcp renew
 * v4.6 - web server optimization
 * v4.5 - log format updated
 * v4.4 - web server algoritm updated
 * v4.3 - new algoritm PZEM-04 module check
 * v4.2 - led bright
 * v4.1 - add table and autorefresh to web server
 * v4.0 - add web server
 * v3.0 - add ntp in menu 4
 ***************************************************************************/
#define ServerDEBUG

#include <SoftwareSerial.h> // Arduino IDE <1.6.6
#include <PZEM004T.h>

//PZEM004T pzem3(14,15);  // Soft serial 6,7  2,3 (RX,TX) connect to TX,RX of PZEM

PZEM004T pzem1(&Serial1);  // D19, D18 (RXD1, TXD1) connect to TX,RX of PZEM
PZEM004T pzem2(&Serial2);  // D17, D16 (RXD2, TXD2) connect to TX,RX of PZEM
PZEM004T pzem3(&Serial3);  // D15, D14 (RXD3, TXD3) connect to TX,RX of PZEM
IPAddress ip(192,168,1,1);

float v1,v2,v3,v4;
float i1,i2,i3,i4;
float p1,p2,p3,p4,pK;
float e1,e2,e3,e4,eK;

// LCD 20x4 Display --------------------------------------------------------

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Ethernet and MQTT describe ----------------------------------------------
//#define ENC28J60_CONTROL_CS 8
//#define SPI_MOSI 11
//#define SPI_MISO 12
//#define SPI_SCK 13
#include <UIPEthernet.h>
#include "PubSubClient.h"

#define CLIENT_ID  "amega-01"
#define CLIENT_VERSION "PZEM04_UIPE_MQTT_5.1"

String my_ip = "";
String MAC = "";
uint8_t mac[6] = {0xF4, 0x16, 0x3E, 0x12, 0xC8, 0x90}; // MAC for SET F4-16-3E-12-C8-90

byte ip_addr[] = { 192, 168, 17, 90 };
byte gateway[] = { 192, 168, 17, 1 };
byte subnet[] = { 255, 255, 255, 0 };

char mqtt_ip[] = "192.168.17.60";
int mqtt_port = 1883;

EthernetClient ethClient;
PubSubClient mqttClient;

byte ethStatus = 0;


// ***************  Global SETs ****************************************************** 
long CIRCLE_TIME_1 = 60; // sleep time for rescan power module in seconds
long CIRCLE_TIME_2 = 300; // dhcp request in seconds
//long CIRCLE_TIME_3 = 62; // sleep time for rescan power module in seconds
//long CIRCLE_TIME_4 = 62; // sleep time for rescan power module in seconds
long CIRCLE_TIME_5 = 100; // sleep time for led in ms
int CIRCLE_STATE = 0;

long circle_lastmsg_1 = 0; // power read circle counter
long circle_lastmsg_2 = 0; // dhcp read circle counter
//long circle_lastmsg_3 = 0; // power read circle counter
//long circle_lastmsg_4 = 0; // power read circle counter
long circle_lastmsg_5 = 0; // led and buttons circle counter

long  upTime = 0; // Uptime counter

// LED GPIO pin 
int led = 13;
int led_now = 0;    // how bright the LED is
int led_last = 0;    // how many points to fade the LED by
int led_bright = 5; // led bright
int led_lcd =12;
int led_lcd_bright = 250;


// Buttons 
#define BUTTON_PIN 10       // кнопка подключена сюда (PIN --- КНОПКА --- GND)
#include "GyverButton.h"
GButton butt1(BUTTON_PIN);

// LCD MENU mode --------------
int menu_mode = 0; // LCD MENU mode
int menu_modes = 3; // Количество пунтков меню



/* ************** SETUP ******************************       */
void setup() {
  byte i;
  pinMode(led, OUTPUT);
  Serial.begin(9600);
  pzem1.setAddress(ip);
  pzem2.setAddress(ip);
  pzem3.setAddress(ip);

  // Button SETUP
  butt1.setDebounce(200);        // настройка антидребезга (по умолчанию 80 мс)
  butt1.setTimeout(1000);        // настройка таймаута на удержание (по умолчанию 500 мс)
  butt1.setIncrStep(1);         // настройка инкремента, может быть отрицательным (по умолчанию 1)
  butt1.setIncrTimeout(1000);    // настрйока интервала инкремента (по умолчанию 800 мс)
  
  // initialize the LCD
  analogWrite(led_lcd, led_lcd_bright);
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.home();

  Serial.println("Starting MEGA2560 POWER METER");
  Serial.print("Version: ");  Serial.println(CLIENT_VERSION);
  Serial.print("CIENT_ID: ");  Serial.println(CLIENT_ID);
  
  lcd.setCursor(0,0);  lcd.print("MEGA2560 POWER METER"); 

  // setup ethernet communication
  //--- HEX MAC address to string with ":" 
  for (i = 0; i < 6; i++) {  if ((mac[i]) <= 0x0F) {  MAC = MAC + "0";} MAC = MAC + String ((mac[i]), HEX); if (i<5) { MAC = MAC + "-"; };}
  Serial.print(millis()); Serial.print(": MAC address: "); Serial.println(MAC);
  lcd.setCursor(0,1); lcd.print(MAC);
  
  Ethernet.begin(mac, ip_addr, gateway, subnet);
  

  // --- ip to string 
  for (i = 0; i < 4; i++) {  my_ip = my_ip + String (Ethernet.localIP()[i]); if (i<3) { my_ip = my_ip + "."; }
  }
    
  Serial.print(millis()); Serial.println(F(": Ethernet configured"));
  Serial.print(millis()); Serial.print(": IP address: "); Serial.println(Ethernet.localIP());
  lcd.setCursor(0,2); lcd.print("IP:"); lcd.print(Ethernet.localIP()); 

  

  // setup mqtt client
  mqttClient.setClient(ethClient);
  mqttClient.setServer(mqtt_ip, mqtt_port);
  Serial.print(millis()); Serial.println(F(": MQTT: client configured"));
  lcd.setCursor(0,3); lcd.print("MQTT:"); lcd.print(mqtt_ip);

}

void loop() {
  long circle_now_1 = millis();
  long circle_now_2 = millis();
  long circle_now_5 = millis();
   
  //check_web_connections(); // web server work
  
  mqttClient.loop();  // MQTT lisen replay from server 
  
  butt1.tick();  // обязательная функция отработки. Должна постоянно опрашиваться 
  if (butt1.isSingle()) { 
    Serial.print(circle_now_5); Serial.print(": ");
    Serial.println("Button1: Single click");
    menu_mode++; 
    led_lcd_bright=250;
    analogWrite(led_lcd, led_lcd_bright);
    Serial.print(millis()); Serial.print(": Led bright = "); Serial.println(led_lcd_bright);
    lcd_menu(); 
  }

// START CHECK POWER MODULES    
   circle_now_1 = millis(); 
// 1 Phaze  ----------------------------------------------------
   if ((circle_now_1 - circle_lastmsg_1 > (CIRCLE_TIME_1+0)*1000)&&(CIRCLE_STATE == 0)) {  
   Serial.print(circle_now_1); Serial.print(": ");
   power_check(1);
   
   CIRCLE_STATE = 1; 
   Serial.print(circle_now_1); Serial.print(": ");
   Serial.print("CIRCLE_STATE = "); Serial.println(CIRCLE_STATE);
  }

// 2 Phaze  ----------------------------------------------------
   if ((circle_now_1 - circle_lastmsg_1 > (CIRCLE_TIME_1+3)*1000)&&(CIRCLE_STATE == 1)) {  
   Serial.print(circle_now_1); Serial.print(": ");
   power_check(2);
   
   CIRCLE_STATE = 2; 
   Serial.print(circle_now_1); Serial.print(": ");
   Serial.print("CIRCLE_STATE = "); Serial.println(CIRCLE_STATE);
  }

// 3 Phaze  ----------------------------------------------------
   if ((circle_now_1 - circle_lastmsg_1 > (CIRCLE_TIME_1+6)*1000)&&(CIRCLE_STATE == 2)) {  
   Serial.print(circle_now_1); Serial.print(": ");
   power_check(3);
   
   CIRCLE_STATE = 3; 
   Serial.print(circle_now_1); Serial.print(": ");
   Serial.print("CIRCLE_STATE = "); Serial.println(CIRCLE_STATE);
  }

// 4 Phaze Summary  ----------------------------------------------------
   if ((circle_now_1 - circle_lastmsg_1 > (CIRCLE_TIME_1+9)*1000)&&(CIRCLE_STATE == 3)) {  
   Serial.print(circle_now_1); Serial.print(": ");
   power_check(4);
   // Send DATA 2 MQTT Server
   sendMQTTData();

   CIRCLE_STATE = 0; 
   Serial.print(circle_now_1); Serial.print(": ");
   Serial.print("CIRCLE_STATE = "); Serial.println(CIRCLE_STATE);
   
   if ( led_lcd_bright > 0 ){led_lcd_bright=led_lcd_bright-50; analogWrite(led_lcd, led_lcd_bright);}
   Serial.print(millis()); Serial.print(": Led bright = "); Serial.println(led_lcd_bright);
   // Renew Power Check circle ---------------------------
   circle_lastmsg_1 = circle_now_1; 
  }

// CHECK DHCP renew  
/*  
   circle_now_2 = millis(); 
// DHCP  ----------------------------------------------------

   if (circle_now_2 - circle_lastmsg_2 > (CIRCLE_TIME_2*1000)) { 
    circle_lastmsg_2 = circle_now_2;
    Serial.print(circle_now_2); Serial.print(": ");
    Serial.print("DHCP Requesting ... "); 
    if (ethStatus == 1){
     Serial.print(Ethernet.maintain());
     Serial.print(" New IP:");  
     Serial.println(Ethernet.localIP());
     if (Ethernet.localIP()[0] == 0) {
       if (Ethernet.begin(mac) == 0) {
          Serial.println(F("Unable to configure Ethernet using DHCP"));
        //for (;;);
        ethStatus = 0;
       } else { ethStatus = 1; };
     }
     
    } else {
      // setup ethernet communication using DHCP
      if (Ethernet.begin(mac) == 0) {
         Serial.println(F("Unable to configure Ethernet using DHCP"));
       //for (;;);
       ethStatus = 0;
      } else { ethStatus = 1; };
    }
   }  
*/  
// 5 circle ---------------------------------------------------
  circle_now_5 = millis();
  if (circle_now_5 - circle_lastmsg_5 > CIRCLE_TIME_5) {
    circle_lastmsg_5 = circle_now_5;

    if (led_last == 0 ) led_now =1;
    if (led_last == 1 ) led_now =0;
    //digitalWrite(led, led_now);
    analogWrite(led, led_now*led_bright);
    led_last = led_now;
  }
  
}

/************************************************************************
 *  Check POWER MODULES
 ***********************************************************************/
void power_check(int nf) {
  digitalWrite(led, 0);
  
  char lcd_buf[20]; // Массив для вывода
    
  // START CHECK POWER MODULES
  switch (nf) {
    case 1:
  //  --------------- FAZA 1 -----------------------------------
  v1 = pzem1.voltage(ip);
  if (v1 < 0.0) v1 = 0.0;
  Serial.print("F1 "); Serial.print(v1);Serial.print("V; ");
  lcd.setCursor(0, 0); lcd.print("                    ");
  lcd.setCursor(0, 0); lcd.print(int(v1));lcd.print(" ");

  i1 = pzem1.current(ip);
  if(i1 >= 0.0){ Serial.print(i1);Serial.print("A; "); lcd.setCursor(4, 0);
  if(i1 < 10.0) {sprintf(lcd_buf,"%1d.%1d", (int)i1, (int)(i1*10)%10);} else {sprintf(lcd_buf,"%3d", (int)i1);}
  lcd.print(lcd_buf);
  }
  
  p1 = pzem1.power(ip);
  if(p1 >= 0.0){ Serial.print(p1);Serial.print("W; "); lcd.setCursor(8, 0); dtostrf(p1, 5, 0, lcd_buf); ;lcd.print(lcd_buf); }
  
  e1 = pzem1.energy(ip);
  if(e1 >= 0.0){ Serial.print(e1);Serial.print("Wh; "); lcd.setCursor(14, 0); 
   if(e1 < 1000.0) {sprintf(lcd_buf,"%3d.%2d", (int)e1/1000, (int)((e1/1000)*100)%100);} else {sprintf(lcd_buf,"%6d", (int)(e1/1000));}
   lcd.print(lcd_buf);
  }
  Serial.println();
   break;
  case 2:
// FAZA 2----------------------------------------------------------  
  v2 = pzem2.voltage(ip);
  if (v2 < 0.0) v2 = 0.0;
  Serial.print("F2 "); Serial.print(v2); Serial.print("V; ");
  lcd.setCursor(0, 1); lcd.print("                    ");
  lcd.setCursor(0, 1); lcd.print(int(v2));lcd.print(" ");

  i2 = pzem2.current(ip);
  if(i2 >= 0.0){ Serial.print(i2);Serial.print("A; "); lcd.setCursor(4, 1);
  if(i2 < 10.0) {sprintf(lcd_buf,"%1d.%1d", (int)i2, (int)(i2*10)%10);} else {sprintf(lcd_buf,"%3d", (int)i2);}
  lcd.print(lcd_buf);
  }
  
  p2 = pzem2.power(ip);
  if(p2 >= 0.0){ Serial.print(p2);Serial.print("W; "); lcd.setCursor(8, 1); dtostrf(p2, 5, 0, lcd_buf); lcd.print(lcd_buf);}
  
  e2 = pzem2.energy(ip);
  //if(e2 >= 0.0){ Serial.print(e2);Serial.print("Wh; "); lcd.setCursor(14, 1); dtostrf(e2, 6, 0, lcd_buf); lcd.print(lcd_buf);}
  if(e2 >= 0.0){ Serial.print(e2);Serial.print("Wh; "); lcd.setCursor(14, 1); 
   if(e2 < 1000.0) {sprintf(lcd_buf,"%3d.%2d", (int)e2/1000, (int)((e2/1000)*100)%100);} else {sprintf(lcd_buf,"%6d", (int)(e2/1000));}
   lcd.print(lcd_buf);
  }
  
  Serial.println();
   break;
  case 3:
// FAZA 3----------------------------------------------------------  
  v3 = pzem3.voltage(ip);
  if (v3 < 0.0) v3 = 0.0;
  Serial.print("F3 "); Serial.print(v3); Serial.print("V; ");
  lcd.setCursor(0, 2); lcd.print("                    ");
  lcd.setCursor(0, 2); lcd.print(int(v3));lcd.print(" ");

  i3 = pzem3.current(ip);
  if(i3 >= 0.0){ Serial.print(i3);Serial.print("A; "); lcd.setCursor(4, 2); 
  if(i3 < 10.0) {sprintf(lcd_buf,"%1d.%1d", (int)i3, (int)(i3*10)%10);} else {sprintf(lcd_buf,"%3d", (int)i3);}
  lcd.print(lcd_buf);
  }
  
  p3 = pzem3.power(ip);
  if(p3 >= 0.0){ Serial.print(p3);Serial.print("W; "); lcd.setCursor(8, 2); dtostrf(p3, 5, 0, lcd_buf); lcd.print(lcd_buf);}
  
  e3 = pzem3.energy(ip);
  //if(e3 >= 0.0){ Serial.print(e3);Serial.print("Wh; "); lcd.setCursor(14, 2); dtostrf(e3, 6, 0, lcd_buf);lcd.print(lcd_buf);}
  if(e3 >= 0.0){ Serial.print(e3);Serial.print("Wh; "); lcd.setCursor(14, 2); 
   if(e3 < 1000.0) {sprintf(lcd_buf,"%3d.%2d", (int)e3/1000, (int)((e3/1000)*100)%100);} else {sprintf(lcd_buf,"%6d", (int)(e3/1000));}
   lcd.print(lcd_buf);
  }


  Serial.println();
   break;
  case 4:
// TOTAL
  v4 = (v1+v2+v3)/3;
  i4 = i1+i2+i3;
  p4 = p1+p2+p3;
  e4 = e1+e2+e3;
  pK = p4/1000;
  eK = e4/1000;
  
  Serial.print("TOTAL "); Serial.print(v4); Serial.print("V; ");
  lcd.setCursor(0, 3); lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print(""); 
  
  
  Serial.print(i4);Serial.print("A; "); lcd.setCursor(4, 3); dtostrf(round(i4), 3, 0, lcd_buf); lcd.print(lcd_buf);
  Serial.print(p4);Serial.print("W; "); lcd.setCursor(8, 3); dtostrf(p4, 5, 0, lcd_buf); lcd.print(lcd_buf);
  Serial.print(e4);Serial.print("Wh; "); lcd.setCursor(14, 3); 
    if(e4 < 1000.0) {sprintf(lcd_buf,"%3d.%2d", (int)e4/1000, (int)((e4/1000)*100)%100);} else {sprintf(lcd_buf,"%6d", (int)(e4/1000));}
  lcd.print(lcd_buf);



  Serial.println();
  default:
      // if nothing else matches, do the default
      // default is optional
  break;
  }
} 

/************************************************************************
 *  Send Data 2 MQTT Server
 ***********************************************************************/
void sendMQTTData() {

  char msgBuffer[20];//was 20
  char msgParam[64];//was 20
  
  long upTimeMS = millis();
  upTime = upTimeMS / 1000;
  
  lcd.setCursor(0, 3); lcd.print("D>S");
  
  if (!mqttClient.connected()){
    Serial.print(upTimeMS); Serial.print(": ");
    Serial.println("MQTT: Client not connected! Reconnect ....");
   
    mqttClient.connect(CLIENT_ID);
  
  };
     
  if (mqttClient.connected()){
    Serial.print(upTimeMS); Serial.print(": ");
    Serial.print("MQTT: Sending DATA -> MQTT Server; Uptime: "); Serial.println(upTime);
    lcd.setCursor(0, 3);lcd.print("MQTT");
    
    sprintf(msgParam,"%s/%s",CLIENT_ID,"version"); mqttClient.publish(msgParam, CLIENT_VERSION);
    sprintf(msgParam,"%s/%s",CLIENT_ID,"mac");     mqttClient.publish(msgParam, MAC.c_str());
    sprintf(msgParam,"%s/%s",CLIENT_ID,"ip");      mqttClient.publish(msgParam, my_ip.c_str());
    sprintf(msgParam,"%s/%s",CLIENT_ID,"uptime");  mqttClient.publish(msgParam, deblank(dtostrf(upTime, 6, 0, msgBuffer)));
    
    sprintf(msgParam,"%s/%s",CLIENT_ID,"v1"); Serial.print(msgParam); Serial.print(" "); Serial.println(v1);
    mqttClient.publish( msgParam, deblank(dtostrf(v1, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"i1"); Serial.print(msgParam); Serial.print(" "); Serial.println(i1);
    mqttClient.publish(msgParam, deblank(dtostrf(i1, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"p1"); Serial.print(msgParam); Serial.print(" "); Serial.println(p1);
    mqttClient.publish(msgParam, deblank(dtostrf(p1, 5, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"e1"); Serial.print(msgParam); Serial.print(" "); Serial.println(e1);
    mqttClient.publish(msgParam, deblank(dtostrf(e1, 6, 0, msgBuffer)));

    sprintf(msgParam,"%s/%s",CLIENT_ID,"v2"); Serial.print(msgParam); Serial.print(" "); Serial.println(v2);
    mqttClient.publish( msgParam, deblank(dtostrf(v2, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"i2"); Serial.print(msgParam); Serial.print(" "); Serial.println(i2);
    mqttClient.publish(msgParam, deblank(dtostrf(i2, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"p2"); Serial.print(msgParam); Serial.print(" "); Serial.println(p2);
    mqttClient.publish(msgParam, deblank(dtostrf(p2, 5, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"e2"); Serial.print(msgParam); Serial.print(" "); Serial.println(e2);
    mqttClient.publish(msgParam, deblank(dtostrf(e2, 6, 0, msgBuffer)));

    sprintf(msgParam,"%s/%s",CLIENT_ID,"v3"); Serial.print(msgParam); Serial.print(" "); Serial.println(v3);
    mqttClient.publish( msgParam, deblank(dtostrf(v3, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"i3"); Serial.print(msgParam); Serial.print(" "); Serial.println(i3);
    mqttClient.publish(msgParam, deblank(dtostrf(i3, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"p3"); Serial.print(msgParam); Serial.print(" "); Serial.println(p3);
    mqttClient.publish(msgParam, deblank(dtostrf(p3, 5, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"e3"); Serial.print(msgParam); Serial.print(" "); Serial.println(e3);
    mqttClient.publish(msgParam, deblank(dtostrf(e3, 6, 0, msgBuffer)));

    sprintf(msgParam,"%s/%s",CLIENT_ID,"v4"); Serial.print(msgParam); Serial.print(" "); Serial.println(v4);
    mqttClient.publish( msgParam, deblank(dtostrf(v4, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"i4"); Serial.print(msgParam); Serial.print(" "); Serial.println(i4);
    mqttClient.publish(msgParam, deblank(dtostrf(i4, 3, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"p4"); Serial.print(msgParam); Serial.print(" "); Serial.println(p4);
    mqttClient.publish(msgParam, deblank(dtostrf(p4, 5, 1, msgBuffer)));
    sprintf(msgParam,"%s/%s",CLIENT_ID,"e4"); Serial.print(msgParam); Serial.print(" "); Serial.println(e4);
    mqttClient.publish(msgParam, deblank(dtostrf(e4, 6, 0, msgBuffer)));

    sprintf(msgParam,"%s/%s",CLIENT_ID,"value"); Serial.print(msgParam); Serial.print(" "); Serial.println(e4);
    mqttClient.publish(msgParam, deblank(dtostrf(e4, 6, 0, msgBuffer)));

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
 *  Print MENU on LCD
 ***********************************************************************/
void lcd_menu () {
  int i = 0;
  upTime = millis() / 1000;
  // menu_mode++;
  if (menu_mode > menu_modes ) menu_mode = 1;
  Serial.print(millis()); Serial.print(": MENU [");Serial.print(menu_mode);Serial.println("]");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("["); lcd.print(menu_mode);lcd.print("] ");

   if (menu_mode == 1) {
    lcd.print("SYSTEM");
    lcd.setCursor(0,3); lcd.print("UpTime:"); lcd.print(upTime); lcd.print("s"); 
    lcd.setCursor(0,2); lcd.print(CLIENT_VERSION);
    };

  if (menu_mode == 2) {
    lcd.print("NETWORK SET");
    lcd.setCursor(0,2); lcd.print("IP:"); lcd.print(Ethernet.localIP()); 
    lcd.setCursor(0,3); lcd.print(MAC);
    };

  if (menu_mode == 3) {
    lcd.print("MQTT SERVER");
    lcd.setCursor(0,2); lcd.print("IP:");lcd.print(mqtt_ip);
    lcd.setCursor(0,3); lcd.print("PORT:"); lcd.print(mqtt_port); 
    };

  
    
}  


