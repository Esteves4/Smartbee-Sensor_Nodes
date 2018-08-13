#define TINY_GSM_MODEM_SIM800
#define SerialMon Serial
#define SerialAT Serial1
#define TINY_GSM_DEBUG SerialMon

#define TEMPOENTRECADALEITURA 20000                             // Time between each reading in milliseconds 
#define DTR_PIN 7
#define DEBUG

#include <TinyGsmClient.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>

#include <LowPower.h>

#include <RF24Network.h>
#include <RF24Network_config.h>
#include <Sync.h>

#include <nRF24L01.h>
#include <RF24.h>
#include <SPI.h>

//INITIAL CONFIGURATION OF NRF
const int pinCE = 8;                                            // This pin is used to set the nRF24 to standby (0) or active mode (1)
const int pinCSN = 9;                                           // This pin is used to tell the nRF24 whether the SPI communication is a command or message to send out

RF24 radio(pinCE, pinCSN);                                      // Declare object from nRF24 library (Create your wireless SPI)
RF24Network network(radio);                                     // Network uses that radio

const uint16_t id_origem = 00;                                  // Address of this node
const uint16_t ids_destino[3] = {01, 02, 03};                   // Addresses of the others nodes

//INITIAL CONFIGURATION OF SIM800
/*const char apn[]  = "claro.com.br";
const char user[] = "claro";
const char pass[] = "claro";*/

const char apn[]  = "timbrasil.br";
const char user[] = "tim";
const char pass[] = "tim";

SoftwareSerial SerialAT(4, 5);                                   // Serial Port configuration -(RX, TX) pins of SIM800L

//INITIAL CONFIGURATION OF MQTT
const char* broker = "200.129.43.208";

const char* user = "teste@teste";               
const char* pass = "123456";                    

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
  byte checksum;
};

//GLOBAL VARIABLES
const uint8_t ArraySize = 1;
uint8_t ArrayCount = 0;
payload_t ArrayPayloads[ArraySize];
payload_t payload;                                              // Used to store the payload from the sensor node

bool dataReceived;                                              // Used to know whether a payload was received or not

void setup() {  
  SerialMon.begin(57600);                                           // Start Serial communication
  delay(10);

  SerialAT.begin(57600);
  delay(3000);
    
  /* nRF24L01 configuration*/
  Serial.println("Initializing nRF24L01...");
  SPI.begin();                                                  // Start SPI protocol
  radio.begin();                                                // Start nRF24L01
  radio.maskIRQ(1, 1, 0);                                       // Create a interruption mask to only generate interruptions when receive payloads
  radio.setPayloadSize(32);                                     // Set payload Size
  radio.setPALevel(RF24_PA_LOW);                                // Set Power Amplifier level
  radio.setDataRate(RF24_250KBPS);                              // Set transmission rate
  attachInterrupt(0, receberDados, FALLING);                    // Attach the pin where the interruption is going to happen
  network.begin(/*channel*/ 120, /*node address*/ id_origem);   // Start the network

  pinMode(DTR_PIN, OUTPUT);
  digitalWrite(DTR_PIN, LOW);
  
  SerialMon.flush();
  SerialMon.end();
}

void loop() {
  network.update();                                            // Check the network regularly

  SerialMon.begin(57600);

  if (radio.rxFifoFull()) {                                     // If the RX FIFO is full, the RX FIFO is cleared
    radio.flush_rx();
  } else if (radio.txFifoFull()) {                              // If the TX FIFO is full, the TX FIFO is cleared
    radio.flush_tx();
  }

  SerialMon.println("Shutting SIM800L down");
  sleepGSM();
  
  
  SerialMon.println("Shutting Arduino down");
  SerialMon.end();

  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);                  // Function to put the arduino in sleep mode
  
  attachInterrupt(0, receberDados, FALLING);

  SerialMon.begin(57600);
  SerialMon.println("Arduino woke up");

  if (dataReceived) {
    dataReceived = false;
    ArrayPayloads[ArrayCount] = payload;
    ++ArrayCount;
  }

  if(ArrayCount == ArraySize){
   
    SerialMon.println("Waking GSM");
    wakeGSM();
    connection();
    publicar(ArrayPayloads); 
      
    ArrayCount = 0;
    
  }

  SerialMon.flush();
  SerialMon.end();

}

void connection(){
  SerialMon.println("Inicializando GSM...");
  modem.restart();
 
  SerialMon.println("Aguardando rede...");
  modem.waitForNetwork();
    

  SerialMon.print("Conectando a ");
  SerialMon.print(apn);
  SerialMon.println("...");

  modem.gprsConnect(apn, user, pass);
  mqtt.setServer(broker, 1883);
  
  SerialMon.println("Conectando ao broker...");

  mqtt.connect("CentralNode", ThingspeakUser, ThingspeakPass);
}

void receberDados() {
  RF24NetworkHeader header;
  SerialMon.begin(57600);

  while (!network.available()) {                              // Keeps busy-waiting until the transmission of the payload is completed
    network.update();
  }

  while (network.available()) {                               // Reads the payload received
    network.read(header, &payload, sizeof(payload));

#ifdef DEBUG
    SerialMon.print("Received data from sensor: ");
    SerialMon.println(payload.colmeia);

    SerialMon.println("The data: ");
    //SerialMon.print("Colmeia: ");
    SerialMon.print(payload.colmeia);
    SerialMon.print(" ");
    //SerialMon.print("Temperatura: ");
    SerialMon.print(payload.temperatura);
    SerialMon.print(" ");
    //SerialMon.print("Umidade: ");
    SerialMon.print(payload.umidade);
    SerialMon.print(" ");
    //SerialMon.print("Tensao sensor: ");
    SerialMon.print(payload.tensao_c);
    SerialMon.print(" ");
    //SerialMon.print("Tensao repetidor: ");
    SerialMon.println(payload.tensao_r);
    if (payload.checksum == getCheckSum((byte*) &payload)) {
      SerialMon.println("Checksum matched!");
    } else {
      SerialMon.println("Checksum didn't match!");
    }
#endif

  SerialMon.flush();
  SerialMon.end();

    dataReceived = true;
  }

}

byte getCheckSum(byte* payload) {
  byte payload_size = sizeof(payload_t);
  byte sum = 0;

  for (byte i = 0; i < payload_size - 1; i++) {
    sum += payload[i];
  }

  return sum;
}

void lerTensaoGSM()
{
   payload.tensao_r = modem.getBattVoltage()/1000.0;
}

void publicar(payload_t[] DataOut){
  String publishingMsg = "field1=" + String(DataOut.colmeia, DEC) + "&field2=" + String(DataOut.temperatura, DEC) + "&field3=" + String(DataOut.umidade, DEC) + "&field4=" + String(DataOut.tensao_c, DEC) + "&field5=" + String(DataOut.tensao_r, DEC);
  String topicString = "channels/" + String( ChannelID ) + "/publish/"+String(WriteApiKey);

  int length = publishingMsg.length();
  char msgBuffer[length];
  publishingMsg.toCharArray(msgBuffer,length+1);

  length = topicString.length();
  char topicBuffer[length];
  topicString.toCharArray(topicBuffer,length+1);
  
  mqtt.publish(topicBuffer, msgBuffer);
  SerialMon.println("Erro: " + mqtt.state());

}

void sleepGSM() {
  modem.radioOff();

  digitalWrite(DTR_PIN, HIGH);                                // Puts DTR pin in HIGH mode so we can enter in sleep mode
  modem.sleepEnable(true);

}

void wakeGSM() {  
  digitalWrite(DTR_PIN, LOW);                                // Puts DTR pin in LOW mode so we can exit the sleep mode
  
  modem.sleepEnable(false);
 
}

