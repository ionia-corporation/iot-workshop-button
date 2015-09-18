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
/* Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it. */
void setup()
{
  //Set up the GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  //Initialise UART and EEPROM
  Serial.begin(115200);
  EEPROM.begin(4096);

  Serial.print("\nWelcome! The chip ID of your ESP8266 is [");
  Serial.print(ESP.getChipId());
  Serial.print("] and your MAC address [");
  Serial.print(WiFi.macAddress());
  Serial.println("]");

  //If the user has already set up their WiFi creds before they'll be in EEPROM
  bool gotValidCredentials = 0;
  EEPROM.get(WIFI_SSID_ADDR, ssid);
  EEPROM.get(WIFI_PASS_ADDR, pass);
  if(strlen(ssid) > 0) gotValidCredentials = 1;
  else Serial.println("There are no WiFi credentials stored in the EEPROM");

  if(gotValidCredentials)
  {
    Serial.println("Trying to use the stored WiFi credentials:");
    Serial.println(String("\tSSID: [") + ssid + "]\n\tpass: [" + pass + "]");
    if(!tryWifiConnect())
    {
      play_led_sequence(ERR_WIFI_CONNECT_FAILED);
      setupAccessPoint();
    }
  }else{
    Serial.println("no creds in EEPROM (yet) ");
    setupAccessPoint();
  }
}

void loop()
{
  if(WiFi.status() == WL_CONNECTED) //If we're in client mode and connected
  {
    MQTT_connect();
    if(testFeed.publish("Our pub payload!"))
    {
      Serial.println("MQTT message successfully published");
      play_led_sequence(ERR_OK);
    }else{
      Serial.println("PUBLISH FAILED");
      play_led_sequence(ERR_MQTT_PUBLISH_FAILED);
    }
    // TODO: make this a deep sleep instead (note the ESP wakes up by rebooting)
    //delay(5000);

    // IMPORTANT: Deep-sleep will wake up by rebooting the ESP8266.
    //            For it to work, the pins 16 and RST need to be shorted
    // This call never returns
    ESP.deepSleep(5000000, WAKE_RFCAL); //time is in micro seconds
    //@TODO: Figure out why we need this endless loop:
    while(1) delay(1000); //stop the program from re-entering loop()
  }else{ //If we're in AP mode or simply not connected
    server.handleClient();
  }
}

#define SLOW_BLINK_DELAY 300
#define FAST_BLINK_DELAY 100

inline void led_seq_for(uint32_t loops, uint32_t delay_len)
{
  for(int i=0; i<loops; i++)
  {
    delay(delay_len);
    digitalWrite(LED_PIN, HIGH);
    delay(delay_len);
    digitalWrite(LED_PIN, LOW);
  }
}

//@TODO: Make sure this ifdef works fine. Explain how to use it in a comment.
#define LED_DEBUGGING_ENABLED
void play_led_sequence(uint16_t status)
{
  #ifdef LED_DEBUGGING_ENABLED
  switch(status)
  {
    case ERR_OK:                     led_seq_for(2,  FAST_BLINK_DELAY); break;
    case ERR_WIFI_CONNECT_FAILED:    led_seq_for(5,  SLOW_BLINK_DELAY); break;
    case ERR_MQTT_CONNECTION_FAILED: led_seq_for(10, SLOW_BLINK_DELAY); break;
    case ERR_MQTT_PUBLISH_FAILED:    led_seq_for(2,  SLOW_BLINK_DELAY); break;
    case ERR_MQTT_UNEXPECTED_DISC:   led_seq_for(10, SLOW_BLINK_DELAY); break;
    case ERR_SERVER_ISSUE:           led_seq_for(7,  SLOW_BLINK_DELAY); break;
  }
  #endif
}

void setupAccessPoint()
{
  Serial.println();
  Serial.print("\nConfiguring access point...");
  //@TODO: I believe the parameters in softAP are being ignored. My AP is called
  //       ESP_%c%c%c%c. Find out why.
  //@TODO: Generate a random number 1-13 to use as the WiFi channel in AP mode.
  /* You can pass 0 as the password parameter if you want the AP to be open. */
  WiFi.softAP(ap_ssid, ap_pass, 1); //3rd argument is the WiFi channel (1-13)

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

/* Connect to a network.
     * return: true if the connection was successful. false otherwise.
     */
//@TODO: This function should get passed ssid/pass instead of using globals
bool tryWifiConnect()
{
  WiFi.begin(ssid, pass);
  char err_cause[100] = "";
  switch(WiFi.waitForConnectResult())
  {
    case WL_CONNECTED:
      Serial.println("Success! Connection to the AP stablished");
      break;
    case WL_DISCONNECTED:
      Serial.println("Connection to AP failed. Is the mode set to STA?");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("Connection to AP failed. Network does not exist");
    default:
      Serial.print("Connection to AP failed with status ");
      Serial.println(WiFi.status());
      /* Meaning of each status code as defined in wl_definitions.h:
         WL_IDLE_STATUS      = 0
         WL_NO_SSID_AVAIL    = 1
         WL_SCAN_COMPLETED   = 2
         WL_CONNECTED        = 3
         WL_CONNECT_FAILED   = 4
         WL_CONNECTION_LOST  = 5
         WL_DISCONNECTED     = 6
      */
  }
  return (WiFi.status() == WL_CONNECTED);
}

byte ascii_char_to_byte(char c)
{
  c = toupper(c);
  switch(c)
  {
    case 'A': return 0x0A;
    case 'B': return 0x0B;
    case 'C': return 0x0C;
    case 'D': return 0x0D;
    case 'E': return 0x0E;
    case 'F': return 0x0F;
    default:
      c &= 0x0F;
      return c;
  }
}

bool decode_url_string(char *dst, char *src)
{
  uint32_t dst_idx = 0;
  char debug = 0x00;
  for(uint32_t i=0; i< strlen(src); i++)
  {
    if(src[i] == '%')
    {
      dst[dst_idx]  = ascii_char_to_byte(src[++i]) & 0x0F;
      dst[dst_idx]  = dst[dst_idx] << 4;
      dst[dst_idx] |= ascii_char_to_byte(src[++i]) & 0x0F;
    }
    else
    {
      dst[dst_idx] += src[i];
    }
    dst_idx++;
  }
  dst[dst_idx] = '\0';
}

/* Handle serving the webpage and getting the post */
//@TODO: Simplify the implementation of this func. Split it into smaller ones?
//@TODO: Rename the function to use a more descriptive name
void handleRoot()
{
  if(server.args() > 0)
  {
    for(int x = 0; x < server.args(); x++)
    {
      Serial.print(server.argName(x));
      Serial.print(":");
      Serial.println(server.arg(x));
    }
    server.arg(0).toCharArray(ssid, 100);
    String pwd = server.arg(1);
    pwd.toCharArray(pass, 100);
    decode_url_string(ssid, pass);
    Serial.print("new pwd: ");
    Serial.println(pass);
    EEPROM.put(WIFI_SSID_ADDR, ssid);
    EEPROM.put(WIFI_PASS_ADDR, pass);
    Serial.println("Commiting ssid and pass to EEPROM");
    EEPROM.commit();

    Serial.println("checking status");
    WiFi.disconnect(); // Disconnect the AP before trying to connect as a client
    if(tryWifiConnect())
    {
      //@TODO?: Include the reason why the connection failed in the HTTP resp
      //        tryWifiConnect() will have to be modified to return the reason.
      server.send(200, "text/html", "couldn't connect");
      Serial.println("couln't connect");
    }else{
      // NOTE: cannot return a page that says I've connected because
      //       the access point isn't live when connected as a client
      //server.send(200, "text/html", "connected!@#$%");=
      Serial.println("connected!!");
    }
  }else{
    // TODO: push down JS to refresh the page 15 seconds after post
    // Build a string with the contents of the main page:
    String mainPage = "";
    mainPage += "<html>";
    mainPage += "<body>";
    mainPage += "<h1>Connect Me!</h1>";
    mainPage += "<form action='.' method='POST'>";
    mainPage += "<input type='text' name='ssid' placeholder='SSID' />";
    mainPage += "<input type='password' name='pass' placeholder='password' />";
    mainPage += "<input type='submit' value='connect!' />";
    mainPage += "</form>";
    mainPage += "<p>(note that the page will not return any data if you ";
    mainPage += "end up successfully connecting to Wifi!)</p>";
    mainPage += "</body>";
    mainPage += "</html>";
    server.send(200, "text/html", mainPage);
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
void MQTT_connect()
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
  {
    return;
  }

  Serial.print("Connecting to MQTT... ");
  for (int y = 0; y < 5 && (ret = mqtt.connect()) != 0; y++)
  { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 2 seconds...");
       mqtt.disconnect();
       delay(2000);  // wait 5 seconds
  }
  if(mqtt.connect() == 0)
  {
    Serial.println("MQTT Connected!");
  }else{
    // not connected so reset wifi access point
    setupAccessPoint();
  }
}
