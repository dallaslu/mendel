#include <ArduinoOTA.h>
#include <Scheduler.h>
#include <Task.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
//#include <Ticker.h> //for LED status
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Stepper.h>

#define STEPS 100 // steps per circle
//#define BUTTON 15
#define BUTTON D10

Stepper stepper(STEPS, D4, D5, D6, D7); // IN1~IN4
//Stepper stepper(STEPS, 4, 5, 6, 7); // IN1~IN4

//Ticker ticker;
WiFiManager wifiManager;
String apName = ("mendel-feeder_" + String(ESP.getChipId(), HEX));
const char *wifiName = apName.c_str();

int hour = -1; // hour of day
int timeZone = +8;

int feeding = 0;

class FeedTask : public Task {
  private:
    int feederHours[4] = {0, 9, 15, 21};
    int feederProcess = 0;
    int feederPackage[4] = {1, 3, 2, 3};
    int feederHoursSum = sizeof(feederHours) / sizeof(feederHours[0]);
  protected:
    void setup() {
      while (hour < 0) {
        delay(10000);
      }
      for (int i = 0; i < feederHoursSum; i++) {
        if (hour <= feederHours[i]) {
          feederProcess = i;
          break;
        }
      }
      Serial.println("*FEED ready.");
    }
    void loop() {
      Serial.print("*FEED cur: ");
      Serial.println(feederProcess);
      Serial.print("*FEED now : ");
      Serial.println(hour);
      Serial.print("*FEED next: ");
      Serial.println(feederHours[feederProcess]);
      if (hour == feederHours[feederProcess]) {
        Serial.println("*FEED feeding");
        feederProcess ++;
        if (feederProcess >= feederHoursSum - 1) {
          feederProcess = 0;
        }
        doFeedPackage(feederPackage[feederProcess]);
      }
      delay(10000);
    }
    /** yeild for WDT */
    void espStep(int step) {
      if (step > 0) {
        for (int i = 0; i < step; i++) {
          stepper.step(1);
          yield();
        }
      } else {
        for (int i = 0; i < -step; i++) {
          stepper.step(-1);
          yield();
        }
      }
    }
  public:
    void doFeedPackage(int pkg) {
      if (feeding == 0) {
        feeding = 1;
        for (int i = 0; i < pkg; i++) {
          espStep(-128); // left 1/16 loop
          delay(100);
          espStep(1152); //2048 steps to a circle in 4-step-mode // right 9/16 loop
          delay(100);
          espStep(-128); // left 2/16 loop
          delay(10);
        }
        feeding = 0;
      }
    }
} feedTask;
class OtaTask : public Task {
  protected:
    void setup() {
      // Port defaults to 8266
      ArduinoOTA.setPort(8266);
      ArduinoOTA.setHostname(wifiName);
      //  ArduinoOTA.setPassword((const char *)"123");
      ArduinoOTA.onStart([]() {
        Serial.println("*OTA Start");
      });
      ArduinoOTA.onEnd([]() {
        Serial.println("\n*OTA End");
      });
      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      });
      ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });
      ArduinoOTA.begin();
    }
    void loop() {
      ArduinoOTA.handle();
    }
} otaTask;

class NtpTask : public Task {
  private:
    unsigned int localPort = 2390;      // local port to listen for UDP packets
    const char* ntpServerName = "time.nist.gov";
    IPAddress timeServerIP; // time.nist.gov NTP server address
    WiFiUDP udp;
    const static int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
    byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
  protected:
    void setup() {
      udp.begin(localPort);
    }
    // send an NTP request to the time server at the given address
    unsigned long sendNTPpacket(IPAddress& address)
    {
      Serial.println("*NTP sending...");
      // set all bytes in the buffer to 0
      memset(packetBuffer, 0, NTP_PACKET_SIZE);
      // Initialize values needed to form NTP request
      // (see URL above for details on the packets)
      packetBuffer[0] = 0b11100011;   // LI, Version, Mode
      packetBuffer[1] = 0;     // Stratum, or type of clock
      packetBuffer[2] = 6;     // Polling Interval
      packetBuffer[3] = 0xEC;  // Peer Clock Precision
      // 8 bytes of zero for Root Delay & Root Dispersion
      packetBuffer[12]  = 49;
      packetBuffer[13]  = 0x4E;
      packetBuffer[14]  = 49;
      packetBuffer[15]  = 52;
      // all NTP fields have been given values, now
      // you can send a packet requesting a timestamp:
      udp.beginPacket(address, 123); //NTP requests are to port 123
      udp.write(packetBuffer, NTP_PACKET_SIZE);
      udp.endPacket();
    }
    void loop() {
      //get a random server from the pool
      WiFi.hostByName(ntpServerName, timeServerIP);
      sendNTPpacket(timeServerIP); // send an NTP packet to a time server
      // wait to see if a reply is available
      delay(1000);
      int cb = udp.parsePacket();
      if (!cb) {
        Serial.println("no packet yet");
      }
      else {
        Serial.print("*NTP received, length=");
        Serial.println(cb);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

        //the timestamp starts at byte 40 of the received packet and is four bytes,
        // or two words, long. First, esxtract the two words:

        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        // combine the four bytes (two words) into a long integer
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        Serial.print("*NTP Seconds = " );
        Serial.println(secsSince1900);

        // now convert NTP time into everyday time:
        Serial.print("Unix time = ");
        // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
        const unsigned long seventyYears = 2208988800UL;
        // subtract seventy years:
        unsigned long epoch = secsSince1900 - seventyYears;
        // print Unix time:
        Serial.println(epoch);

        hour = (epoch  % 86400L) / 3600;
        hour += timeZone;
        if (hour >= 24) {
          hour -= 24;
        }

        // print the hour, minute and second:
        Serial.print("UTC: ");       // UTC is the time at Greenwich Meridian (GMT)
        Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
        Serial.print(':');
        if ( ((epoch % 3600) / 60) < 10 ) {
          // In the first 10 minutes of each hour, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
        Serial.print(':');
        if ( (epoch % 60) < 10 ) {
          // In the first 10 seconds of each minute, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.println(epoch % 60); // print the second
      }
      // wait ten seconds before asking for the time again
      delay(10000);
    }
} ntpTask;
class ButtonTask : public FeedTask {
  private:
    int buttonFoo = 0;
  protected:
    void setup() {
      pinMode(BUTTON, INPUT);
      digitalWrite(BUTTON, LOW); // writing a HIGH to an INPUT pin
    }
    void loop() {
      int buttonState = digitalRead(BUTTON);
      if (buttonState == HIGH) {
        buttonFoo ++;
      } else {
        buttonFoo = 0;
      }
      digitalWrite(BUTTON, LOW); // writing a HIGH to an INPUT pin
      if (buttonFoo == 100) {
        Serial.println("*BTN test");
        doFeedPackage(2);
        buttonFoo = 0;
      }
      delay(10);
    }
} buttonTask;

void setup() {
  Serial.begin(115200);
  Serial.println("starting...");
  //set led pin as output
  //  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  //  ticker.attach(0.6, tick);

  WiFi.hostname(wifiName);
  //wifiManager.resetSettings();
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  startAutoConfig(&wifiManager);
  //  MDNS.begin("WebPool");
  // steper speed: 90 steps per min
  stepper.setSpeed(120);

  Scheduler.start(&ntpTask);
  Scheduler.start(&feedTask);
  Scheduler.start(&buttonTask);
  Scheduler.start(&otaTask);
  Scheduler.begin();
}

void loop() {
}
void startAutoConfig(WiFiManager *myWiFiManage) {
  if (!myWiFiManage->autoConnect(wifiName)) {
    Serial.println("failed to connect and hit timeout");
    delay(1000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(3000);
  }
  //if you get here you have connected to the WiFi
  Serial.println("connected.:)");
  //  ticker.detach();
  //keep LED on
  //  digitalWrite(BUILTIN_LED, LOW);
}
void configModeCallback (WiFiManager *myWiFiManager) {
  //  Serial.println("Entered config mode");
  //  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  //  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  //  ticker.attach(0.2, tick);
}
//void tick() {
//  //toggle state
//  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
//  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
//}
