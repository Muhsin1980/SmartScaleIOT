/**
 * Smart Scale IoT Project
 *  This is a simple smart scale application that uses ESP32 and HX711 to measure weight.
 * It uses a load cell to measure the weight and a 4 digits 7-segment display to show the weight.
 * It displays the current weight on a 4 digits 7-segment display and sends the weight to the Blynk cloud.  
 * It also provides a simple web interface to display the current weight and tare the scale.
 * The web interface can be accessed using the IP address of the ESP32. 
 * It uses freeRTOS to create different tasks for getting the weight, displaying the weight, handling the web server, and Blynk cloud interaction.
 * The application uses a semaphore to ensure that only one task can access the shared resource (load cell and display) at a time.    
 * 
 * @author: Muhsin Atto
 * @email: darenhaji@gmail.com
 * @version: 1.0
 * @date Date:09/06/2025
 * Repository: https://github.com/Muhsin1980/SmartScaleIOT.git
 */

// libraries needed for the project
#include <Arduino.h>            // Arduino library    
#include<WiFiMulti.h>            // WiFiMulti library to connect to multiple WiFi networks 
#include<WiFi.h>                 // WiFi library to connect to WiFi networks
#include <TM1637.h>              // 4 digits 7-segment display library
#include "HX711.h"              // HX711 library to read the load cell
#include <freeRTOS.h>           // freeRTOS library to create tasks and handle multitasking
#include<semphr.h>              // semaphore library to handle shared resources in freeRTOS
#include <WiFiClient.h>         // WiFiClient library to create a client to connect to the WiFi network
#include <WebServer.h>         // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
#include <WebSocketsServer.h>  // needed for instant communication between client and server through Websockets
#include<ArduinoJson.h>       // ArduinoJson library to handle JSON data.


// Blynk Cloud configuration
// Blynk Cloud is used to send the current weight to the Blynk cloud and display it on the Blynk app.   
// You need to create a Blynk account and a template to use this feature.
// To use Blynk, you need to install the Blynk library in your PltformIO.     
// Blyn Cloud template id and name 
#define BLYNK_TEMPLATE_ID   "TMPL54GiWGKuC"
#define BLYNK_TEMPLATE_NAME "Muhsin"
#define BLYNK_AUTH_TOKEN    "0U5MXMqH_NVYl6XEhvD7EyDtZoQxAXmD"
/*
#define BLYNK_TEMPLATE_ID "TMPL5PU2--geM"
#define BLYNK_TEMPLATE_NAME "Smart Scale Demo"
#define BLYNK_AUTH_TOKEN "KsoajqCRKWcJSJf3TdZYekqVoZ2NPdNq"
#define BLYNK_PRINT Serial
*/
// Include the Blynk library for ESP32
// @note If I moved this to above, I will get an error.
#include <BlynkSimpleEsp32.h>  // Blynk library for ESP32. 

// Display circuit wiring
#define CLK_PIN    48         // GPIO PIN 48 from the ESP32 MCU is connected to the pin CLK of the Display
#define DIO_PIN    47         // GPIO PIN 47 from the ESP32 MCU is connected to the pin DIO of the Display

// HX711 circuit wiring
#define DOUT_PIN  21           // GPIO PIN 21 from the ESP32 MCU is connected to the pin DOUT of the HX711
#define SCK_PIN   20           // GPIO PIN 20 from the ESP32 MCU is connected to the pin SCK  of the HX711
#define maxScaleValue 5000     // load cell maximum weight is 5k = 5000grams.

// WiFi configuration
// You need to replace these with your own WiFi network name and password.  
// This is the WiFi network that the ESP32 will connect to.
#define AP_NAME  "VM1080293"       // AP name 
#define AP_PASS "Omidmuhsin2015"  // AP password 

/* 
  The basic HTML page to show the current weight and tare the scale, when needed. 
  The whole html is contained (compressed) in a string variable called "website". 
  https://www.textfixer.com/html/compress-html-compression.php
  We can use this string variable to send the HTML page to the client when requested.
  If you want to change the HTML page, you can change the string variable "website".
  The HTML page contains a button to tare the scale and a span to display the current weight.
  The button will send a message to the server to tare the scale, and the server will respond 
  with the current weight.
  The span with id "rand" will be updated with the current weight received from the server.
  
  @note This is a simple web interface to interact with the scale. However, we can design our own web interface 
  using seperate HTML, CSS, and JavaScript files, for real projects, to build better web pages. We can also 
  use JSON to create a more complex web interface with more features. I could even have a chat graph to show
  the current weight over time. However, as this is a simple demo, I decided to use a simple HTML page to show the 
  current weight and tare the scale.
  */
String website = "<!DOCTYPE html><html><head><title>SmartScaleMeasuring</title></head><body style='background-color: #EEEEEE;'><span style='color: #003366;'><h1>Displaying the Current Weight</h1><p>The current Weight is: <span id='rand'>-</span></p><p><button type='button' id='BTN_SEND_BACK'>Taring the Scale (Zeroing the Scale)</button></p></span></body><script> var Socket; document.getElementById('BTN_SEND_BACK').addEventListener('click', button_send_back); function init() { Socket = new WebSocket('ws://' + window.location.hostname + ':81/'); Socket.onmessage = function(event) { processCommand(event); }; } function button_send_back() { Socket.send('Taring the Scale'); } function processCommand(event) { document.getElementById('rand').innerHTML = event.data; console.log(event.data); } window.onload = function(event) { init(); }</script></html>";

// Web server and web socket configuration 
WebServer  server(80);                                //  the server uses port 80 (standard port for websites)
WebSocketsServer webSocket = WebSocketsServer(81);    // the websocket uses port 81 (standard port for websockets
StaticJsonDocument<200> jsonDoc_tx;                      // JSON document to hold the data to be sent to the Blynk cloud and web server
// FreeRTOS tasks and semaphore configuration
// We will create 4 tasks to handle the different functionalities of the application.
StaticJsonDocument<200> jsonDoc_rx;                     // JSON document to hold the data received from the Blynk cloud and web server


TaskHandle_t TaskHandle_1;  // get weight task 
TaskHandle_t TaskHandle_2;  // display weight task 
TaskHandle_t TaskHandle_3;  // web server task
TaskHandle_t TaskHandle_4;  // Blyn Task
SemaphoreHandle_t semaphore; 
const int shared_resource = 3; 

// HX711 scale reader object
// Defining a scale reader object to read the load cell
// This variable reads the load cell using the HX711 chip
// HX711 is a chip that converts the analog signal from the load cell to a 
// digital signal that can be read by the ESP32
HX711 scaleReader;

// Blynk timer object to run the Blynk cloud interaction
// BlynkTimer is used to run the Blynk cloud interaction every second
BlynkTimer timer;

// Calibration factor for the load cell
// This numbr is used to convert the raw reading from the load cell to the actual weight.
// See the getCalibrateFactor() function for more details on how to calculate this factor.
float calibration_factor = -396.99; // Calibration factor to get the well known weight 
                                    // This number works for me.  You can change this number to 
                                    // get the correct weight for your load cell.                      
                                  
// This variable is used to store the current weight.
 long static  currentWeight = 0; // current weight 

// This is the 4 digits 7-segment display object 
TM1637 displayScale(CLK_PIN,DIO_PIN);
// This is the WiFiMulti object to connect to the WiFi network.
WiFiMulti wifiMulti;  // wifi access

//******************************************* Help functions *********************************************************************

/** 
 *  @brif This function is used to get the calibration factor for the load cell.  
  *         The calibration factor is used to convert the raw reading from the load cell to the actual weight.
  *         The calibration factor is calculated by taking two readings from the load cell:
  *           - The first reading is taken with no weight on the scale (y1 = 0).
  *           - The second reading is taken with a known weight on the scale (y2 = 50.0g).
  *        The calibration factor is then calculated using the formula:
  *        calibration_factor = (y2 - y1) / (x2 - x1)
  *        where:          
  *           - y1 is the first reading (0.0g)
  *           - y2 is the second reading (50.0g)
  *           - x1 is the first raw reading (raw_reading1)
  *           - x2 is the second raw reading (raw_reading2)
  *        The function returns the calibration factor as a float value.
  *@note: This function assumes that the load cell is connected to the ESP32 using the HX711 library.
  *@para: This function does not take any parameters.  
  *@return: The calibration factor as a float value.
 **/

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
 * @brief function is used to reset the display to show "----".
 *          It is called when the display needs to be cleared or reset.  
 * @para: This function does not take any parameters.
 * @note: This function is used to reset the display to show "----" on the 4 digits 7-segment display.
 *        It is called when the display needs to be cleared or reset.
 *        It is used to ensure that the display is clear before showing the current weight.
 */
void resetDisplay()
{   
    // This function is used to reset the display to show "----"
    displayScale.display("----"); 
}

/**
  * @brief  This function is used to get the current weight from the load cell.
  * @details       It checks if the scale is ready, sets the calibration factor, and gets the reading from the load cell. 
  *        If the reading is greater than the maximum scale value or less than or equal to zero, it returns zero.
  *        If the reading is within the valid range, it returns the reading (in grams). 
  * @para: This function does not take any parameters.
  * @return: The current weight as a long value.
  * @note: This function assumes that the load cell is connected to the ESP32 using the HX711 library.
  *        If you see an unexpected weight (less than 0) for a long time, then you can reset the ESP32 chip. 
  *        After this, you should see the expected weight on the display and in the serial monitor. 
 */

long  getWeight()
{  
  // is scale ready?
  if (scaleReader.is_ready()) 
  {          
    // set calibration factor  
    // You can change this value to get the correct weight for your load cell.    
    // see getCalibrateFactor() function for more details on how to calculate this factor.  
    scaleReader.set_scale(calibration_factor);
    // get reading from the load cell          
    long gram_reading = scaleReader.get_units();
    Serial.println(""); 
    Serial.print("Reading: ");
    Serial.println(gram_reading); 
    // reading must be less than max scale value.
    if ( (gram_reading > maxScaleValue) || (gram_reading <0) )
      {
        Serial.println("Overweight!"); // if the reading is greater than the max scale value, then it is overweight.
        resetDisplay();                // reset the display to show "----"
        Serial.println("Resetting the display to show ----");
        delay(1000);                   // wait for 1 second      
        return 0;                       // overweight 
      }
    else
      {
        // if the reading is within the valid range, then return the reading.                  
        return gram_reading;           // return the reading 
      }
  } 
  else 
  {    
    // This means that the scale is not ready to read the weight.
    // It could be due to a bad connection or the esp32 is not powered on.
    // You can check the connections and power supply to the scale.
    // If the scale is not ready, then return 0.
    resetDisplay();                // reset display
    // print a message to the serial monitor to indicate that the scale is not ready yet. 
    Serial.println("Scale is not ready yet...");
    // wait for 1 second before returning again. 
    delay(1000); 
    // return 0 to indicate that the scale is not ready.
    return 0;
  } 
}

/**
 * @brief This function is used to handle the web socket events.
 * @details  It is called when a client connects, disconnects, or sends a message to the server.  
 *       It handles the following events:
 *       - WStype_DISCONNECTED: When a client disconnects, it prints a message to the serial monitor. 
 *       - WStype_CONNECTED: When a client connects, it prints a message to the serial monitor.
 *       - WStype_TEXT: a client sends a message = "Taring the Scale"  to the server, server tares the
 *                   scale and prints a message to the serial monitor.
 * @note currently, we assume that we only have one button to tare the scale.
 *       This function is used to communicate between clients and the server using web sockets.
 *       It is called by the webSocket.onEvent() function in the setup() function.  
 * 
 * @param num: The client number that sent the message.
 * @param type: The type of event that occurred (WStype_DISCONNECTED, WStype_CONNECTED, or WStype_TEXT).
 * @param payload: The payload of the message sent by the client.
 * @param length: The length of the payload.
 * @note: This function is used to handle web socket events in the ESP32 using the WebSocketsServer library.
 * @reference: This task is inspired by the work of [Mo Thunderz](https://www.youtube.com/@MoThunderz).
 */

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) 
{     
  // check the type of event that occurred and handle it accordingly. 
  switch (type) {    
    // handle the different types of events
    case WStype_ERROR:          // if there is an error, then type == WStype_ERROR
      Serial.println("Error on client " + String(num));
      break;                                 
    case WStype_DISCONNECTED:   // if a client is disconnected, then type == WStype_DISCONNECTED
      Serial.println("Client " + String(num) + " disconnected");
      break;
    case WStype_CONNECTED:   // if a client is connected, then type == WStype_CONNECTED
      Serial.println("Client " + String(num) + " connected");
      // optionally you can add code here what to do when connected
      break;
    case WStype_TEXT:                          
      // a clients sends a request to tare the scale( we assume we only have one button)
      // we could receive different data. 
      // if we receive any other data, we can handle it accordingly.
      // This could be the case when we have multiple buttons or actions. 
      DeserializationError error = deserializeJson(jsonDoc_rx, payload, length); // deserialize the payload to a JSON document               
      if (error) 
      {
        Serial.println("Failed to deserialize JSON: " + String(error.c_str()));
        return; // exit the function if there is an error
      }   
      else
      {
        // print the received JSON document to the serial monitor for debugging purposes.
        const char *str = jsonDoc_rx["rand"]; // get the message from the JSON document
        Serial.println("Received JSON: " + String(str[0]));
        Serial.println("Client " + String(num) + "requesting to tare the scale");
        scaleReader.tare(); // tare the scale
        Serial.println("Taring done:)");
      }
     
      break;
  }
}


/**
 * @brief   This function is used to display the current weight on the 4 digits 7-segment display.
 * @details takes the weight value as a parameter and displays it on the display. 
 *          It is called when the current weight needs to be displayed on the display.
 *          It resets the display first to ensure that the display is clear before showing the current weight. 
 *          It is used to display the current weight on the display in a human-readable format.  
 * @param weightVal: The weight value to be displayed on the display.
 * @returns: This function does not return any value.
  */
void displayWeight(long weightVal)
{       
    // reset the display first so that we can have a clear display.
    // when digit is not displayed, it is assumed to be "-". 
    // This is done to ensure that the display is clear before showing the current weight.
    Serial.println("Displaying the current weight on the display"); 
    resetDisplay();
    displayScale.display(weightVal);
}

//*********************************************************************************************** Blynk Cloud ******************************

/**
 * @brief function is used to run the Blynk cloud interaction.
 * @details sends the current weight to the Blynk cloud using virtual pins V0 and V1.   
 *          It is called every second to update the current weight on the Blynk cloud.
 * @para This function does not take any parameters.
 * @returns: This function does not return any value. *  
 **/
void runBlynk()
{
  // send the current weight to the Blynk cloud using virtual pins V0 and V1
  Serial.println("Sending the current weight to the Blynk cloud");  
  Blynk.virtualWrite(V0,currentWeight);   // send the current weight to the Blynk cloud using virtual pin V0    
  Blynk.virtualWrite(V1,currentWeight);   // send the current weight to the Blynk cloud using virtual pin V1
  Serial.println("Current weight sent to the Blynk cloud"); 
  //wait for 1 second before sending the next weight. 
  delay(1000); 
}


//********************************************************************* freeRTOS Tasks ********************************************************
/**
 * @brief: This task is used to get the current weight from the load cell.
 * @details takes the semaphore to ensure that only one task can access the load cell at a time.  
 *          It reads the current weight from the load cell and stores it in the currentWeight variable.
 *          It releases the semaphore after reading the weight.  
 * @para: This task does not take any parameters.
 * @note: This task runs every 500 milliseconds to get the current weight from the load cell.
 *        It is used to ensure that the current weight is updated frequently and accurately.
 */
void Task1( void *pvParameters )
{  
  while (1)
  {  
    // taking the semaphore 
    xSemaphoreTake(semaphore,portMAX_DELAY);    
    // get the current weight from the load cell 
    // if the scale is not ready, then it will return 0.
    // if the scale is ready, then it will return the current weight.
    Serial.println("Task1:Getting the current weight from the load cell");
    currentWeight = getWeight();   // current weight 
    //releasing the semaphore. 
    xSemaphoreGive(semaphore);  
    // wait for 500 milliseconds before getting the next weight.
    vTaskDelay(500/ portTICK_PERIOD_MS); // poll over 500ms, as requested. 
  }
}

/**
 * @brief: This task is used to display the current weight on the 4 digits 7-segment display.
 * @details takes the semaphore to ensure that only one task can access the display at a time.    
 *          It reads the current weight from the currentWeight variable and displays it on the display.
 *          It releases the semaphore after displaying the weight. 
 * @para: This task does not take any parameters.
 * @note: This task runs every second to display the current weight on the display. 
 *        It is used to ensure that the current weight is displayed frequently and accurately.
 *        The display is reset before displaying the current weight to ensure that the display is clear.   
 * @return: This task does not return any value.
  */
void Task2( void *pvParameters )
{  
   while(1)
  { 
    // taking the semaphore 
    xSemaphoreTake(semaphore,portMAX_DELAY);
    // display the current weight on the display
    Serial.println("Task2: Displaying the current weight on the display");
    // if the current weight is 0, then display "----" on the display
    displayWeight(currentWeight);   
    // if the current weight is greater than 0, then display the current weight on the display
    // wait for 1 second before displaying the next weight.
    // The semaphore is released after displaying the weight to allow other tasks to access the shared resource.
    //releasing the semaphore.  
    xSemaphoreGive(semaphore); 
    // wait for 1 second before displaying the next weight.
    vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per (n) second.
  }  
}

/**
 * @brief:  This task is used to run the web server.
 * @details takes the semaphore to ensure that only one task can access the web server at a time.
 *          It handles the web server requests and sends the current weight to the web clients. * 
 * @para:   This task does not take any parameters.
 * @note:   This task runs every second to handle the web server requests and send the current weight to the
 *          web clients. It is used to ensure that the web server is running and broadcasting the current 
 *          weight to the clients.
 * @return: This task does not return any value.
 *  
 * */
void Task3( void *pvParameters )
{   
  while (1)
  {
    // taking the semaphore 
    // This is used to ensure that only one task can access the web server at a time.
    // This is done to avoid any conflicts between the tasks that access the web server.  
    Serial.println("Task3: Running the web server");
    // handle the web server requests 
    xSemaphoreTake(semaphore,portMAX_DELAY);
    // convert the current weight to a string and send it to the web clients.
    // This is done to update the current weight on the web interface.
    
    String jsonString = ""; 
    // create a JSON object to hold the current weight

    JsonObject object = jsonDoc_tx.to<JsonObject>(); // create a JSON object
    object["weight"] = currentWeight;               // add the current weight to the JSON object  

    // serialize the JSON object to a string
    serializeJson(jsonDoc_tx, jsonString);              // convert the JSON object to a string
    // send the JSON string to the web clients.     
    webSocket.broadcastTXT(jsonString);     // send char_array to clients(broadcast). 
    // give the semaphore to allow other tasks to access the shared resource.
    Serial.println("Task3: Web server is running and broadcasting the current weight to the clients");
    xSemaphoreGive(semaphore);  //releasing the semaphore. 
    // wait for 1 second before running the web server again.
    vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per 1 second.
  }
}

/**
 * @brief: This task is used to run the Blynk cloud interaction.
 * @details takes the semaphore to ensure that only one task can access the Blynk cloud at a time.      
 *          It sends the current weight to the Blynk cloud using virtual pins V0 and V1.
 *          It releases the semaphore after sending the weight.  
 * @para: This task does not take any parameters.
 * @note: This task runs every second to send the current weight to the Blynk cloud.
 *        It is used to ensure that the current weight is updated frequently and accurately on the Blynk cloud.
 *        The Blynk cloud is used to display the current weight over time (5 seconds).     
 *        This task requires a Blynk account and a template to be created before using it.
 *        You need to replace the BLYNK_AUTH_TOKEN with your own Blynk authentication token.                     
 * @return: This task does not return any value.
 * */
void Task4(void *pvParameters )
{   
  while (1)
  {
    // this task take the semaphore 
    Serial.println("Task4: Running the Blynk cloud interaction");
    // This is used to ensure that only one task can access the Blynk cloud at a time.
    // This is done to avoid any conflicts between the tasks that access the Blynk cloud.
    xSemaphoreTake(semaphore,portMAX_DELAY);
    Serial.println("Task4: Blynk cloud interaction is running and sending the current weight to the Blynk cloud");
    // run the Blynk cloud interaction 
    Blynk.run();
    // start the Blynk timer to run the Blynk cloud interaction every second
    timer.run();
    // give the semaphore to allow other tasks to access the shared resource.
    xSemaphoreGive(semaphore);  //releasing the semaphore. 
    // wait for 1 second before running the Blynk cloud interaction again.
    vTaskDelay((1000/ portTICK_PERIOD_MS)); // run per (n) second.
  }
}



//****************************************************************************** Sep up function  *****************************************************************************

/**
 * @brief function is used to set up the ESP32 and initialize the display, load cell, WiFi connection, web server, and Blynk cloud.
 *        It is called once when the ESP32 is powered on or reset.
 *  
 *@details This function initializes the display, load cell, WiFi connection, web server, and Blynk cloud.
 *         It sets the brightness of the display, initializes the load cell, and sets the calibration factor.
 *         It also creates a semaphore to ensure that only one task can access the shared resource at a time.
 *         It starts the web server and web socket, and sets up the Blynk cloud interaction.      
 * @para:  This function does not take any parameters.
 * @return: This function does not return any value.  
 */
void setup() 
{
  // connect to the available wifi using your AP name and password. 
  // This is done to connect the ESP32 to the WiFi network.
  Serial.println("Connecting to WiFi..."); // print a message to the serial monitor to indicate that the ESP32 is connecting to the WiFi network.
  // WiFi.begin() function is used to connect the ESP32 to the WiFi network.
  WiFi.begin(AP_NAME,AP_PASS);  
  // Serial.begin() function is used to initialize the serial communication with the ESP32.
  // The serial communication is initialized with a baud rate of 115200.  
  Serial.begin(115200);   
  
  // 1- Initializing the display (4 digits 7 segment display) 
  // The display is used to show the current weight.
  // The display is initialized with the CLK_PIN and DIO_PIN defined above. 
  Serial.println("Initializing the LED display");
  // initialize the display with the CLK_PIN and DIO_PIN
  displayScale.init();  // initialize the display
  // set the brightness of the display
  displayScale.setBrightness(5); // set the brightness (0:dimmest, 7:brightest) 
    
  //2- Load cell setting and initilization 
  Serial.println("Initializing the scale");
  scaleReader.begin(DOUT_PIN,SCK_PIN);   
  
  // set the calibration factor = 0 for the load cell
  scaleReader.set_scale();    //no calibration 
  //Tare the scale to remove any weight on the scale.
  // Taring the scale means removing any weight on the scale and setting the current weight to zero.
  // This is done to ensure that the scale starts from zero when there is no weight on it.  
  // Taring the scale is done by calling the tare() function of the HX711 library.
  // This function will set the current weight to zero and remove any weight on the scale.  
  scaleReader.tare();         

  // create a new semaphpre and check if it has been created.
  // The semaphore is used to ensure that only one task can access the shared resource (load cell and display) at a time.
  // This is done to avoid any conflicts between the tasks that access the shared resource.
  Serial.println("Creating a semaphore to handle shared resources");
  // create a mutex semaphore to handle shared resources
  semaphore = xSemaphoreCreateMutex();
  if (semaphore == NULL)
  {
    Serial.println("Semaphore could not be created!");
  }

  // wait for the WiFi connection to be established.
  Serial.println("Connecting to WiFi...");
  if(WiFi.status() != WL_CONNECTED) 
  {        
    Serial.println("..... Connecting.... \n");
    delay(1000);   
  }  

  // This is your local IP address. 
  // You can use this IP address to access the web interface of the ESP32.
  // It will be printed on the Serial Monitor after the ESP32 is connected to the WiFi network.
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  // you need this IP to access to the web interface
    
  //3 start the web server and sockets.
  Serial.println("Starting the web server and web socket");
  // start the web server on port 80
  // The web server will serve the HTML page defined in the "website" string variable.
  // The web server will handle any client requests to the root path ("/").
  server.on("/", []() {                               
  server.send(200, "text/html", website);              //  send out the HTML string "webpage" to the client
      });
  
  server.begin();   
  Serial.println("Web server started on port 80"); // print the web server started message on the Serial Monitor                                 
  // start the web socket on port 81.
  webSocket.begin();    
  webSocket.onEvent(webSocketEvent);                 // set the web socket event handler to handle client requests.
  Serial.println("Web socket started on port 81");   // print the web socket started message on the Serial Monitor
  // The webSocket.onEvent(webSocketEvent) function is used to set the event handler for the web socket.
  // This function will be called when a client connects, disconnects, or sends a message to the server.  
  
  
  //4- Blynk interaction
  // Blynk is used to send the current weight to the Blynk cloud and display it on the Blynk app.
  Serial.println("Starting Blynk Cloud");     
  // Blynk.begin() function is used to start the Blynk cloud interaction.
  // You need to replace the BLYNK_AUTH_TOKEN with your own Blynk authentication token.   
  // You can get the BLYNK_AUTH_TOKEN from the Blynk app after creating a template.
  // Blynk.begin() function takes the BLYNK_AUTH_TOKEN, AP_NAME, and AP_PASS as parameters.       
  // Blynk.begin() function will connect the ESP32 to the Blynk cloud and start the Blynk cloud interaction.
   Blynk.begin(BLYNK_AUTH_TOKEN, AP_NAME, AP_PASS); // Blynk starts here.

  // BlynkTimer is used to run the Blynk cloud interaction every second.  
  // The timer.setInterval() function takes the interval in milliseconds and the function to be called as parameters.   
  timer.setInterval(1000L, runBlynk); 
   
  //5- Creating different tasks(getting weight, displaying the current weight, web server and blynk interaction.
   Serial.println("Creating tasks");
  // Create different tasks for getting weight, displaying weight, web server, and Blynk cloud interaction.
  // The tasks are created with different priorities and stack sizes.
  // Task1: getting weight
  // Task2: displaying weight
  // Task3: web server
  // Task4: Blynk cloud interaction (commented out).

  xTaskCreate(Task1, "get Weight", 10000, NULL, 1, &TaskHandle_1); // getting weight 
  xTaskCreate(Task2, "Display 1", 10000,NULL, 1, &TaskHandle_2);  
  xTaskCreate(Task3, "Web Server", 10000,NULL, 2, &TaskHandle_3);
  xTaskCreate(Task4, "Blynk Cloud", 10000,NULL,3, &TaskHandle_4);
  // Blynk cloud interaction is commented out. You can uncomment it to use it.

  // Starting the demo and run the tasks.
  Serial.println("....Starting the demo .... \n");  
}

//**************************************************************************** main function (loop) *****************************************************

/**
 * @brief function is the main loop of the ESP32 application.
 * @details  It runs continuously after the setup function is called. 
 *            This function handles the web server and web socket events.
 *            It calls the server.handleClient() function to handle any client requests to the web server.
 *            It also calls the webSocket.loop() function to handle any web socket events.
 * @para: This function does not take any parameters.
 * @return: This function does not return any value.
 * 
 */
void loop() 
{   
  // run server forever. 
  // This function handles the web server and web socket events.
  // It is called continuously in the main loop to handle any client requests to the web server.
  // It calls the server.handleClient() function to handle any client requests to the web server.
  // It also calls the webSocket.loop() function to handle any web socket events. 
  server.handleClient();  // webserver methode that handles all Client
  webSocket.loop();   
}
// This is the end of the main function (loop) and the end of the program.
