import time
import serial

class GsmClient:
  def __init__(self, port, baudrate):
    self.ser = serial.Serial('/dev/ttyAMA0', 57600, timeout=5)
    time.sleep(1)
    
  def begin(self):
    return init()
  
  def millis(self):
    return time.time() * 1000
      
  def init(self):
    if(not testeAT()):
      return False
    
    sendAT("&FZ") #Factory + Reset
    waitResponse()
    
    sendAT("&E0") #Echo off
    if(waitResponse() != 1):
      return False
    
    getSimStatus()
    return True
    
  def setBaud(self,baud):
    sendAT("+IPR=", baud)
  
  def testAT(self,timeout = 10000):
    start = self.millis()
    while self.millis() - start < timeout:
      sendAT("")
      
      if(waitResponse(200) == 1):
        time.sleep(0.1)
        return True
      
      time.sleep(0.1)
      
    return False
  
  def factoryDefault(self):
    sendAT("&FZE0&W")  # Factory + Reset + Echo Off + Write
    waitResponse()
    sendAT("+IPR=0")   # Auto-baud
    waitResponse()
    sendAT("+IFC=0,0") # No Flow Control
    waitResponse()
    sendAT("+IFC=3,3") # 8 data 0 parity 1 stop
    waitResponse()
    sendAT("+CSCLK=0") # Disable Slow Clock
    waitResponse()
    sendAT("&W")       # Write configuration
    return waitResponse() == 1
  
  def restart(self):
    if(not testAT()):
      return False
    
    sendAT("+CLTS=1")
    if waitResponse(10000) != 1:
      return False
    
    sendAT("&W") 
    waitResponse()
    sendAT("+CFUN=0")
    if waitResponse(10000) != 1:
      return False
    sendAT("+CFUN=1,1")
    if waitResponse(10000) != 1:
      return False
    
    time.sleep(3)
    
    return init()
  
  # def getRegistrationStatus():
  #   #TO_DO 
  #   sendAT("+CREG?")
  #   if(waitResponse("\r\n+CREG:") != 1):
  #     return REG_UNKOWN 
  #   streamskipUntil(',') #Skip format (0)
  #   status = stream.readStringUntil('\n').toInt()
  #   waitResponse()
    
  #   return (RegStatus)status
        
  def isNetworkConnected(self):
    #s = getRegistrationStatus()
    #return (s == REG_OK_HOME || s == REG_OK_ROAMING)
    sendAT("+CREG?")
    return waitResponse("\r\n+CREG:")
    
  def waitForNetwork(self,timeout = 60000):
    start = self.millis()
    while self.millis() - start < timeout:   
      if(isNetworkConnected()):
        return True
      time.sleep(0.25)
    return False
  
  def gprsConnect(self,apn, user = None, pwd = None):
    gprsDisconnect()
    
    # Set the Bearer for the IP 
    sendAT("+SAPBR=3,1,\"Contype\",\"GPRS\"")  # Set the connection type to GPRS
    waitResponse()
    
    sendAT("+SAPBR=3,1,\"APN\",\"", apn, '"') # Set the APN
    waitResponse()
    
    if(user and len(user) > 0):
      sendAT("+SAPBR=3,1,\"USER\",\"", user, '"') # Set the user name
      waitResponse()
      
    if(pwd and len(pwd) > 0):
      sendAT("+SAPBR=3,1,\"PWD\",\"", pwd, '"') # Set the password
      waitResponse()
      
    # Define the PDP context
    sendAT("+CGACT=1,1")
    waitResponse(60000)
    
    # Open the defined GPRS bearer context
    sendAT("+SAPBR=1,1")
    waitResponse(85000)
    
    # Query the GPRS bearer context status
    sendAT("+SAPBR=2,1")
    if(waitResponse(30000) != 1):
      return False
    
    # Attach to GPRS
    sendAT("+CGATT=1")
    if(waitResponse(60000) != 1):
      return False
    
    # Set to multi-IP
    sendAT("+CIPMUX=1")
    if(waitResponse() != 1):
      return False
    
    # Put in "quick send" mode (thus no extra "Send OK")
    sendAT("+CIPQSEND=1")
    if(waitResponse() != 1):
      return False
    
    # Set to get data manually
    sendAT("+CIPRXGET=1")
    if(waitResponse() != 1):
      return False
    
    # Start Task and Set APN, USER NAME, PASSWORD
    sendAT("+CSTT=\"", apn, "\",\"", user, "\",\"", pwd, "\"")
    if(waitResponse(60000) != 1):
      return False
    
    # Bring Up Wireless Connection with GPRS or CSD
    sendAT("+CIICR")
    if(waitResponse(60000) != 1):
      return False
    
    # Get Local IP Address, only assigned after connection
    sendAT("+CIFSR;E0")
    if(waitResponse(10000) != 1):
      return False
    
    # Configure Domain name Server (DNS)
    sendAT("+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"")
    if(waitResponse() != 1):
      return False
    
    return True
  
  def gprsDisconnect(self):
    # Shut the TCP/IP connection
    sendAT("+CIPSHUT")
    if(waitResponse(60000) != 1):
      return False
    
    sendAT("+CGATT=0")  # Deactivate the bearer context
    if(waitResponse(60000) != 1):
      return False
    
    return True
  
  def sendAT(self,*argv):    
    streamWrite("AT", argv, "\r\n")
    stream.flush()
    
  def streamWrite(self,head, *tail):
    for i in tail:
      head += i
    self.ser.write(head.encode())
    time.sleep(1)
    
  def waitResponse(self,timeout, r1 = 'OK\r\n', r2 = 'ERROR\r\n', r3 = None, r4 = None, r5=None):
    start = self.millis()
    data = ''
    while self.millis() - start < timeout:
      while self.ser.in_waiting > 0:
        a = self.ser.read()

        if a <= 0:
          continue
        data += a

        if r1 and data.endsWith(r1) :
          print(data)
          return True
        elif r2 and data.endsWith(r2) :
          print(data)
          return True
        elif r3 and data.endsWith(r3) :
          print(data)
          return True
        elif r4 and data.endsWith(r4) :
          print(data)
          return True
        elif r5 and data.endsWith(r5) :
          print(data)
          return True
        #falta colocar o resto
        return False
    
  def streamSkipUntil(self):
    start = self.millis()
    while self.millis() - start < timeout:   
      while self.millis() - start < timeout and self.ser.in_waiting <= 0:
        if(self.ser.read() == c):
          return True
      
    return False
    