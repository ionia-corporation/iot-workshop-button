#include <EEPROM.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>

extern "C" {
  #include "user_interface.h"
}

/*************************** GPIO Setup **************************************/

#define LED_PIN    13
#define BUTTON_PIN 14

enum {
  ERR_OK                      = 0,
  ERR_WIFI_CONNECT_FAILED     = 1,
  ERR_MQTT_CONNECTION_FAILED  = 2,
  ERR_MQTT_PUBLISH_FAILED     = 3,
  ERR_MQTT_UNEXPECTED_DISC    = 4,
  ERR_SERVER_ISSUE            = 5
};

/*************************** Wifi Setup **************************************/

// Acces Point
const char *ap_ssid = "PaulsAccessPoint";
const char *ap_pass = "lols";
ESP8266WebServer server(80);
// Client
char ssid[100];
char pass[100];
#define WIFI_SSID_ADDR    0
#define WIFI_PASS_ADDR    100

/*************************** MQTT Server *************************************/

#define MQTT_SERVER_ADDR  "m11.cloudmqtt.com"
#define MQTT_SERVERPORT   15848
#define MQTT_USERNAME_S   "jehezims"
#define MQTT_KEY          "Z1YgWLhMin_c"

/************************** Global State ************************************/

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;

// Store the MQTT server, client ID, username, and password in flash memory.
// This is required for using the Adafruit MQTT library.
const char MQTT_SERVER[] PROGMEM    = MQTT_SERVER_ADDR;
const char MQTT_CLIENTID[] PROGMEM  = MQTT_KEY;
const char MQTT_USERNAME[] PROGMEM  = MQTT_USERNAME_S;
const char MQTT_PASSWORD[] PROGMEM  = MQTT_KEY;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD);

/****************************** Feeds ***************************************/

const char TEST_FEED[] PROGMEM = "/testing";
Adafruit_MQTT_Publish testFeed = Adafruit_MQTT_Publish(&mqtt, TEST_FEED);

/*
// Setup a feed called 'onoff' for subscribing to changes.
const char ONOFF_FEED[] PROGMEM = AIO_USERNAME "/feeds/onoff";
Adafruit_MQTT_Subscribe onoffbutton = Adafruit_MQTT_Subscribe(&mqtt, ONOFF_FEED);
*/

/***************************** Program **************************************/
uint32_t sendPayload=0;

/* Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it. */
void setup() {
  //Set up the GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  // TODO: test if this initial delay is necessary
  delay(5000);
  Serial.begin(115200);
  EEPROM.begin(4096);

  //@TODO: This line is just a test. Remove it. Maybe use the Chip ID to uniquely identify the AP?
  Serial.println(ESP.getChipId());

  // test to see if we can connect with values from EEPROM
  bool gotValidCredentials = 1;
  Serial.println("trying to read WiFi creds from EEPROM");
  //gotValidCredentials  = read_eeprom_string(WIFI_SSID_ADDR, ssid, 100);
  //gotValidCredentials &= read_eeprom_string(WIFI_PASS_ADDR, pass, 100);
  EEPROM.get(WIFI_SSID_ADDR, ssid);
  EEPROM.get(WIFI_PASS_ADDR, pass);

  /*
  Serial.println("<Debugging>");
  for(int i=0; i<100; i++)
  {
    Serial.print(i);
    Serial.println(ssid[i]);
    if(ssid[i] == '\0') break;
  }
  Serial.println("</Debugging>");
  */

  if(gotValidCredentials){
    Serial.println("trying to use creds from EEPROM");
    Serial.print("ssid: [");
    Serial.print(ssid);
    Serial.println("]");
    Serial.print("pass: [");
    Serial.print(pass);
    Serial.println("]");
    if(tryWifiConnect()){
      Serial.println("connect succeeded!");
      sendPayload = 1;
    }else{
      Serial.println("connect failed :(");
      play_led_sequence(ERR_WIFI_CONNECT_FAILED);
      setupAccessPoint();
    }
  } else {
    Serial.println("no creds in EEPROM (yet) ");
    setupAccessPoint();
  }
  
}

void loop() {
  //JC's
  //@TODO: Check the API for "WiFi mode (client/AP/...) and check for that instead of WiFi.status()
  if(WiFi.status() == WL_CONNECTED){ //If we're in client mode and connected to a WiFi...
    MQTT_connect();
    if(! testFeed.publish(sendPayload++)){
      Serial.println("PUBLISH FAILED");
    } else {
      Serial.println("MQTT message successfully published");
    }
    // TODO: make this a deep sleep instead (note you will have to store and retrieve Wifi creds!!!!)
    delay(5000);

    //system_deep_sleep_set_option(0);
    //system_deep_sleep(5000000);            // deep sleep for 5 seconds
    ////ESP.deepsleep(5000, WAKE_RFCAL);
  }else{ //If we're in AP mode or simply not connected
    server.handleClient();
  }

  // Paul's original logic workflow
  /*
  if(sendPayload > 0){
    MQTT_connect();
    if(! testFeed.publish(sendPayload++)){
      Serial.println("PUBLISH FAILED!");
    } else {
      Serial.println("HOLY CRAP IT WORKED!");
    }
    // TODO: make this a deep sleep instead (note you will have to store and retrieve Wifi creds!!!!)
    delay(5000);
    //system_deep_sleep_set_option(0);
    //system_deep_sleep(5000000);            // deep sleep for 5 seconds    
    ////ESP.deepsleep(5000, WAKE_RFCAL);
  }else{
    server.handleClient();
  }
  */
}

#define SLOW_BLINK_DELAY 300
#define FAST_BLINK_DELAY 100

inline void led_seq_for(uint32_t loops, uint32_t delay_len) {
  for(int i=0; i<loops; i++) {
    delay(delay_len);
    digitalWrite(LED_PIN, HIGH);
    delay(delay_len);
    digitalWrite(LED_PIN, LOW);
  }
}

void play_led_sequence(uint16_t status) {
  switch(status)
  {
    case ERR_OK:                      led_seq_for(3, FAST_BLINK_DELAY);  break;
    case ERR_WIFI_CONNECT_FAILED:     led_seq_for(5, SLOW_BLINK_DELAY);  break;
    case ERR_MQTT_CONNECTION_FAILED:  led_seq_for(10, SLOW_BLINK_DELAY); break;
    case ERR_MQTT_PUBLISH_FAILED:     led_seq_for(2, SLOW_BLINK_DELAY);  break;
    case ERR_MQTT_UNEXPECTED_DISC:    led_seq_for(10, SLOW_BLINK_DELAY); break;
    case ERR_SERVER_ISSUE:            led_seq_for(7, SLOW_BLINK_DELAY);  break;
  }
}

bool read_eeprom_string(int addr, char* buffer, int max_size) {
  for(int i=0; i<max_size; i++){
    //EEPROM.get(addr+(i*sizeof(char)), buffer[i]);
    buffer[i] = EEPROM.read(addr+i);
    if(buffer[i] == '\0'){
      Serial.println(i);
      return true;
    }
  }
  return false;
}

void setupAccessPoint() {
  Serial.println();
  Serial.print("Configuring access point...");
  //@TODO: I believe the parameters in softAP are being ignored. My AP is called ESP_%c%c%c%c. Find out why.
  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ap_ssid, ap_pass);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

bool tryWifiConnect(){
  //WiFi.begin(ssid, pass);
  //char s[100];
  //char p[100];
  //ssid.toCharArray(s, 100);
  //pass.toCharArray(p, 100);
  //WiFi.begin(s, p);
  //WiFi.begin("SKYD6F7B", "CBECPTFA");
  for(int y = 0; y < 6 && WiFi.status() != WL_CONNECTED; y++){
    delay(1500);
    Serial.print("\tFailed to connect. WiFi.status() is ");
    Serial.println(WiFi.status());
  }
  //@TODO: Is this WL_CONNECTED the reason why this function is returning false? See if there's a delay when setting WiFi.status or something. println what's the WiFi.status()
  return WiFi.status() == WL_CONNECTED;
}

/* Handle serving the webpage and getting the post */
void handleRoot() {
  if(server.args() > 0){
    for(int x = 0; x < server.args(); x++){
      Serial.print(server.argName(x));
      Serial.print(":");
      Serial.println(server.arg(x));
    }
    server.arg(0).toCharArray(ssid, 100);
    String pwd = server.arg(1);
    // TODO: do a full URL decode here
    pwd.replace("%21", "!");
    pwd.toCharArray(pass, 100);
    Serial.print("new pwd: ");
    Serial.println(pwd);
    EEPROM.put(WIFI_SSID_ADDR, ssid);
    EEPROM.put(WIFI_PASS_ADDR, pass);
    Serial.println("Commiting ssid and pass to EEPROM");
    EEPROM.commit();

    Serial.println("checking status");
    if(tryWifiConnect()){
      server.send(200, "text/html", "couldn't connect");
      Serial.println("couln't connect");
    }else{
      //// NOTE: cannot return a page that says I've connected because
      ////       the access point isn't live when connected as a client
      //server.send(200, "text/html", "connected!@#$%");=
      Serial.println("connected!!");
      // set payload to 1 as a flag for MQTT to know to connect
      sendPayload = 1;
    }
  }else{
    // TODO: push down JS to refresh the page 15 seconds after post
    server.send(200, "text/html", "<html><body><h1>Connect Me!</h1><form action='.' method='POST'><input type='text' name='ssid' placeholder='SSID' /><input type='password' name='pass' placeholder='password' /><input type='submit' value='connect!' /></form><p>(note that the page will not return any data if you end up successfully connecting to Wifi!)</p></body></html>");
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");
  for (int y = 0; y < 5 && (ret = mqtt.connect()) != 0; y++) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 2 seconds...");
       mqtt.disconnect();
       delay(2000);  // wait 5 seconds
  }
  if(mqtt.connect() == 0){
    Serial.println("MQTT Connected!");
  }else{
    // not connected so reset wifi access point
    setupAccessPoint();
    sendPayload = 0;
  }
}
