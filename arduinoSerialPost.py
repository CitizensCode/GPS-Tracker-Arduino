import serial
import requests
import logging
import httplib as http_client
http_client.HTTPConnection.debuglevel = 1

# Connect to the Arduino's serial port
ser = serial.Serial('/dev/tty.usbmodem1451', 115200)

postURL = "http://citizenscode-gpstracker-api.herokuapp.com/"

# Initialize logging
logging.basicConfig() 
logging.getLogger().setLevel(logging.DEBUG)
requests_log = logging.getLogger("requests.packages.urllib3")
requests_log.setLevel(logging.DEBUG)
requests_log.propagate = True

# Loop and post the lat/lng
while True:
    coord = ser.readline()
    coord = coord.strip('\n\r').split(',')
    postCoords = {"lat": coord[0], "lng": coord[1]}
    print postCoords
    requests.post(postURL, data=postCoords)
    print "Posted."
