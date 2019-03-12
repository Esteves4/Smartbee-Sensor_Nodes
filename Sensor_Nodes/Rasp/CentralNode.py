#!/usr/bin/python
# coding=utf-8
import GsmClient as gsm 
import PubSubClient as MQTT 
import Adafruit_DHT
import RPi.GPIO as GPIO
import time 
import datetime 
import os
import sys, traceback
import logging
import logging.handlers
import threading
from struct import * 
from RF24 import * 
from RF24Network import *


LOG_FILENAME = './log/CentralNode.log'

# Create logger
logger = logging.getLogger("CentralNode-logger")
logger.setLevel(logging.DEBUG)

# Create console handler and set level to debug
handler = logging.handlers.RotatingFileHandler(LOG_FILENAME, maxBytes=1024000, backupCount = 5)
handler.setLevel(logging.DEBUG)

#Create formatter
formatter = logging.Formatter("%(asctime)s - %(name)s - %(levelname)s - %(message)s")

# Add formatter to handler
handler.setFormatter(formatter)

# Add ch to logger
logger.addHandler(handler)

# SIM800L Configuration
GPIO.setmode(GPIO.BOARD)

pino_rst = 11
GPIO.setup(11, GPIO.OUT, initial=GPIO.HIGH)

SerialAT = gsm.GsmClient('/dev/ttyAMA0', 57600)
mqtt = MQTT.PubSubClient(SerialAT)

#apn = "claro.com.br"
#user = "claro"
#password = "claro"

#apn = "timbrasil.br"
#user = "tim"
#password = "tim"

apn = "zap.vivo.com.br"
user = "vivo"
password = "vivo"

# MQTT Configuration
mqtt = MQTT.PubSubClient(SerialAT)

broker = "200.129.43.208"
user_MQTT = "teste@teste"
pass_MQTT = "123456"

topic_audio = "sensors/coleta_audio"
topic_data = "sensors/coleta_data"

# NRF24L01 Configuration
octlit = lambda n:int(n,8)

radio = RF24(RPI_GPIO_P1_22, RPI_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ)
network = RF24Network(radio)
this_node = octlit("00")

radio.begin()
time.sleep(0.1)
radio.setPALevel(RF24_PA_HIGH);                                # Set Power Amplifier level
radio.setDataRate(RF24_1MBPS);                              # Set transmission rate
radio.enableDynamicPayloads()
network.begin(120, this_node)    # channel 120
radio.printDetails()

# Control Variables
d_counter = 0
a_counter = 0
audio_count = 0

MAX_COUNTER = 12
MAX_AUDIO_COUNT = 760

bufferAudio = []

previousStart = False
dataReady = False
audioReady = False


th = None                                                    # GSM Thread

# Sensor temperatura externa
# Tipo sensor

sensor = Adafruit_DHT.DHT22
temp_ext = 0.0
umid_ext = 0.0

start_delay = 0
reading_enable = True

# GPIO conectada
pino_sensor = 23

if not os.path.exists("data_collect/"):
	os.makedirs("data_collect/")
if not os.path.exists("data_to_send/"):
	os.makedirs("data_to_send/")

if not os.path.exists("audio_collect/"):
	os.makedirs("audio_collect/")
if not os.path.exists("audio_to_send/"):
	os.makedirs("audio_to_send/")

if not os.path.exists("counter.txt"):
	with open("counter.txt","w") as file:
		file.write(str(0)+","+str(0) + '\n')

with open("counter.txt","r") as file:
	line = file.readline()
	line = line.split(",")
	d_counter = int(line[0])
	a_counter = int(line[1])

def gsmSend():
	global SerialAT, apn, user, password, mqtt, user_MQTT, pass_MQTT, broker, topic_data, topic_audio

	if not connection_gsm(SerialAT,apn, user, password):
		logger.error("Erro na conexão com rede gsm")
	elif not connection_mqtt(mqtt, user_MQTT, pass_MQTT, broker):
		logger.error("Erro na conexão com servidor MQTT")
	else:
		publish_MQTT(mqtt, topic_data, "data_to_send/buffer_data.txt", "data_to_send/temp.txt")

	if not connection_mqtt(mqtt, user_MQTT, pass_MQTT, broker):
		logger.error("Erro na conexao com servidor MQTT")
	else:
		publish_MQTT(mqtt, topic_audio, "audio_to_send/buffer_audio.txt", "audio_to_send/temp.txt")

def receiveData():
	if(network.available()):
		header, payload = network.read(201)
		
		if header.type == 68:
			bufferData = list(unpack('<b5fb',bytes(payload)))
			return False, False, True, False, bufferData
		elif header.type == 65:
			bufferData = list(unpack('<b50H', bytes(payload)))
			return False, False, False, True, bufferData
		elif header.type == 83:
			print("START")
			return True, False, False, False, [0]
		elif header.type == 115:
			print("STOP")
			return False, True, False, False, [0]
		
	return False, False, False, False, [0]
def setRaspTimestamp(gsm, apn, user, password):
	if not connection_gsm(gsm,apn, user, password):
		return False
	
	time = gsm.getGsmTime()
	time = time.split(',')
	
	if int(time[0]) != 0:
		return False
	os.system("sudo date +%Y/%m/%d -s "+time[1])
	os.system("sudo date +%T -s " + time[2] + " -u")
	os.system("sudo date")
	print(time)
	
	return True

def getTimeStamp():
	timestamp = datetime.datetime.now()
	
	return timestamp

def toString(buffer):
	for i in range(0, len(buffer)):
		if type(buffer[i]) == float:
			buffer[i] = "%3.2f" %buffer[i]
		else:
			buffer[i] = str(buffer[i])
		
	return ",".join(buffer)	

def saveDataToSD(buffer, timestamp, isLast):

	buffer.append(timestamp.strftime("%Y%m%d%H%M%S"))
	msg = toString(buffer)


	with open("data_collect/"+timestamp.strftime("%d_%m_%y") + ".txt", "a") as file:
		file.write(msg + '\n')

	with open("data_to_send/buffer_data.txt", "a") as file:

		if isLast:
			msg += '\n'
		else:
			msg += '/'

		file.write(msg)

def saveAudioToSD(buffer, timestamp, isLast):
	buffer.append(timestamp.strftime("%Y%m%d%H%M%S"))
	
	msg = toString(buffer)

	with open("audio_collect/"+timestamp.strftime("%d_%m_%y") + ".txt", "a") as file:
		file.write(msg+'\n')

	#Selecionando 100 amostras do audio
	if len(buffer)>100:
		msg = toString(buffer[0:101]) 
	else:
		msg = toString(buffer)

	msg = msg + "," + timestamp.strftime("%Y%m%d%H%M%S")
 
	with open("audio_to_send/buffer_audio.txt", "a") as file:
		if isLast:
			file.write(msg + '\n')
		else:
			file.write(msg + '/')

def connection_gsm(gsmClient, apn, user, password):
	if not gsmClient.waitForNetwork():
		return False
	return gsmClient.gprsConnect(apn,user,password)
	
def connection_mqtt(mqttClient, user, password, broker):
	
	mqttClient.setServer(broker, 1883)

	return mqttClient.connect("CentralNode", user, password) 
	
def publish_MQTT(mqttClient, topic, file_source, file_temp):
	all_sent = True

	with open(file_temp, "a") as file2:
		with open(file_source, "r") as file:
			count = 1
			for line in file:
				if count > 12:
					file2.write(line)
				else:	
					if not mqttClient.publish(topic,line.rstrip('\n')):
						file2.write(line)
						all_sent = False		
				count += 1
				time.sleep(1)
	os.remove(file_source)
	os.rename(file_temp,file_source)

	return all_sent
def updateCounter(new_d_counter, new_a_counter):
	with open("counter.txt","w") as file:
		file.write(str(new_d_counter) + "," + str(new_a_counter)+ '\n')


#Descomentar linha abaixo para atualizar data e hora pelo SIM800L
#setRaspTimestamp(SerialAT, apn, user, password)

try:

	while(1):
		network.update()
		
		startReceived, stopReceived, dataReceived, audioReceived, bufferData = receiveData()
		
		if(startReceived):
			reading_enable = False
			if(previousStart):
				if len(bufferAudio)> 2:
					if a_counter == 2:
						saveAudioToSD(bufferAudio[1:], bufferAudio[0], True)
						a_counter = 0
						updateCounter(d_counter, a_counter)
					else:
						saveAudioToSD(bufferAudio[1:], bufferAudio[0], False)
						a_counter += 1
						updateCounter(d_counter, a_counter)

					del bufferAudio[:]
					audio_count = 0

					if (dataReady):
						audioReady = True

				else:
					previousStart = True

		elif(stopReceived):
			reading_enable = True
			if len(bufferAudio) > 2:
				if a_counter == 2:
					saveAudioToSD(bufferAudio[1:], bufferAudio[0], True)
					a_counter = 0
					updateCounter(d_counter, a_counter)
				else:
					saveAudioToSD(bufferAudio[1:], bufferAudio[0], False)
					a_counter += 1
					updateCounter(d_counter, a_counter)

				del bufferAudio[:]
				audio_count = 0

				if (dataReady):
					audioReady = True

			previousStart = False

		elif(dataReceived):
			timestamp = getTimeStamp()

			if d_counter == MAX_COUNTER - 1:
				saveDataToSD(bufferData, timestamp, True)

				dataReady = True
		
			else:
				saveDataToSD(bufferData, timestamp, False)
				d_counter += 1
				updateCounter(d_counter, a_counter)

		elif(audioReceived):
			if(audio_count == 0):
				timestamp = getTimeStamp()
				bufferAudio.append(timestamp)
				bufferAudio.extend(bufferData)
				reading_enable = False
			else:
				bufferAudio.extend(bufferData[1:])
			
			if(audio_count == MAX_AUDIO_COUNT - 1):
				if a_counter == 2:
					saveAudioToSD(bufferAudio[1:], bufferAudio[0], True)
					a_counter = 0
					updateCounter(d_counter, a_counter)		
				else:
					saveAudioToSD(bufferAudio[1:], bufferAudio[0], False)			
					a_counter += 1
					updateCounter(d_counter, a_counter)		
				del bufferAudio[:]	
				
				if(dataReady):
					audioReady = True

				audio_count = 0
				reading_enable = True

			else:
				audio_count += 1
		else:
			end_delay = time.time()

			if(end_delay - start_delay > 10 and reading_enable):

				umid, temp = Adafruit_DHT.read_retry(sensor=sensor, pin=pino_sensor)

				if umid is not None and temp is not None:
					umid_ext = umid
					temp_ext = temp

				start_delay = time.time()

		
		if audioReady and dataReady:

			send_ok = True
					
			if not connection_gsm(SerialAT,apn, user, password):
				logger.error("Erro na conexão com rede gsm")
				send_ok = False
			elif not connection_mqtt(mqtt, user_MQTT, pass_MQTT, broker):
				logger.error("Erro na conexão com servidor MQTT")			
				send_ok = False
			elif not publish_MQTT(mqtt, topic_data, "data_to_send/buffer_data.txt", "data_to_send/temp.txt"):
				logger.error("Erro no envio dos dados via MQTT")
				send_ok = False

			if not connection_mqtt(mqtt, user_MQTT, pass_MQTT, broker):
				logger.error("Erro na conexao com servidor MQTT")
				send_ok = False

			elif not publish_MQTT(mqtt, topic_audio, "audio_to_send/buffer_audio.txt", "audio_to_send/temp.txt"):
				logger.error("Erro no envio dos audios via MQTT")
				send_ok = False

					
			if not send_ok:
				
				logger.warning("GSM reiniciado por Software e Hardware!")

				#Hardware reset
				GPIO.output(pino_rst, GPIO.LOW)
				time.sleep(1)
				GPIO.output(pino_rst, GPIO.HIGH)
				time.sleep(1)

				SerialAT.restart()
				

			#if th is not None:
			#	th.join()

			#th = threading.Thread(target=gsmSend)
			#th.start()
			
			d_counter = 0
			a_counter = 0
			audio_count = 0
			audioReady = False
			dataReady = False

			updateCounter(a_counter, d_counter)

			# NRF24L01 Reset
			del network

			network = RF24Network(radio)
			network.begin(120, this_node)    # channel 120
			radio.printDetails()
			reading_enable = True
			
except KeyboardInterrupt:
	traceback.print_exc(file=sys.stdout)
	GPIO.cleanup()
	print("\nGoodbye!\n")
	sys.exit()
except:
	GPIO.cleanup()
	logger.error(traceback.print_exc(file=sys.stdout))
	sys.exit()
	


