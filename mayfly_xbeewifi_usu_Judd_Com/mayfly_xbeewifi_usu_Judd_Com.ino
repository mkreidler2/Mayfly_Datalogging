/**************************************************************************
Mayfly_XBeeWiFi_USU.ino
Written By:  Jeff Horsburgh (jeff.horsburgh@usu.edu)
Updated By:  Kenny Fryar-Ludwig (kenny.fryarludwig@usu.edu)
Judd Communcations Sensor Added By: Mason Kreidler (mkreidler@utah.edu) 
Creation Date: 6/3/2016
Updated: 12/10/2017
Development Environment: Arduino 1.6.9
Hardware Platform: Stroud Water Resources Mayfly Arduino Datalogger
Radio Module: XBee S6b WiFi module.

This sketch is a code for posting Judd Communications distance sensor data to the EnviroDIY Water Quality
data portal (http://data.envirodiy.org) using a Mayfly Arduino board and an 
XBee Wifi module. It also uses the temperature values from 
the Mayfly's real time clock and POSTs them to http://data.envirodiy.org. 
This sketch could easily be modified to post any sensor measurements to a stream
at http://data.envirodiy.org that has been configured to accept them.

This sketch was adapted from Jim Lindblom's example at:

https://learn.sparkfun.com/tutorials/internet-datalogging-with-arduino-and-xbee-wifi

Assumptions:
1. The XBee WiFi module has must be configured correctly to connect to the
wireless network prior to running this sketch.
2. The Mayfly has been registered at http://data.envirodiy.org and the sensor 
has been configured. In this example, only temperature is used.

DISCLAIMER:
THIS CODE IS PROVIDED "AS IS" - NO WARRANTY IS GIVEN.
**************************************************************************/


// -----------------------------------------------
// Note: All 'Serial.print' statements can be
// removed if they are not desired - used for 
// debugging only
// -----------------------------------------------


// -----------------------------------------------
// 1. Include all sensors and necessary files here
// -----------------------------------------------
#include <Wire.h>
#include <SD.h>
#include "Sodaq_DS3231.h"

// -----------------------------------------------
// 2. Device registration and sampling features
// -----------------------------------------------
// Register your site and get these tokens from data.envirodiy.org
const String REGISTRATION_TOKEN = "29612116-01d0-4086-b858-835ad4eaae25";
const String SAMPLING_FEATURE = "4410ba27-aca2-4994-a4d4-6ea5fa26b17e";

// -----------------------------------------------
// 3. WebSDL Endpoints for POST requests
// -----------------------------------------------
const String HOST_ADDRESS = "data.envirodiy.org";
const String API_ENDPOINT = "/api/data-stream/";

// -----------------------------------------------
// 4. Saving File Name & Misc. Options
// -----------------------------------------------
#define FILE_NAME "JuddNew.txt" // specify the file name to record to (do not include underscores)
#define DATA_HEADER "DateTime, UTC_Offset, Judd_Water_Depth, Judd_Temp, MayFly_Temp"
#define LOGGERNAME "Mayfly microSD Card Reader"
#define UPDATE_RATE 30000 // milliseconds - 30000 = 30 seconds
#define MAIN_LOOP_DELAY 30000 // milliseconds
#define COMMAND_TIMEOUT 10000 // ms (5000 ms = 5 s)
const int TIME_ZONE = -6;

// -----------------------------------------------
// 5. Board setup info
// -----------------------------------------------
#define XB_BAUD 9600 // XBee BAUD rate (9600 is default)
#define SERIAL_BAUD 57600 // Serial port BAUD rate
#define SD_SS_PIN 12 // the pin number for the SD card reader
// -----------------------------------------------
// 6. Global variables 
// -----------------------------------------------
unsigned long lastUpdate = 0; // Keep track of last update time
Sodaq_DS3231 sodaq;           // This is used for some board functions
size_t sensorCount = 0;       // Keep this at 0 - it'll get set properly in the setup() function
float ONBOARD_TEMPERATURE = 0;

// for the Judd depth sensor 
int JuddsignalPin = 5;   // Digital port 5 for the Judd logger analog read - green
int JuddechoPin1 = 4;    // Analog port 4 for the Judd logger analog read - white
int JuddechoPin2 = 5;    // Analog port 5 for the Judd logger analog read - brown
int Judd_Reference_Distance = 46; // need to change this depending on the distance without water
float Judd_Distance;
float Judd_Depth;
float Judd_Temperature;
String Judd_Data_Display; // to check if readings are working

int led = 8; // temporary

enum HTTP_RESPONSE
{
    HTTP_FAILURE = 0,
    HTTP_SUCCESS,
    HTTP_TIMEOUT,
    HTTP_SERVER_ERROR,
    HTTP_REDIRECT,
    HTTP_OTHER
};

// Used to flush out the buffer after a post request.
// Removing this may cause communication issues. If you
// prefer to not see the std::out, remove the print statement
void printRemainingChars(int timeDelay = 1, int timeout = 5000)
{
    while (timeout-- > 0 && Serial1.available() > 0)
    {
        while (Serial1.available() > 0)
        {
            char netChar = Serial1.read();
            Serial.print(netChar);
            delay(timeDelay);
        }

        delay(timeDelay);
    }

    Serial1.flush();
}

// Used only for debugging - can be removed
void printPostResult(int result)
{
    switch (result)
    {
        case HTTP_SUCCESS:
        {
            Serial.println("\nSucessfully sent data to " + HOST_ADDRESS + "\n");
        }
        break;

        case HTTP_FAILURE:
        {
            Serial.println("\nFailed to send data to " + HOST_ADDRESS + "\n");
        }
        break;

        case HTTP_TIMEOUT:
        {
            Serial.println("\nRequest to " + HOST_ADDRESS + " timed out, no response from server.\n");
        }
        break;

        case HTTP_REDIRECT:
        {
            Serial.println("\nRequest to " + HOST_ADDRESS + " was redirected.\n");
        }
        break;

        case HTTP_SERVER_ERROR:
        {
            Serial.println("\nRequest to " + HOST_ADDRESS + " caused an internal server error.\n");
        }
        break;

        default:
        {
            Serial.println("\nAn unknown error has occured, and we're pretty confused\n");
        }
    }
}

// This function makes an HTTP connection to the server and POSTs data
int postData(String requestString, bool redirected = false)
{
    Serial.println("Checking for remaining data in the buffer");
    printRemainingChars(5, 5000);
    Serial.println("\n");

    HTTP_RESPONSE result = HTTP_OTHER;

    Serial1.flush();
    Serial1.print(requestString.c_str());
    Serial1.flush();


    Serial.flush();
    Serial.println(" -- Request -- ");
    Serial.print(requestString.c_str());
    Serial.flush();

    // Add a brief delay for at least the first 12 characters of the HTTP response
    int timeout = COMMAND_TIMEOUT;
    while ((timeout-- > 0) && Serial1.available() > 0)
    {
        delay(2);
    }

    // Process the HTTP response
    if (timeout > 0 || Serial1.available() >= 12)
    {
        char response[10];
        char code[4];
        memset(response, '\0', 10);
        memset(code, '\0', 4);

        int responseBytes = Serial1.readBytes(response, 9);
        int codeBytes = Serial1.readBytes(code, 3);
        Serial.println("\n -- Response -- ");
        Serial.print(response);
        Serial.println(code);

        printRemainingChars(5, 5000);

        // Check the response to see if it was successful
        if (memcmp(response, "HTTP/1.0 ", responseBytes) == 0
            || memcmp(response, "HTTP/1.1 ", responseBytes) == 0)
        {
            if (memcmp(code, "200", codeBytes) == 0
                || memcmp(code, "201", codeBytes) == 0)
            {
                // The first 12 characters of the response indicate "HTTP/1.1 200" which is success
                result = HTTP_SUCCESS;
            }
            else if (memcmp(code, "302", codeBytes) == 0)
            {
                result = HTTP_REDIRECT;
            }
            else if (memcmp(code, "400", codeBytes) == 0
                || memcmp(code, "404", codeBytes) == 0)
            {
                result = HTTP_FAILURE;
            }
            else if (memcmp(code, "500", codeBytes) == 0)
            {
                result = HTTP_SERVER_ERROR;
            }
        }
    }
    else // Otherwise timeout, no response from server
    {
        result = HTTP_TIMEOUT;
    }

    return result;
}

// This function generates the POST request that gets sent to data.envirodiy.org
String generatePostRequest(String dataString)
{
    String request = "POST " + API_ENDPOINT + " HTTP/1.1\r\n";
    request += "Host: " + HOST_ADDRESS + "\r\n";
    request += "TOKEN: " + REGISTRATION_TOKEN + "\r\n";
    request += "Cache-Control: no-cache\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(dataString.length() + 3) + "\r\n";
    request += "\r\n";
    request += dataString;
    request += "\r\n\r\n";

    return request;
}

// This function updates the values for any connected sensors. Need to add code for
// Any sensors connected - this example only uses temperature.
bool updateAllSensors()
{
    // Get the temperature from the Mayfly's real time clock and convert to Farenheit
    rtc.convertTemperature();  //convert current temperature into registers
    float tempVal = rtc.getTemperature();
    // ONBOARD_TEMPERATURE = (tempVal * 9.0 / 5.0) + 32.0; // Convert to farenheit
    ONBOARD_TEMPERATURE = tempVal;

    // Take readings from Judd Sensor
    // Activate the sensor
    digitalWrite(JuddsignalPin, HIGH);
    // Delay 0.8 seconds to take the temperature reading
    delay(800);
    float TempDiff = analogRead(JuddechoPin1)-analogRead(JuddechoPin2);
    // Delay another 1.8 seconds to take the distance reading
    delay(1800);
    float DistanceDiff = analogRead(JuddechoPin1)-analogRead(JuddechoPin2);
    //Turn off the sensor
    digitalWrite(JuddsignalPin, LOW);
    //Convert the outputs from bits to volts
    Judd_Temperature = TempDiff*(3.3/1023)*200 - 273;
    Judd_Distance = DistanceDiff*(3.3/1023)*500;  // multiply by 500, the correction factor to cm (0.5 in judd code)
    // Judd_Distance = DistanceDiff*(3.3/1023)*196.85; // for distance in inches
    Judd_Depth = Judd_Reference_Distance - Judd_Distance; // convert the distance read into depth of water (reference must be set at top of code)
    DateTime currDateTime = sodaq.now();
    Judd_Data_Display = getDateTime_print() + ", " + TIME_ZONE + ", " + String(Judd_Depth) + ", " + String(Judd_Temperature) + ", " +  String(ONBOARD_TEMPERATURE); // to check if readings are working

    return true;
}

// This function generates the JSON data string that becomes the body of the POST request
// For now, the Result UUID is hard coded here
// TODO:  Move the Result UUID somewhere easier to configure.
String generateSensorDataString(void)
{
    String jsonString = "{ ";
    jsonString += "\"sampling_feature\": \"" + SAMPLING_FEATURE + "\", ";
    jsonString += "\"timestamp\": \"" + getDateTime() + "\", ";
    jsonString += "\"6f36abf5-7914-43a7-b73c-a9d666da01bf\": " + String(ONBOARD_TEMPERATURE);
    jsonString += " }";
    return jsonString;
}

String generateSensorDataStringJuddDistance(void)
{
    String jsonString = "{ ";
    jsonString += "\"sampling_feature\": \"" + SAMPLING_FEATURE + "\", ";
    jsonString += "\"timestamp\": \"" + getDateTime() + "\", ";
    jsonString += "\"7e3924d3-859f-4833-97a5-67277d8b5890\": " + String(Judd_Distance);
    jsonString += " }";
    return jsonString;
}

String generateSensorDataStringJuddTemp(void)
{
    String jsonString = "{ ";
    jsonString += "\"sampling_feature\": \"" + SAMPLING_FEATURE + "\", ";
    jsonString += "\"timestamp\": \"" + getDateTime() + "\", ";
    jsonString += "\"ec74c3d2-f9ae-4e3d-88f9-809914f532b1\": " + String(Judd_Temperature);
    jsonString += " }";
    return jsonString;
}

// This function returns a simple datetime from the realtime clock as an ISO 8601 formated string (for printing to file)
String getDateTime_print(void)
{
    String dateTimePrint;
    DateTime currDateTime = sodaq.now();
    // Convert it to a string
    currDateTime.addToString(dateTimePrint);
    
    return dateTimePrint;
}

// This function returns the datetime from the realtime clock as an ISO 8601 formated string (for data.envirodiy)
String getDateTime(void)
{
    String dateTimeStr;
    DateTime currDateTime = sodaq.now();
    // Convert it to a string
    currDateTime.addToString(dateTimeStr);
    dateTimeStr.replace(F(" "), F("T"));
    String tzString = String(TIME_ZONE);
    if (-24 <= TIME_ZONE && TIME_ZONE <= -10)
    {
        tzString += F(":00");
    }
    else if (-10 < TIME_ZONE && TIME_ZONE < 0)
    {
        tzString = tzString.substring(0,1) + F("0") + tzString.substring(1,2) + F(":00");
    }
    else if (TIME_ZONE == 0)
    {
        tzString = F("Z");
    }
    else if (0 < TIME_ZONE && TIME_ZONE < 10)
    {
        tzString = "+0" + tzString + F(":00");
    }
    else if (10 <= TIME_ZONE && TIME_ZONE <= 24)
    {
        tzString = "+" + tzString + F(":00");
    }
    dateTimeStr += tzString;
    
    return dateTimeStr;
}

void setupLogFile()
{
  //Initialise the SD card
  if (!SD.begin(SD_SS_PIN))
  {
    Serial.println("Error: SD card failed to initialise or is missing.");
    //Hang
  //  while (true); 
  }
  
  //Check if the file already exists
  bool oldFile = SD.exists(FILE_NAME);  
  
  //Open the file in write mode
  File logFile = SD.open(FILE_NAME, FILE_WRITE);
  
  //Add header information if the file did not already exist
  if (!oldFile)
  {
    logFile.println(LOGGERNAME);
    logFile.println(DATA_HEADER);
  }
  
  //Close the file to save it
  logFile.close();  
}

void logData(String rec)
{
  //Re-open the file
  File logFile = SD.open(FILE_NAME, FILE_WRITE);
  
  //Write the CSV data
  logFile.println(rec);
  
  //Close the file to save it
  logFile.close();  
}

// Main setup function
void setup()
{ 
    //sodaq.setEpoch(1512395280);  // Use this to set the current time, set to current unix epoch
    pinMode(JuddsignalPin, OUTPUT); // set the digital pin for Judd activation
    pinMode(JuddechoPin1, INPUT); // primary analog pin for reading voltage
    pinMode(JuddechoPin2, INPUT); // secondary analog pin for reading voltage differential

    pinMode(led, OUTPUT); //temporary: turns on light
    digitalWrite(led,HIGH); //temporary: turns on light
    
    Serial.begin(SERIAL_BAUD);   // Start the serial connections
    Serial1.begin(XB_BAUD);      // XBee hardware serial connection
    Serial.println("WebSDL Device: EnviroDIY Mayfly\n");
    
    setupLogFile();//Initialise log file
    Serial.println(DATA_HEADER);
}

void loop()
{
    // Check to see if it is time to post the data to the server
    if (millis() > (lastUpdate + UPDATE_RATE))
    {
        lastUpdate = millis();
        Serial.println("\n---\n---\n");
        logData(Judd_Data_Display);
        //Serial.println(Judd_Data_Display); // to check if readings are working
        updateAllSensors(); // get the sensor value(s), store as string
        String request = generatePostRequest(generateSensorDataString());
        int result = postData(request);
        printPostResult(result);
        // Upload and print the judd distance outcome
        String request2 = generatePostRequest(generateSensorDataStringJuddDistance());
        int result2 = postData(request2);
        printPostResult(result2);
        // Upload and print the judd temperature outcome
        String request3 = generatePostRequest(generateSensorDataStringJuddTemp());
        int result3 = postData(request3);
        printPostResult(result3);
    }

    delay(MAIN_LOOP_DELAY);
}

