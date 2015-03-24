// Test code for Adafruit GPS modules using MTK3329/MTK3339 driver
//
// This code shows how to listen to the GPS module in an interrupt
// which allows the program to have more 'freedom' - just parse
// when a new NMEA sentence is available! Then access data when
// desired.
//
// Tested and works great with the Adafruit Ultimate GPS module
// using MTK33x9 chipset
//    ------> http://www.adafruit.com/products/746
// Pick one up today at the Adafruit electronics shop 
// and help support open source hardware & software! -ada

#include <Adafruit_GPS.h>
#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// CC3000 interrupt and control pins
#define ADAFRUIT_CC3000_IRQ 2 // MUST be an interrupt pin!
#define ADAFRUIT_CC3000_VBAT 7
#define ADAFRUIT_CC3000_CS 10

// Hardware SPI required for remaining pins.
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(
  ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
  SPI_CLOCK_DIVIDER);
  
// Wifi access credentials
// WiFi access point credentials
#define WLAN_SSID     ""  // 32 characters max
#define WLAN_PASS     ""
#define WLAN_SECURITY WLAN_SEC_WPA2 // WLAN_SEC_UNSEC/WLAN_SEC_WEP/WLAN_SEC_WPA/WLAN_SEC_WPA2

// API URL
#define WEBSITE "citizenscode-gpstracker-api.herokuapp.com"
const unsigned long
  dhcpTimeout     = 60L * 1000L, // Max time to wait for address from DHCP
  connectTimeout  = 15L * 1000L, // Max time to wait for server connection
  responseTimeout = 15L * 1000L; // Max time to wait for data from server
unsigned long
  currentTime = 0L;

// If you're using a GPS module:
// Connect the GPS Power pin to 5V
// Connect the GPS Ground pin to ground
// If using software serial (sketch example default):
//   Connect the GPS TX (transmit) pin to Digital 3
//   Connect the GPS RX (receive) pin to Digital 2
// If using hardware serial (e.g. Arduino Mega):
//   Connect the GPS TX (transmit) pin to Arduino RX1, RX2 or RX3
//   Connect the GPS RX (receive) pin to matching TX1, TX2 or TX3

// If you're using the Adafruit GPS shield, change 
// SoftwareSerial mySerial(3, 2); -> SoftwareSerial mySerial(8, 7);
// and make sure the switch is set to SoftSerial

// If using software serial, keep this line enabled
// (you can change the pin numbers to match your wiring):
SoftwareSerial mySerial(4, 3); // mySerial(RX, TX)

// If using hardware serial (e.g. Arduino Mega), comment out the
// above SoftwareSerial line, and enable this line instead
// (you can change the Serial number to match your wiring):

//HardwareSerial mySerial = Serial1;


Adafruit_GPS GPS(&mySerial);


// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences. 
#define GPSECHO  false

// this keeps track of whether we're using the interrupt
// off by default!
boolean usingInterrupt = true;
void useInterrupt(boolean); // Func prototype keeps Arduino 0023 happy

void setup()  
{
  // connect at 115200 so we can read the GPS fast enough and echo without dropping chars
  // also spit it out
  Serial.begin(115200);
  Serial.println("GPS starting...");
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(9600);
  
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time
  
  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  // GPS.sendCommand(PMTK_SET_NMEA_UPDATE_200_MILLIHERTZ);   // 200 mHz, or once every 5 seconds
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_ANTENNA);

  // the nice thing about this code is you can have a timer0 interrupt go off
  // every 1 millisecond, and read data from the GPS for you. that makes the
  // loop code a heck of a lot easier!
  useInterrupt(true);
  
  delay(1000);
  // Ask for firmware version
  mySerial.println(PMTK_Q_RELEASE);
  
  Serial.println(F("\nWifi starting..."));
  if (!cc3000.begin())
  {
    Serial.println(F("Unable to initialise the CC3000! Check your wiring?"));
    while(1);
  }

  // Delete old connection data on the module
  Serial.println(F("\nDeleting old connection profiles"));
  if (!cc3000.deleteProfiles()) {
    Serial.println(F("Failed!"));
    while(1);
  }

  /* Attempt to connect to an access point */
  char *ssid = WLAN_SSID;             /* Max 32 chars */
  Serial.print(F("\nAttempting to connect to ")); Serial.println(ssid);

  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(F("Failed!"));
    while(1);
  }

  Serial.println(F("Connected!"));

  /* Wait for DHCP to complete */
  Serial.println(F("Request DHCP"));
  while (!cc3000.checkDHCP())
  {
    delay(100);
  }

  uint32_t ip = 0L;
  Serial.print(WEBSITE); Serial.print(F(" -> "));
  while (ip == 0) {
    if (!cc3000.getHostByName(WEBSITE, &ip)) {
      Serial.println(F("Couldn't resolve!"));
    }
    delay(500);
  }
  cc3000.printIPdotsRev(ip);
  Serial.println();

  /* Try connecting to the website.
     Note: HTTP/1.1 protocol is used to keep the server from closing the connection before all data is read.
  */
  Adafruit_CC3000_Client client = cc3000.connectTCP(ip, 80);
  if (client.connected()) {
    Serial.println(F("Posting data..."));
    client.fastrprint(F("POST "));
    Serial.print(F("POST "));
    client.fastrprint(F("/")); // Root directory
    Serial.print(F("/")); // Root directory
    client.fastrprint(F(" HTTP/1.1\r\n"));
    Serial.print(F(" HTTP/1.1\r\n"));
    client.fastrprint(F("Host: "));
    Serial.print(F("Host: "));
    client.fastrprint(WEBSITE);
    Serial.print(WEBSITE);
    client.fastrprint(F("\r\n"));
    Serial.print(F("\r\n"));
    client.fastrprint(F("Content-Length: 24\r\n"));
    Serial.print(F("Content-Length: 24\r\n"));
    client.fastrprint(F("Accept-Encoding: gzip, deflate\r\n"));
    Serial.print(F("Accept-Encoding: gzip, deflate\r\n"));
    client.fastrprint(F("Accept: */*\r\n"));
    Serial.print(F("Accept: */*\r\n"));
    client.fastrprint(F("User-Agent: Arduino-GPS-Wifi v1.0\r\n"));
    Serial.print(F("User-Agent: Arduino-GPS-Wifi v1.0\r\n"));
    client.fastrprint(F("Connection: keep-alive\r\n"));
    Serial.print(F("Connection: keep-alive\r\n"));
    client.fastrprint(F("Content-Type: application/x-www-form-urlencoded\r\n\r\n"));
    Serial.print(F("Content-Type: application/x-www-form-urlencoded\r\n\r\n"));
    client.fastrprint(F("lat=45.9588&lng=-66.6482"));
    Serial.print(F("lat=45.9588&lng=-66.6482"));
    client.fastrprint(F("\r\n"));
    Serial.println(F("Posting complete..."));
  } else {
    Serial.println(F("Connection failed"));    
    return;
  }
  Serial.println(F("\n\nClosing the connection"));
  cc3000.disconnect();

}


// Interrupt is called once a millisecond, looks for any new GPS data, and stores it
SIGNAL(TIMER0_COMPA_vect) {
  char c = GPS.read();
  // if you want to debug, this is a good time to do it!
#ifdef UDR0
  if (GPSECHO)
    if (c) UDR0 = c;  
    // writing direct to UDR0 is much much faster than Serial.print 
    // but only one character can be written at a time. 
#endif
}

void useInterrupt(boolean v) {
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
    usingInterrupt = true;
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
    usingInterrupt = false;
  }
}

uint32_t timer = millis();

void loop()                     // run over and over again
{
  // in case you are not using the interrupt above, you'll
  // need to 'hand query' the GPS, not suggested :(
  if (! usingInterrupt) {
    // read data from the GPS in the 'main loop'
    char c = GPS.read();
    // if you want to debug, this is a good time to do it!
    if (GPSECHO)
      if (c) Serial.print(c);
  }
  
  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    // a tricky thing here is if we print the NMEA sentence, or data
    // we end up not listening and catching other sentences! 
    // so be very wary if using OUTPUT_ALLDATA and trytng to print out data
    //Serial.println(GPS.lastNMEA());   // this also sets the newNMEAreceived() flag to false
  
    if (!GPS.parse(GPS.lastNMEA()))   // this also sets the newNMEAreceived() flag to false
      return;  // we can fail to parse a sentence in which case we should just wait for another
  }

  // if millis() or timer wraps around, we'll just reset it
  if (timer > millis())  timer = millis();

  // approximately every 5 seconds or so, print out the current stats
  if (millis() - timer > 5000) { 
    timer = millis(); // reset the timer
    
    // Serial.print("\nTime: ");
    // Serial.print(GPS.hour, DEC); Serial.print(':');
    // Serial.print(GPS.minute, DEC); Serial.print(':');
    // Serial.print(GPS.seconds, DEC); Serial.print('.');
    // Serial.println(GPS.milliseconds);
    // Serial.print("Date: ");
    // Serial.print(GPS.day, DEC); Serial.print('/');
    // Serial.print(GPS.month, DEC); Serial.print("/20");
    // Serial.println(GPS.year, DEC);
    // Serial.print("Fix: "); Serial.print((int)GPS.fix);
    // Serial.print(" quality: "); Serial.println((int)GPS.fixquality); 
    if (GPS.fix) {
      // Serial.print("Location: ");
      // Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
      // Serial.print(", "); 
      // Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);
      // Serial.print("Location (in degrees, works with Google Maps): ");
      Serial.print(GPS.latitudeDegrees, 4);
      Serial.print(","); 
      Serial.println(GPS.longitudeDegrees, 4);
      
      // Serial.print("Speed (km/h): "); Serial.println(GPS.speed * 1.852);
      // Serial.print("Angle: "); Serial.println(GPS.angle);
      // Serial.print("Altitude: "); Serial.println(GPS.altitude);
      // Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
    }
  }
}
