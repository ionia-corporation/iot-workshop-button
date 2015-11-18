#include <EEPROM.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>

extern "C" {
  #include "user_interface.h"
}

/*************************** MQTT Setup **************************************/
#define MQTT_SERVER_ADDR  "m11.cloudmqtt.com"
#define MQTT_SERVERPORT   12476
#define MQTT_USERNAME_S   "ard"
#define MQTT_KEY          "uino"
#define MQTT_OUT_TOPIC    "button"
#define MQTT_IN_TOPIC     "response"

/*************************** Wifi Setup **************************************/
// Acces Point -- The ap_ssid_prefix will be prepended to part of the MAC addr
const char *ap_ssid_prefix = "ESP8266_";
const char *ap_pass = "knockknock"; //Min. 8 characters
ESP8266WebServer server(80);
#define WIFI_SSID_ADDR    0
#define WIFI_PASS_ADDR    100

/*************************** PROG SETINGS *************************************/
#define DEFAULT_BUFFER_SIZE 100 //Buffer size to use for creds, messages, etc.
//Remember to update the EEPROM addresses for the client SSID and password if
//you ever increase the size of DEFAULT_BUFFER_SIZE over 100. These addresses
//can be found in the definitions of WIFI_SSID_ADDR and WIFI_PASS_ADDR.

/*************************** GPIO Setup **************************************/
#define LED_PIN    5 //The LED in the button is to be connected to this pin
//The button for the demo needs to be connected between the RST pin and GND

/*************************** Workflow Variables *******************************/
#define RESPONSE_WAITING_TIME 60000 //Wait for an MQTT response for up to 1min
#define LED_DEBUGGING_ENABLED true  //Enable LED debugging (blink LED to report
                                    //system status). See play_led_sequence()
#define SLOW_BLINK_DELAY 300
#define FAST_BLINK_DELAY 100

enum {
  ERR_OK                      = 0,
  ERR_WIFI_CONNECT_FAILED     = 1,
  ERR_MQTT_CONNECTION_FAILED  = 2,
  ERR_MQTT_PUBLISH_FAILED     = 3,
  ERR_MQTT_UNEXPECTED_DISC    = 4,
  ERR_SERVER_ISSUE            = 5
};

enum {
  LED_USER_COMING  = 0,
  LED_USER_AWAY    = 1,
  LED_USER_TIMEOUT = 2
};

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

//This is the topic we'll publish to when the device boots up. Pressing the
//connected between RST and GND causes a reboot
const char BUTTON_TOPIC[] PROGMEM = MQTT_OUT_TOPIC;
Adafruit_MQTT_Publish buttonTopic = Adafruit_MQTT_Publish(&mqtt, BUTTON_TOPIC);

//This is the topic we subscribe to in order to receive the user's response
//after the RST button is pressed. We wait for incoming messages for 1 minute
//after boot:
const char RESPONSE_TOPIC[] PROGMEM = MQTT_IN_TOPIC;
Adafruit_MQTT_Subscribe respTopic = Adafruit_MQTT_Subscribe(&mqtt, RESPONSE_TOPIC);

#define BUTTON_PRESSED_MQTT_PAYLOAD "The button has been pressed!"

/***************************** Program **************************************/
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

void play_led_sequence(uint16_t status, bool is_debug_sequence=false)
{
  if(is_debug_sequence==true)
  {
    if(!LED_DEBUGGING_ENABLED) return;
    switch(status)
    {
      case ERR_OK:                     led_seq_for(2,  FAST_BLINK_DELAY); break;
      case ERR_WIFI_CONNECT_FAILED:    led_seq_for(5,  SLOW_BLINK_DELAY); break;
      case ERR_MQTT_CONNECTION_FAILED: led_seq_for(10, SLOW_BLINK_DELAY); break;
      case ERR_MQTT_PUBLISH_FAILED:    led_seq_for(2,  SLOW_BLINK_DELAY); break;
      case ERR_MQTT_UNEXPECTED_DISC:   led_seq_for(10, SLOW_BLINK_DELAY); break;
      case ERR_SERVER_ISSUE:           led_seq_for(7,  SLOW_BLINK_DELAY); break;
    }
    return;
  }
  //If it's not a debug sequence but a functional one (for the end user):
  switch(status)
  {
    case LED_USER_COMING:   led_seq_for(20, SLOW_BLINK_DELAY); break;
    case LED_USER_AWAY:     led_seq_for(10, FAST_BLINK_DELAY); break;
    case LED_USER_TIMEOUT:  led_seq_for(5,  FAST_BLINK_DELAY); break;
    default: Serial.println("Error! Unrecognised functional LED sequence");
  }
}

/* Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it. */
void setup()
{
  //Set up the GPIO
  pinMode(LED_PIN, OUTPUT);

  //Initialise UART and EEPROM
  Serial.begin(115200);
  EEPROM.begin(4096);

  Serial.print("\nWelcome! The chip ID of your ESP8266 is [");
  Serial.print(ESP.getChipId());
  Serial.print("] and your MAC address [");
  Serial.print(WiFi.macAddress());
  Serial.println("]");

  //If the user has already set up their WiFi creds before, they'll be in EEPROM
  char ssid[DEFAULT_BUFFER_SIZE] = "";
  char pass[DEFAULT_BUFFER_SIZE] = "";
  bool gotValidCredentials = 0;
  EEPROM.get(WIFI_SSID_ADDR, ssid);
  EEPROM.get(WIFI_PASS_ADDR, pass);
  if(strlen(ssid) > 0) gotValidCredentials = 1;
  else Serial.println("There are no WiFi credentials stored in the EEPROM");

  if(gotValidCredentials)
  {
    Serial.println("Trying to use the stored WiFi credentials:");
    Serial.println(String("\tSSID: [") + ssid + "]\n\tpass: [" + pass + "]");
    if(!tryWifiConnect(ssid, pass))
    {
      play_led_sequence(ERR_WIFI_CONNECT_FAILED, true);
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
    mqtt.subscribe(&respTopic); //Subscribe to the response topic
    MQTT_connect();
    if(!buttonTopic.publish(BUTTON_PRESSED_MQTT_PAYLOAD))
    {
      Serial.println("PUBLISH FAILED");
      play_led_sequence(ERR_MQTT_PUBLISH_FAILED, true);
    }else{
      Serial.println("MQTT message successfully published");
      play_led_sequence(ERR_OK, true);
    }

    Serial.println("Waiting for the user to respond to the message...");
    //Wait for a message in the respTopic MQTT topic. If we haven't received one
    //after ${RESPONSE_WAITING_TIME} ms, time out.
    static char mqtt_response[DEFAULT_BUFFER_SIZE] = "";
    static bool got_response = false;
    Adafruit_MQTT_Subscribe *subscription;

    for(uint16_t i=0; i<RESPONSE_WAITING_TIME; i+=1000)
    {
      while ((subscription = mqtt.readSubscription(1000)))
      {
        if (subscription == &respTopic)
        {
          strncpy(mqtt_response, (char *)respTopic.lastread, DEFAULT_BUFFER_SIZE);
          got_response = true;
          Serial.print("Received a message in the response tocpic: ");
          Serial.println(mqtt_response);
        }
      }
      if(got_response == true) break;
    }

    if(got_response == true) //Got a response via MQTT!
    {
      Serial.print(F("Received MQTT message: "));
      Serial.println(mqtt_response);
      if(!strncmp(mqtt_response, "user_away", DEFAULT_BUFFER_SIZE))
      {
        Serial.println("\tPlaying the LED_USER_AWAY sequence...");
        play_led_sequence(LED_USER_AWAY);
      }
      else if(!strncmp(mqtt_response, "user_coming", DEFAULT_BUFFER_SIZE))
      {
        Serial.println("\tPlaying the LED_USER_COMING sequence...");
        play_led_sequence(LED_USER_COMING);
      }
    }else{                   //We didn't receive an MQTT response in time
      Serial.println("The wait for an MQTT response timed out!");
      Serial.println("\tPlaying the LED_USER_TIMEOUT sequence...");
      play_led_sequence(LED_USER_TIMEOUT);
    }

    // IMPORTANT: Deep-sleep will wake up by rebooting the ESP8266.
    //            For it to work, the pins 16 and RST need to be shorted
    //            BUT in this case we don't want the device to ever wake up.
    //            We use deepSleep as a way of keeping low power consumption
    //            until the Button connecting RST and GND gets pressed. Only
    //            then we'll wake up the ESP and publish a 'button pressed' msg
    // This call should never return:
    ESP.deepSleep(5000000, WAKE_RFCAL); //time is in micro seconds
    //@TODO: depSleep() is returning the 1st time it's called. Figure out why.
    //       In the meantime:
    while(1) delay(1000); //stop the program from re-entering loop()
  }else{ //If we're in AP mode or simply not connected
    server.handleClient();
  }
}

void setupAccessPoint()
{
  Serial.println();
  Serial.print("\nConfiguring access point...");
  randomSeed(analogRead(A0));
  int wifi_channel = random(1,14); //The channel to use as an AP. Range: [1, 13]

  char ap_ssid[DEFAULT_BUFFER_SIZE] = "";
  String esp_mac = WiFi.macAddress();
  esp_mac.remove(0, 9);     // Remove the first 3 octets of the mac
  esp_mac.replace(":", ""); // Remove colons for the ap_ssid

  sprintf(ap_ssid, "%s%s", ap_ssid_prefix, esp_mac.c_str());
  Serial.print(String("\n\tAP SSID:     ") + ap_ssid);
  Serial.print(String("\n\tAP Password: ") + ap_pass);
  WiFi.softAP(ap_ssid, ap_pass, wifi_channel);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("\n\tAP IP address: ");
  Serial.println(myIP);
  server.on("/", handle_http_root);
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Next steps:");
  Serial.println("\t1. Connect to your device's WiFi network");
  Serial.print(  "\t2. Open your browser and go to the address ");
  Serial.println(myIP);
  Serial.println("\t3. Introduce the credentials of your home WiFi");
}

/* Connect to a network.
     * return: true if the connection was successful. false otherwise.
     */
bool tryWifiConnect(char *ssid, char *password)
{
  WiFi.begin(ssid, password);
  char err_cause[DEFAULT_BUFFER_SIZE] = "";
  switch(WiFi.waitForConnectResult())
  {
    case WL_CONNECTED:
      Serial.println("Success! Connection to the AP stablished");
      return true;
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
  return false;
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
      return (c & 0x0F);
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
      dst[dst_idx] = src[i];
    }
    dst_idx++;
  }
  dst[dst_idx] = '\0';
}

inline void serve_main_page()
{
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

/* Handle serving the webpage and getting the post */
void handle_http_root()
{
  if(server.args() > 0) //The user sent the credentials via HTTP POST
  {
    /*
    //Print all received args (encoded)
    for(int x = 0; x < server.args(); x++)
    {
      Serial.print(server.argName(x));
      Serial.print(":");
      Serial.println(server.arg(x));
    }
    */
    static char received_ssid[DEFAULT_BUFFER_SIZE] = ""; //For the encoded ssid
    static char received_pass[DEFAULT_BUFFER_SIZE] = ""; //For the encoded pass
    static char decoded_ssid[DEFAULT_BUFFER_SIZE] = "";  //For the decoded ssid
    static char decoded_pass[DEFAULT_BUFFER_SIZE] = "";  //For the decoded pass
    server.arg(0).toCharArray(received_ssid, DEFAULT_BUFFER_SIZE);
    server.arg(1).toCharArray(received_pass, DEFAULT_BUFFER_SIZE);
    decode_url_string(decoded_ssid, received_ssid);
    decode_url_string(decoded_pass, received_pass);
    Serial.println("\nReceived new WiFi credentials:");
    Serial.print("\tNew ssid: ");
    Serial.println(decoded_ssid);
    Serial.print("\tNew pass: ");
    Serial.println(decoded_pass);
    EEPROM.put(WIFI_SSID_ADDR, decoded_ssid);
    EEPROM.put(WIFI_PASS_ADDR, decoded_pass);
    Serial.println("Commiting ssid and pass to EEPROM");
    EEPROM.commit();

    Serial.println("Attempting to stablish WiFi connection...");
    WiFi.disconnect(); // Disconnect the AP before trying to connect as a client
    if(!tryWifiConnect(decoded_ssid, decoded_pass))
    {
      //@TODO?: Include the reason why the connection failed in the HTTP resp
      //        tryWifiConnect() will have to be modified to return the reason.
      server.send(200, "text/html", "Couldn't connect to the WiFi network");
    }
  }else{
    serve_main_page();
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
