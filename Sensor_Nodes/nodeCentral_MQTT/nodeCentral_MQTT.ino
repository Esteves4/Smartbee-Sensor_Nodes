#define TINY_GSM_MODEM_SIM800                                                                     // Defines the model of our gsm module
#define SerialMon Serial                                                                        // Serial communication with the computer
#define SerialAT Serial3                                                                        // Serial communication with the gsm module
#define TINY_GSM_DEBUG SerialMon                                  

#define DUMP_AT_COMMANDS                                                                          // Comment this if you don't need to debug the commands to the gsm module
#define DEBUG                                                                                     // Comment this if you don't need to debug the arduino commands         

#include <TinyGsmClient.h>
#include <PubSubClient.h>

#include <LowPower.h>

#include <RF24Network.h>
#include <RF24Network_config.h>
#include <Sync.h>

#include <nRF24L01.h>
#include <RF24.h>
#include <SdFat.h>
#include <SPI.h>

//INITIAL CONFIGURATION OF SD
const uint8_t SOFT_MISO_PIN = 10;
const uint8_t SOFT_MOSI_PIN = 11;
const uint8_t SOFT_SCK_PIN  = 12;
const uint8_t SD_CHIP_SELECT_PIN = 13;
const int8_t  DISABLE_CHIP_SELECT = -1;

// SdFat software SPI template
SdFatSoftSpi<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> SD;

// File
SdFile file;
SdFile file2;

//INITIAL CONFIGURATION OF NRF
const int pinCE = 53;                                                                             // This pin is used to set the nRF24 to standby (0) or active mode (1)
const int pinCSN = 48;                                                                            // This pin is used to tell the nRF24 whether the SPI communication is a command or message to send out
const int interruptPin = 18;                                                                      // This pin is used to wake wape the arduino when a payload is received

RF24 radio(pinCE, pinCSN);                                                                        // Declare object from nRF24 library (Create your wireless SPI)
RF24Network network(radio);                                                                       // Network uses that radio

#define id_origem 00                                                                              // Address of this node

//INITIAL CONFIGURATION OF SIM800

/* ### APN configurations ###  */

const char apn[]  = "claro.com.br";
const char user[] = "claro";
const char pass[] = "claro";

/*
const char apn[]  = "timbrasil.br";
const char user[] = "tim";
const char pass[] = "tim";
*/

const int DTR_PIN = 7;                                                                               // This pin is used to wake up the gsm module

//INITIAL CONFIGURATION OF MQTT 
const char* broker = "200.129.43.208";                                                            // Address of the mqtt broker
const char* user_MQTT = "teste@teste";                                                            // Username for the tcp connection
const char* pass_MQTT = "123456";                                                                 // Password for the tcp connection

#define TOPIC "sensors/coleta"                                                                    // MQTT topic where we'll be sending the payloads
#define MQTT_MAX_PACKET_SIZE 500

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif
  TinyGsmClient client(modem);
  PubSubClient mqtt(client);

//STRUCTURE OF OUR PAYLOAD
struct payload_t {
  int colmeia;
  float temperatura;
  float umidade;
  float tensao_c;
  float tensao_r;
  String  timestamp;
};

//GLOBAL VARIABLES
const char ArraySize = 12;                                                                        // Amount of payloads the central node is going to save to send to the webservice
payload_t ArrayPayloads[ArraySize];                                                               // Array to save the payloads
char msgBuffer[MQTT_MAX_PACKET_SIZE];

byte ArrayCount = 0;                                                                              // Used to store the next payload    
payload_t payload;                                                                                // Used to store the payload from the sensor node
bool dataReceived;                                                                                // Used to know whether a payload was received or not

long previousMillis = 0;
long interval = 10000; //(ms)

char cp[15];

// PROTOTYPES OF FUNCTIONS - Force them to be after declarations
String payloadToString(payload_t* tmp_pp);
void saveToSD(payload_t* tmp_pp);

void setup() { 
  /* SIM800L configuration */
  #ifdef DEBUG
    SerialMon.begin(57600);
    delay(10);
    
    SerialMon.println(F("Initializing SIM800L and Configuring..."));
    SerialAT.begin(57600);
    delay(3000);
    
    pinMode(DTR_PIN, OUTPUT);
    digitalWrite(DTR_PIN, LOW);

    SerialMon.println(F("Shutting SIM800L down"));
    sleepGSM();
    
    SerialMon.flush();
    SerialMon.end();
  #else

    /* SIM800L configuration*/
    
    SerialAT.begin(57600);                                                                        // Starts serial communication
    delay(3000);                                                                                  // Waits to communication be established
    
    sleepGSM();                                                                                   // Puts gsm to sleeps
  #endif
    
  /* nRF24L01 configuration*/
  #ifdef DEBUG
    SerialMon.begin(57600);                                                         
    SerialMon.println(F("Initializing nRF24L01..."));
    SerialMon.flush();
  #endif
  
  SPI.begin();                                                                                    // Starts SPI protocol
  radio.begin();                                                                                  // Starts nRF24L01
  radio.maskIRQ(1, 1, 0);                                                                         // Create a interruption mask to only generate interruptions when receive payloads
  radio.setPayloadSize(32);                                                                       // Set payload Size
  radio.setPALevel(RF24_PA_LOW);                                                                  // Set Power Amplifier level
  radio.setDataRate(RF24_250KBPS);                                                                // Set transmission rate

  /* SD configuration*/
  
  #ifdef DEBUG
    SerialMon.println(F("Initializing SD..."));
    
    if (!SD.begin(SD_CHIP_SELECT_PIN))
      SerialMon.println("Initialization failed!");
    else
      SerialMon.println("Initialization done.");

    /* Get ArrayCount from SD (if there is one) */
    if (SD.exists("ArrayCount.txt")) {
      SerialMon.print("\nGetting ArrayCount from SD card... ");
      if (file.open("ArrayCount.txt", FILE_READ)) {
        char count_str[4];

        byte i = 0;
        while (file.available()) {
          char c = file.read();
          if (c >= 48 && c <= 57) {
            count_str[i++] = c;
          } else {
            count_str[i] = '\0';
            break;
          }
        }
        
        sscanf(count_str, "%u", &ArrayCount);
        file.close();
        
        SerialMon.print("Done!");
      } else {
        SerialMon.println("FAIL! Couldn't open the file ArrayCount.txt");
      }
    } else {
      SerialMon.print("\nCreating ArrayCount.txt...");
      if (file.open("ArrayCount.txt", FILE_WRITE)) {
        file.println((int) ArrayCount);
        file.close();
        SerialMon.println("Done!");
      } else {
        SerialMon.println("FAIL! Couldn't open the file ArrayCount.txt");
      }
    }

    SerialMon.flush();
    SerialMon.end();
  #else
    SD.begin(SD_CHIP_SELECT_PIN);
    
    /* Get ArrayCount from SD (if there is one) */
    if (file.open("ArrayCount.txt", FILE_READ)) {
      char count_str[4];

      byte i = 0;
      while (file.available()) {
        char c = file.read();
        if (c >= 48 && c <= 57) {
          count_str[i++] = c;
        } else {
          count_str[i] = '\0';
          break;
        }
      }
      
      sscanf(count_str, "%u", &ArrayCount);
      file.close();
      
      SerialMon.print("Done!");
    }
  #endif

  network.begin(/*channel*/ 120, /*node address*/ id_origem);                                     // Starts the network
}

void loop() {
  network.update();                                                                               // Check the network regularly
  
  #ifdef DEBUG
    SerialMon.begin(57600);
  #endif
  
  unsigned long currentMillis = millis();

   if(currentMillis - previousMillis > interval) {
      #ifdef DEBUG
        SerialMon.println(F("\nShutting GSM down"));
      #endif

       /* Puts gsm to sleep again */
       sleepGSM();

      #ifdef DEBUG
        SerialMon.println(F("Shutting Arduino down"));
        SerialMon.flush();
        SerialMon.end();
      #endif

      attachInterrupt(digitalPinToInterrupt(interruptPin), interruptFunction, FALLING);           // Attachs the interrupt again after enabling interruptions
      
      LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);                                            // Function to put the arduino in sleep mode

      detachInterrupt(digitalPinToInterrupt(interruptPin));
      
      /* Wakes gsm up */
      #ifdef DEBUG
        SerialMon.begin(57600);
      #endif
      
      wakeGSM();                                                                                 // Wakes the gsm                                                            
      delay(1000);                                                                               // Waits for the gsm's startup
      
      #ifdef DEBUG
        SerialMon.println(F("GSM woke up"));
        SerialMon.println(F("Arduino woke up"));
        SerialMon.flush();
        SerialMon.end();
      #endif
  }   

  receiveData();                                         
 
  if (dataReceived) {
    #ifdef DEBUG
      SerialMon.begin(57600);
    #endif
    
    dataReceived = false;
    
    if(!modem.testAT()){
        wakeGSM();                                                                                 // Wakes the gsm                                                            
        delay(1000);                                                                               // Waits for the gsm's startup
    }

    /* Gets the timestamp and removes special characters of the string */
    ArrayPayloads[ArrayCount - 1].timestamp = modem.getGSMDateTime(DATE_FULL);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(2,1);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(4,1);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(6,1);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(8,1);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(10,1);
    ArrayPayloads[ArrayCount - 1].timestamp.remove(12);

    /* Save the payload in the microSD card */
    #ifdef DEBUG
      SerialMon.print("Writing payload into SD... ");
      SerialMon.flush();
      SerialMon.end();
      SerialAT.flush();
      SerialAT.end();
    #endif
    
    saveToSD(&ArrayPayloads[ArrayCount - 1]);
    
    #ifdef DEBUG
      SerialMon.begin(57600);
      SerialMon.println("Done!");
      
      SerialAT.begin(57600);
    #endif
    
    
    /* Check if the array is full, if it is, sends all the payloads to the webservice */

    if(ArrayCount == ArraySize){
      #ifdef DEBUG
        SerialMon.println(F("\nConnecting to the server..."));
        connection();
        
        int signalQlt = modem.getSignalQuality();
        SerialMon.println("GSM Signal Quality: " + String(signalQlt));
        
        SerialMon.println(F("Publishing payload..."));
        publish();
        delay(1000);
      #else
        connection();
        publish(); 
        delay(1000);
      #endif

      /* Starts to save the payloads again whether the payloads were sent or not */
      ArrayCount = 0;
      updateArrayCountFile(ArrayCount);
    }

    previousMillis = millis();
    
  }

  #ifdef DEBUG
      SerialMon.flush();
      SerialMon.end();
  #endif

}

void interruptFunction(){
     
}

void connection(){
  #ifdef DEBUG
    SerialMon.println(F("Inicializando GSM..."));
    modem.restart();  

    SerialMon.println(F("Aguardando rede..."));
    modem.waitForNetwork();
  
    SerialMon.print(F("Conectando a "));
    SerialMon.print(apn);
    SerialMon.println("...");
  
    modem.gprsConnect(apn, user, pass);
    mqtt.setServer(broker, 1883);
    
    SerialMon.println(F("Conectando ao broker..."));
    mqtt.connect("CentralNode", user_MQTT, pass_MQTT);
    
  #else
    modem.restart();

    modem.waitForNetwork();
       
    modem.gprsConnect(apn, user, pass);
    mqtt.setServer(broker, 1883);
    mqtt.connect("CentralNode", user_MQTT, pass_MQTT);
  #endif
}

void receiveData() {
  RF24NetworkHeader header;
  
  network.update();

  if(network.available()) {                               
    network.read(header, &payload, sizeof(payload));                                   // Reads the payload received
    
    ArrayPayloads[ArrayCount] = payload;                                               // Saves the payload received
    #ifdef DEBUG
      SerialMon.begin(57600);
    #endif
    updateArrayCountFile(++ArrayCount);
    
    dataReceived = true;
    
    #ifdef DEBUG
      
      SerialMon.print(F("\nReceived data from sensor: "));
      SerialMon.println(payload.colmeia);
  
      SerialMon.println(F("The data: "));
      SerialMon.print(payload.colmeia);
      SerialMon.print(F(" "));
      SerialMon.print(payload.temperatura);
      SerialMon.print(F(" "));
      SerialMon.print(payload.umidade);
      SerialMon.print(F(" "));
      SerialMon.print(payload.tensao_c);
      SerialMon.print(F(" "));
      SerialMon.println(payload.tensao_r);
      SerialMon.print(F(" "));
      SerialMon.println(payload.timestamp);
      
      SerialMon.flush();
      SerialMon.end();
    #endif 
  }
  
}

void publish(){

  /* Sends to the webservice all the payloads saved */
  String msg = "";
  file.open("buffer.txt", FILE_READ);
  file2.open("tmp_buffer.txt", FILE_WRITE);

  /*
  for(int i = 0; i < ArraySize; ++i){
    if(i > 0){
      msg += "/";
    }
    
    msg += payloadToString(&ArrayPayloads[i]);
            
  }
  */
  
  //int length = msg.length();
  char msgBuffer[700];
  /* Gets the next packet to sent from buffer.txt */
  int i;
  for (i = 0; i < 699; i++) {
    char c;
    if (!file.available() || (c = file.read()) == '\n') break;
    msgBuffer[i] = c;
  }
  msgBuffer[i] = '\0';
  
  //msg.toCharArray(msgBuffer,length+1);

  /* If the packet couldn't be sent, save in a new buffer */
  if (!mqtt.publish(TOPIC, msgBuffer)) {
    file2.print(msgBuffer);
    file2.print('\n');
  }

  /* Save the rest of the buffer.txt in the new buffer */
  while (file.available()) {
    file2.print(file.read());
  }

  /* Turns new buffer (tmp_buffer.txt) into THE buffer (buffer.txt) */
  file.close();
  SD.remove("buffer.txt");
  file2.rename(SD.vwd(), "buffer.txt");
  file2.close();
  
  delay(1000);

}

void sleepGSM() {
  modem.radioOff();
  digitalWrite(DTR_PIN, HIGH);                                                          // Puts DTR pin in HIGH mode so we can enter in sleep mode
  modem.sleepEnable(true);
  
}

void wakeGSM() {  
  digitalWrite(DTR_PIN, LOW);                                                          // Puts DTR pin in LOW mode so we can exit the sleep mode
  modem.sleepEnable(false);
}

void saveToSD(payload_t* tmp_pp) {                                                     // Procedure to save the data received in the SD (backup and buffer)
  byte i = 0;
  SerialMon.begin(57600);
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
//  String fileName = "teste.txt";
  sprintf(cp, "%s_%s_20%s.txt","20", "20", "20" );
 /* String fileName = tmp_pp->timestamp.substring(4,6) + "_"   +                         // Gets the timestamp to create the name of the file
                    tmp_pp->timestamp.substring(2,4) + "_20" +
                    tmp_pp->timestamp.substring(0,2) + ".txt";*/
//  SerialMon.print("OK ");
//  SerialMon.println((int) ++i);
//  strcpy(cp, fileName.c_str());                                                        // Converts the string of the name to char array
  SerialMon.print("OK ");
  SerialMon.print((int) ++i);
  
  /* Writes the payload data into the SD backup file */
  #ifdef DEBUG
//    SerialMon.begin(57600);
    if (!file.open(cp, FILE_WRITE)) {
      SerialMon.print("FAIL TO OPEN FILE ");
      SerialMon.println(cp);
    } else {
      SerialMon.print(cp);
      SerialMon.print(" opened with success.");
    }
  #else
    file.open(cp, FILE_WRITE);
  #endif
  
  String msg = "";
  msg = payloadToString(&ArrayPayloads[ArrayCount - 1]);
  int length = msg.length();
  msg.toCharArray(msgBuffer,length+1);
  file.print(msgBuffer);
  
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  file.print("\n");
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  file.close();
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  
  /* Writes the payload data into the SD buffer file */
  #ifdef DEBUG
    if (!file.open("buffer.txt", FILE_WRITE)) {
      SerialMon.println("FAIL TO OPEN FILE 'buffer.txt'");
    } else {
      SerialMon.print("buffer.txt opened with success.");
    }
  #else
    file.open("buffer.txt", FILE_WRITE);
  #endif
  
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  if (ArrayCount - 1 > 0) {
    file.print("/");                                                                   // Add a slash between payloads
  }
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);

  msg = payloadToString(&ArrayPayloads[ArrayCount - 1]);
  length = msg.length();
  msg.toCharArray(msgBuffer,length+1);
  file.print(msgBuffer);
  
  file.print(msgBuffer);
  
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  if (ArrayCount == 12) {
    file.print('\n');                                                                    // Add a new line after 12 payloads
  }
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  file.close();
  SerialMon.print("OK ");
  SerialMon.println((int) ++i);
  #ifdef DEBUG
    SerialMon.println("DONE SAVING TO SD.");
    SerialMon.flush();
    SerialMon.end();
  #endif
}

String payloadToString(payload_t* tmp_pp) {
  return String(tmp_pp->colmeia) + "," + String(tmp_pp->temperatura) + "," + String(tmp_pp->umidade) + "," + String(tmp_pp->tensao_c) + "," + String(tmp_pp->tensao_r) + "," + String(tmp_pp->timestamp);
}

void updateArrayCountFile(char val) {
  #ifdef DEBUG
    SerialMon.print("\nUpdating ArrayCount.txt...");
    if (file.open("ArrayCount.txt", O_CREAT | O_WRITE)) {
      file.println((int) val);
      file.close();
      SerialMon.println("Done!");
    } else {
      SerialMon.println("FAIL! Couldn't open the file ArrayCount.txt");
    }
  #else
    if (file.open("ArrayCount.txt", O_CREAT | O_WRITE)) {
      file.println((int) val);
      file.close();
    }
  #endif
}
