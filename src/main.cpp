/**
 * A smart weight measuring application using ESP32 and HX711.
 * Author:Muhsin Atto
 * Version: 1.0
 * Date:09/06/2025
 * Repository: https://github.com/kp003919/Demo_FinalVersion.git

 */

// libraries 
#include<WiFiMulti.h>            // wifi 
#include<WiFi.h>       
#include <TM1637.h>             // Display 
#include "HX711.h"              // Load cell 
#include <freeRTOS.h>           // freeRTOS 
#include<semphr.h>              // semaphore handler 
#include <WiFiClient.h>         // client requests
#include <WebServer.h>         // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
#include <WebSocketsServer.h>  // needed for instant communication between client and server through Websockets

// define variables 
// Blyn Cloud template id and name 
#define BLYNK_TEMPLATE_ID "TMPL54GiWGKuC"
#define BLYNK_TEMPLATE_NAME "Muhsin"
#define BLYNK_AUTH_TOKEN "0U5MXMqH_NVYl6XEhvD7EyDtZoQxAXmD"
#define BLYNK_PRINT Serial

//The basic HTML page to show the web interface to show the current weight and tare the scale, we needed. 
String website = "<!DOCTYPE html><html><head><title>SmartScaleMeasuring</title></head><body style='background-color: #EEEEEE;'><span style='color: #003366;'><h1>Displaying the Current Weight</h1><p>The current Weight is: <span id='rand'>-</span></p><p><button type='button' id='BTN_SEND_BACK'>Taring the Scale (Zeroing the Scale)</button></p></span></body><script> var Socket; document.getElementById('BTN_SEND_BACK').addEventListener('click', button_send_back); function init() { Socket = new WebSocket('ws://' + window.location.hostname + ':81/'); Socket.onmessage = function(event) { processCommand(event); }; } function button_send_back() { Socket.send('Taring the Scale'); } function processCommand(event) { document.getElementById('rand').innerHTML = event.data; console.log(event.data); } window.onload = function(event) { init(); }</script></html>";

// define server and socket to send and receive data between clients and the server. 
WebServer  server(80);                              //  the server uses port 80 (standard port for websites)
WebSocketsServer webSocket = WebSocketsServer(81);    // the websocket uses port 81 (standard port for websockets

// Load cell variables 
#define maxScaleValue 5000      // load cell maximum weight is 5k = 5000grams.
#include <BlynkSimpleEsp32.h>  // Blynk in ESP32 

//Display circuit wiring 
#define CLK_PIN    48         // GPIO PIN 48 from the ESP32 MCU is connected to the pin CLK of the Display
#define DIO_PIN    47         // GPIO PIN 47 from the ESP32 MCU is connected to the pin DIO of the Display

#define maxScaleValue 5000    // load cell maximum weight is 5k = 5000grams.
#include <BlynkSimpleEsp32.h>  // Blynk in ESP32 

// HX711 circuit wiring
#define DOUT_PIN  21           // GPIO PIN 21 from the ESP32 MCU is connected to the pin DOUT of the HX711
#define SCK_PIN   20           // GPIO PIN 20 from the ESP32 MCU is connected to the pin SCK  of the HX711

// SSID and password of Wifi connection
// Access Provider (AP) details
#define AP_NAME  "VM1080293"       // AP name 
#define AP_PASS "Omidmuhsin2015"  // AP password 

// task handels for freeRTOS.
TaskHandle_t TaskHandle_1;  // get weight task 
TaskHandle_t TaskHandle_2;  // display weight task 
TaskHandle_t TaskHandle_3;  // web server task
TaskHandle_t TaskHandle_4;  // Blyn Task
SemaphoreHandle_t semaphore; 
const int shared_resource = 3; 

// Load cell reader 
HX711 scaleReader; 
// Blynk timer 
BlynkTimer timer;
// calibration factor to be used. 
float calibration_factor = -396.99; // Calibration factor to get the well known weight 
                                    // This number works for me. 
                                    // The basic idea about calibration factor is to find a value where you 
                                    // could get readings close to your well know readings. 
                                    // This is estimated by using different factors and printing the corresponding readings.                                    
                                  
// Variable to store the HTTP request
String header;
 long static  currentWeight = 0; // current weight 
// Current time 
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 10000; // 10 seconds
// display provider 
TM1637 displayScale(CLK_PIN,DIO_PIN);
WiFiMulti wifiMulti;  // wifi access

//******************************************************************************************** Help functions *********************************************************************

/**

 * Calibration process
 * This function was used to find the calibration factor to get close to the well-known weight.
 * After this value was determined, this funtion was not used in the code.

 */

float getCalibrateFactor()
{
  // Step1: get raw reading with no scale factor set.
   // weight is 0 
    Serial.println("Do not put any weight");
    scaleReader.set_scale();    //no calibration 
    scaleReader.tare();    // removing weight on the scale
    float raw_reading1 = scaleReader.read_average(10); // zero weight reading 
    delay(5000);
    Serial.println("Put on your weight ");
    // my known weight is 50.0g

    float myWeight = 50.0; //y2, y1 =0.
   // step 2: get another raw reading when your weight is on the scale 
    float raw_reading2 = scaleReader.read_average(10); //x2
    delay(5000);
    // factor = (y2 - y1) / (x2 - x1)
    float calibration_factor = (myWeight - 0.0)/(raw_reading2-raw_reading1);
    return calibration_factor;
  
}

/**

 * @Desc: This function returns the current weight.  
 *        Max value is expected to be between 0 and 5000g (5k). 
 * @return:  - If reading > max, error and return 0 
 *           - if reading <= 0, return 0 
 *           - return reading otherwise. 
 */

long  getWeight()
{  
    // is scale ready?
    if (scaleReader.is_ready()) 
      {         
          // set calibration factor  
          // This value just works fine for m in this demo and 
          // then it has been usd to get the well know weights. 
          // calibration factor = getCalibrationFactor()        
          scaleReader.set_scale(calibration_factor);
          // get reading from the load cell          
          long gram_reading = scaleReader.get_units();
          Serial.println(""); 
          Serial.print("Reading: ");
          Serial.println(gram_reading); 
          // reading must be less than max scale value.
          if ( (gram_reading > maxScaleValue) || (gram_reading <0) )
              return 0;                       // overweight 
          else  
              return gram_reading;           // return the reading 
      } 
      else 
      {
          Serial.println("Scale is not ready yet...");
          delay(1000); 
          return 0;
      } 
}

/**
 * @Desc:Communicate between clients and a server. 
 * @para num     a client number to be handeled 
 * @para type    a type of the vent to be handleed. 
 * @para payload a data received from a client. 
 * @length       a length of the data recieved from a client. 
 * @reference[Mo Thunderz].
 */
void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) 
{      
  switch (type) {                                   
    case WStype_DISCONNECTED:                         // if a client is disconnected, then type == WStype_DISCONNECTED
      Serial.println("Client " + String(num) + " disconnected");
      break;
    case WStype_CONNECTED:                            // if a client is connected, then type == WStype_CONNECTED
      Serial.println("Client " + String(num) + " connected");
      // optionally you can add code here what to do when connected
      break;
    case WStype_TEXT:                          
        // Client send request to tare the scale( we assume we only have one button)
        // we could receive different data. 
        Serial.println("Client " + String(num) + "requesting to tare the scale");
        scaleReader.tare();
       Serial.println("Taring done:)");
      break;
  }
}


/**
 * Desc: Reseting the display with ----.
 *LED will display ---- to display the accurate value of the measured weight. 
 *For example: if value 240 is returned then the display will show 240-, not 2400.
 */

void resetDisplay()
{
    displayScale.display("----"); // reset display
}

/**
 * Desc: Display the given weight to the 4 digits 7-segment display. 
 * @para  weightVal a value to be displayed. 
 */
void displayWeight(long weightVal)
{ 
        // reset the display first so that we can have a clear display.
        // when digit is not displayed, it is assumed to be "-". 
        resetDisplay();
        displayScale.display(weightVal);
}

//*********************************************************************************************** Blynk Cloud ******************************

/**
 * @Desc:This function sends data to the pins (V0 and V1) of the Blynk. 
 * Updating the current weight to the Blynk cloud over a period of time. 
 * Reference [blynk.io]. 
 */
void runBlynk()
{
  Blynk.virtualWrite(V0,currentWeight); 
  Blynk.virtualWrite(V1,currentWeight);
  delay(1000); 
}


//********************************************************************* freeRTOS Tasks ********************************************************
/**
 * Task1:  getting weight from the load cell. 
 * FreeRTOS is used to run this task and get the current weight from the load cell.  
 */
void Task1( void *pvParameters )
{  
  while (1)
  {  
      // taking the semaphore 
      xSemaphoreTake(semaphore,portMAX_DELAY);    
      currentWeight = getWeight();   // current weight 
      //releasing the semaphore. 
      xSemaphoreGive(semaphore);  
      vTaskDelay(500/ portTICK_PERIOD_MS); // poll over 500ms, as requested. 
  }
}

/**
 * Task2: dislpaly the current weight to the LED. 
 */
void Task2( void *pvParameters )
{  
   while(1)
  { 
      // taking the semaphore 
       xSemaphoreTake(semaphore,portMAX_DELAY);
       Serial.println("Task2");
       Serial.println(currentWeight); 
       displayWeight(currentWeight);   
       //releasing the semaphore.  
       xSemaphoreGive(semaphore); 
       vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per (n) second.
  }  
}

/**
 * Task3: runs web server 
 * Web server is ready for any client requests, if there is any. 
 * Web UI is used to display the current weight or tare the scale
 * This web interface (client) can be accessed using this IP address of the server. 
 * After program is excuted, you can see the IP address from the Serial Monitor.  
 * Copy this IP address and paste it into your browser. 
 */
void Task3( void *pvParameters )
{   
  while (1)
  {
      // taking the semaphore 
      xSemaphoreTake(semaphore,portMAX_DELAY);
      Serial.println("Task3");
      String str = String(currentWeight);     // get the current weight
      int str_len = str.length() + 1;         // convert this number into an array of chars.           
      char char_array[str_len];           
      str.toCharArray(char_array, str_len);   // convert to char array
      webSocket.broadcastTXT(char_array);     // send char_array to clients(broadcast). 
      xSemaphoreGive(semaphore);  //releasing the semaphore. 
      vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per 1 second.
  }
}

/**
 * Sends the current weight to the Blynk using pins V0 and V1.
 * Blynk account must be created and a new template must be designed before using this function. 
 * The current weght is displayed on the Blynk cloud over time(5 second).  
 * Open your Blynk account and you should be able to see the weight. 
 */
void Task4( void *pvParameters )
{   
  while (1)
  {
    // this task take the semaphore 
     xSemaphoreTake(semaphore,portMAX_DELAY);
     Serial.println("Task4");
     //Blynk.run();
     //timer.run();
     xSemaphoreGive(semaphore);  //releasing the semaphore. 
     vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per (n) second.
  }
}



//****************************************************************************** Sep up function  *****************************************************************************

// setup function
void setup() 
{
    // conenct to the available wifi using your AP name and password. 
    WiFi.begin(AP_NAME,AP_PASS); // access wifi 
  
    // set the speed to transfer data in the serial communication. 
    Serial.begin(115200);   // speed = 115200
  
    // 1- Initializing the display (4 digits 7 segment display) 
    Serial.println("Initializing the LED display");
    displayScale.init();
    displayScale.setBrightness(5); // set the brightness (0:dimmest, 7:brightest) 

    //2- Load cell setting and initilization 
    Serial.println("Initializing the scale");
    scaleReader.begin(DOUT_PIN,SCK_PIN);   
  
    scaleReader.set_scale();    //no calibration 
    scaleReader.tare();         // removing any weight on the scale.

    // create a new semaphpre and check if it has been created.
    semaphore = xSemaphoreCreateMutex();
    if (semaphore == NULL)
    {
         Serial.println("Semaphore could not be created!");
    }

   // wifi connection 
    if(WiFi.status() != WL_CONNECTED) 
    {        
        Serial.println("..... Connecting.... \n");
        delay(1000);   
    }  
    
     // This is your local IP address. 
     Serial.print("IP address: ");
     Serial.println(WiFi.localIP());  // you need this IP to access to the web interface
    
    //3 start the web server and sockets.
     server.on("/", []() {                               
     server.send(200, "text/html", website);              //  send out the HTML string "webpage" to the client
      });
      server.begin();                                     // start server  
      webSocket.begin();                                  // start websocket
      webSocket.onEvent(webSocketEvent);                  //What needs to be done at the server, where data received back from clients. 
   
    //4- Blynk interaction
     //Blynk.begin(BLYNK_AUTH_TOKEN, AP_NAME, AP_PASS);
     // Blynck starts here. 
     timer.setInterval(1000L, runBlynk); 
     //timer.setInterval(1000L, runBlynk);       
   
     //5- Creating different tasks(getting weight, displaying the current weight, web server and blynk interaction.
      xTaskCreate(Task1, "get Weight", 10000, NULL, 1, &TaskHandle_1); // getting weight 
      xTaskCreate(Task2, "Display 1", 10000,NULL, 1, &TaskHandle_2);  
      xTaskCreate(Task3, "Web Server", 10000,NULL, 2, &TaskHandle_3);
     // xTaskCreate(Task4, "Blynk Cloud", 10000,NULL,3, &TaskHandle_4);

      Serial.println("....Starting .... \n");  
}

//**************************************************************************** main function (loop) *****************************************************

// loop forever (empty).
void loop() 
{   
   // run server forever. 
   server.handleClient();  // webserver methode that handles all Client
   // loop the sokets for any communication. 
   webSocket.loop();   
}
// end of demo 