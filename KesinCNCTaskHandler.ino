#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "FS.h"
#include <LittleFS.h>

const char* ssid = "realme 10";
const char* password = "s4sviuz4";
const char* MAC="c8:f0:9e:4e:2b:74";

//Your Domain name with URL path or IP address with path
String serverName = "http://mes.yilbay.site/terminal/terminaldata";
String hostnameString;
String ipString;

HTTPClient http1; // For INIT and Production signal
HTTPClient http0; // For Alive signal


hw_timer_t *timer = NULL;

volatile bool startf = false;
volatile bool pausef = false;
volatile bool stopf = false;

volatile bool startOn = false;
volatile bool pauseOn = false;

volatile bool uretimdenM30=false;

volatile bool isOnline; // adjusted true when login WiFi
volatile bool append=false; // To make sure appends or not in writeFile()
volatile bool fromOnline; // is it first Offline signal or not?
volatile bool afterKeepAlive=false;
volatile bool ReadPrev=false;

typedef struct { // Signal structure for count each signal
    char command[6];
    int times[2];
} ButtonEvent;


int startTimer=0 ,pauseTimer=0,stopTimer=0; // Create signal structure for timers
ButtonEvent startt,pauset,stopt; // Create signal structure for dummies

unsigned long button_timestart = 0,last_button_timestart=0;
unsigned long button_timepause = 0,last_button_timepause=0;
unsigned long button_timestop = 0,last_button_timestop=0;

TaskHandle_t xStopTaskHandle = NULL, xStartTaskHandle = NULL, xPauseTaskHandle = NULL; 

const char * filePath= "/data.txt";
#define FORMAT_LITTLEFS_IF_FAILED true

void IRAM_ATTR StartISR() { // Start Button
  button_timestart=millis();
  if(button_timestart-last_button_timestart>5){
    if(!startf){
      startf=true;
    }else if(startf){
      startf=false;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xStartTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
  last_button_timestart=button_timestart;
}

void IRAM_ATTR PauseISR() { // Pause Button
    button_timepause=millis();
    if(button_timepause-last_button_timepause>5){
      if(!pausef){
        pausef=true;
      }else if(pausef){
        pausef=false;
      }
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(xPauseTaskHandle, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    last_button_timepause=button_timepause;
}

void IRAM_ATTR StopISR() { // M30 Button
  button_timestop=millis();
  if(button_timestop-last_button_timestop>5){
    if(!stopf){
      stopf=true;
    }else if(stopf){
      stopf=false;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xStopTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
  last_button_timestop=button_timestop;
}

void setup() {
    Serial.begin(115200);
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed");
    return;
    }
    LittleFS.format();
    delay(1000);
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    Serial.println("Connecting");
    while(WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());
    IPAddress ip = WiFi.localIP();
    ipString = ip.toString();
    Serial.println(ipString);
    const char* hostname = WiFi.getHostname();
    hostnameString = String(hostname);
    Serial.println(hostnameString);
    Serial.println(String(MAC));
    isOnline=true;
    

    delay(1000);

    if(InitCNC()){
      timer = timerBegin(0, 80, true);  // 1MHz (1,000,000 / second)
      timerStop(timer);

      delay(500);

      timerStart(timer);
      pinMode(5,INPUT_PULLUP); // Alarm not consider
      pinMode(18, INPUT_PULLUP); // Start
      pinMode(19, INPUT_PULLUP); // Pause
      pinMode(21, INPUT_PULLUP); // Stop (M30)

      delay(500);

      stopt.times[0]=0; // For ignore first idle
      xTaskCreatePinnedToCore(StartTask, "Start Task", 2048, NULL, 1, &xStartTaskHandle,0);
      xTaskCreatePinnedToCore(PauseTask, "Pause Task", 2048, NULL, 1, &xPauseTaskHandle,0);
      xTaskCreatePinnedToCore(StopTask, "Stop Task", 94208, NULL, 1, &xStopTaskHandle,0); // 100 data writen in file(each 900bytes) + 2 function to call= 94208 bytes

      attachInterrupt(digitalPinToInterrupt(18), StartISR, CHANGE);
      attachInterrupt(digitalPinToInterrupt(19), PauseISR, CHANGE);
      attachInterrupt(digitalPinToInterrupt(21), StopISR, CHANGE);

      Serial.println("Machine is Initiliazed!");
    }else{
      Serial.println("Machine Could Not Initiliazed!");
    }
    
}

void loop() {

  int info=timerRead(timer)/1000000; // Keep alive
  if(info%300==0&&info!=0){
    afterKeepAlive=false;
    KeepAlive();
    afterKeepAlive=true;
  }
  if(ReadPrev==true){
    Serial.println("Just before Send");
    readFile(LittleFS,filePath);
    LittleFS.format(); // Format the file after readed.
    Serial.println("Previous Data Has Been Sended!");
    ReadPrev=false;
  }
  delay(1000);

}

void StartTask(void *p) { //Start Circuit
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (startf&&!pauseOn) { // Start signal Low
            startOn=true;
            startt.times[0] = timerRead(timer) / 100000; // Catch each 100ms. Same for all time values.
            strcpy(startt.command, "start");
            if(uretimdenM30==false&&stopt.times[0]!=0){
              stopt.times[1]=startt.times[0];
              stopTimer+=calculateTime(stopt.times);
            }
            Serial.println("Start Signal Detected");
        } else if(!startf&&startOn) {  // Star signal High
            startt.times[1] = timerRead(timer) / 100000;
            startTimer+=calculateTime(startt.times);
            Serial.println("Start Signal Leased");
            uretimdenM30=true; // Production happened 
            startOn=false;
        }
    }
}
void PauseTask(void *p) { // Pause Circuit
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (pausef&&!startOn&&!startf) { // Pause signal Low
          pauseOn=true;
          pauset.times[0] = timerRead(timer) / 100000;
          strcpy(pauset.command, "pause");
          Serial.println("Pause Signal Detected");
        } else if(!pausef&&pauseOn){  //Pause signal High
          pauset.times[1] = timerRead(timer) / 100000;
          pauseTimer+=calculateTime(pauset.times);
          Serial.println("Pause Signal Leased");
          pauseOn=false;
        }
    }
}

void StopTask(void *parameter) {  // M30 Circuit
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (stopf&&uretimdenM30&&!pauseOn&&!startOn) {
          if(afterKeepAlive){
            timerWrite(timer,0);
            afterKeepAlive=false;
          }
          strcpy(stopt.command, "stopt");
          stopt.times[0] = (timerRead(timer) / 100000)+1;
          Outp();
          uretimdenM30=false;
        }
    }
}

void Outp() {

  int deneme=0;
  String serverPath = serverName + "?parametre1=" + String(MAC) + "&" + "parametre2=" + String(startTimer) + "&" + "parametre3=" + String(pauseTimer)+ "&" + "parametre4=" + String(stopTimer) + "&" + "parametre5=" + "U" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;

  Serial.printf("Start time: %d \n",startTimer);
  Serial.printf("Pause time: %d \n",pauseTimer);
  Serial.printf("Idle time: %d \n",stopTimer);

  delay(500);

  if(isOnline){
    int httpResponseCodeU = SendData(serverPath,"Core0");
    while(httpResponseCodeU!=200 && deneme!=5){
      httpResponseCodeU = SendData(serverPath,"Core0");
      if(httpResponseCodeU==200){
        break;
      }
      deneme++;
      delay(1000);
    }
    if(httpResponseCodeU==200){
      Serial.println("Production Process Saved!");
    }
   else if (httpResponseCodeU!=200){
      isOnline=false;
      fromOnline=true;
    }
    deneme=0;
  }

  if(!isOnline){
    int httpResponseCodeU=0;
    while(httpResponseCodeU!=200 && deneme!=2){
      httpResponseCodeU=SendData(serverPath,"Core0");
      if(httpResponseCodeU==200){
        break;
      }
      deneme++;
      delay(1000);
    }
    if(httpResponseCodeU==200){
      isOnline=true;
      append=false;
      if(fromOnline==false){
        ReadPrev=true;
      }
    }
    if(httpResponseCodeU!=200){
      writeFile(LittleFS,filePath,serverPath.c_str(),append);
      if(!append){
        append=true;
        fromOnline=false;
      }
    }
    deneme=0;
  }

  startTimer=0;
  pauseTimer=0;
  stopTimer=0;
  startf = false;
  pausef = false;
}

int InitCNC(){
  int deneme=0;

  String serverPath = serverName + "?parametre1=" + String(MAC) + "&" + "parametre2=" + "0" + "&" + "parametre3=" + "0" + "&" + "parametre4=" + "0" + "&" + "parametre5=" + "boot" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
  int httpResponseCodeI=SendData(serverPath,"Core1");

  while(httpResponseCodeI!=200 &&deneme!=5){
    httpResponseCodeI = SendData(serverPath,"Core1");
    if(httpResponseCodeI==200){
      break;
    }
    deneme++;
    delay(1000);
  }
  deneme=0;
  if(httpResponseCodeI==200){
    return 1;
  }
  if(httpResponseCodeI!=200){
    return 0;
  }
}
void KeepAlive(){  // Needs to adjust for other core
  int deneme=0;

  String serverPath = serverName + "?parametre1=" + String(MAC) + "&" + "parametre2=" + "0" + "&" + "parametre3=" + "0" + "&" + "parametre4=" + "0" + "&" + "parametre5=" + "info" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
  int httpResponseCodeA = SendData(serverPath,"Core1");

  while(httpResponseCodeA!=200&&deneme!=5){
    httpResponseCodeA=SendData(serverPath,"Core1");
    if(httpResponseCodeA==200){
      break;
    }
    deneme++;
    delay(1000);
  }
  if(httpResponseCodeA==200){
    Serial.println("Machine is Alive!");
  }
}


int calculateTime(int times[]) {  // Catch time flags values and take different
  int full = times[1] - times[0]; 
  int integerPart = full / 10;  
  int remainder = full % 10;
  float fractionalPart = (float)remainder / 10.0; 
  float result = (float)integerPart + fractionalPart; 
  return integerPart; // Chose integer part
}

void writeFile(fs::FS &fs, const char *path, const char *message, bool appends) {  // Write file to .txt file in ESP32
    Serial.printf("Writing file: %s\r\n", path);

    File file;
    if (appends) {
        file = fs.open(path, FILE_APPEND);  // Open in append mode
        Serial.println("- opened in append mode");
    } else {
        file = fs.open(path, FILE_WRITE);   // Open in write mode (overwrites file)
        Serial.println("- opened in write mode");
    }

    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }

    // Write message to the file followed by a newline
    if (file.println(message)) {
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }

    file.close();
    Serial.println("- file closed after writing");
}

void readFile(fs::FS &fs, const char *path) { // Read file from .txt file in ESP32

    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");

    String line = "";
    while (file.available()) {
        char c = file.read();
        if (c == '\r') {
            continue;  // Skip carriage return characters
        }
        if (c == '\n') {  // Newline character detected
            int deneme=0;
            int httpResponseCodeU=SendData(line,"Core1");
            while (httpResponseCodeU!=200 && deneme!=3){
              httpResponseCodeU=SendData(line,"Core1");
              if(httpResponseCodeU==200){
                break;
              }
              deneme++;
              delay(1000);
            }
            Serial.println(line);  // Print the line
            line = "";  // Clear the buffer for the next line
            deneme=0;  // Clear the try
        } else {
            line += c;  // Append character to line
        }
    }

    // Handle the case where the last line does not end with a newline
    if (line.length() > 0) {
        Serial.println(line);  // Print any remaining line
    }

    file.close();
    Serial.println("- file closed after reading");
}

int SendData(String path,String Core){ // Send Get function from Specific Core
  if(Core.equals("Core0")){
    http0.end();
    http0.begin(path.c_str());
    int respond=http0.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(0)");
      http0.end();
      return respond;
    }else{
      http0.end();
      Serial.println("Data Could Not Sended!(0)");
      return respond;
    }
  }
  if(Core.equals("Core1")){
    http1.end();
    http1.begin(path.c_str());
    int respond=http1.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(1)");
      http1.end();
      return respond;
    }else{
      http1.end();
      Serial.println("Data Could Not Sended!(1)");
      return respond;
    } 
  }
}

