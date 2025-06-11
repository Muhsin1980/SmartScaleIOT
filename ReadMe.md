## Title:Smart Weight Measuring Demo using the ESP32 and HX711. 
       This demo shows a smart application to measure the current weight using ESP32 MCU and HX711.
       The current weight can be displayed on the LED and the Blynk cloud. Users can also interact witht the demo 
       using the web interface, such as taring (zeroing) the scale or displaying the current weight. Web interface shows the current weigth. 
       
## Prerequisites
  # Software  
    - PlatformIO with Visual Code.
    - Windows operating system.
    - Compile for C++
  # Libraries  (PlatformIO.ini)
    TM1637 Driver      # 4 digits-7 Segment Display
	HX711_ADC          # HX711 (Load cell)
	BlynkESP32_BT_WF   # Blynk 
	WebSockets         # Web sockets 
	
 # Hardware 
    - ESP32 MCU 
    - 4 digits -7 segment display 
    - Load cell (5k or 10k)
    - HX711 
    - USB cable 
    - Laptop or PC 
## Task breakdown 
  In this demo, there are 4 tasks to do the folloing actions: 
  - Task1: get the current weight from the load cell per 500ms.
  - Task2: Display the current weight on the LED.
  - Task3: Run web server and wait for requests from clients.
    Web server sends the current weight to clients and also recieve commands from them t
    to taring the scale, if needed. 
  - Task4: send the current weight to the Cloud using Blynk platform. 
    
#  Wiring circuit and description 
   # Display circuit wiring 
      - CLK  pin from display  to   pin 48  from ESP32 MCU
      - DIO   pin from display to   pin 47 from  ESP32 MCU 
      - VCC  TO 5V from ESP32 
      - GND  TO GND from ESP32 

   # HX711 circuit wiring
        - DOUT pin from HX711 is connected to the pin  21  from the ESP32 MCU 
        - SCK  pin from HX711 is connected to the pin  20  from the ESP32 MCU
        - VCC  TO 5V from ESP32 
        - GND  TO GND from ESP32 
        
  # Load cell to XH711 circute wiring 
     - Red   --> E+
     - Black --> E-
     - White --> A+
     - Green --> A-

## Calibration Factor 
   # -396.99 was selected as a calibration factor after testing many readins from the lead cell. 
   # The more raw readings you test, the more accurate readings you could have. 
   
## Web server and web Interface
  - Web server is staretd once demo is started.
  - Web server sends the current weight to the client over a period of time. 
  - Web interface is designed to ineract witht the demo by taring the scale . 
    
## Get the code  
   - Create your folder in your own location and use cd to move to your project folder. 
   - Clone the repository(Master Branch):
         git clone https://github.com/Muhsin1980/SmartScaleIOT.git
                           
##  Usage
 To use the Smart Scale demo, follow these steps:
    - Make sure you add all the libraries shown in the PlatformIO.ini
    - Connect your demo to your PC or Laptop. 
    - Open the PlatformIO (You should be able to use Arduino, if wanted)
    - Run demo 
    - Put your well known weight on the load cell. 
    - You should be able to see your weight on the LED display. 
    - Open the serial monitor to get your local IP for web interface. 
    - Open your browser and past your IP. 
    - Open your Blynk account and you should see your weight 
    - Try again with different weights. 

## for more questions please find the report. 


    
    
  
