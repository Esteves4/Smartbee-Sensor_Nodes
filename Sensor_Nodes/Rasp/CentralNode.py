import GsmClient as gsm
import PubSubClient as mqtt

SerialAT = gsm.GsmClient('/dev/ttyAMA0', 57600)
SerialAT.waitForNetwork()
SerialAT.gprsConnect("timbrasil.br","tim","tim")
