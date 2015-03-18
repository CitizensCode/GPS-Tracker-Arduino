import serial
import requests

ser = serial.Serial('/dev/tty.usbmodem1451', 115200)

postURL = "http://citizenscode-gpstracker-api.herokuapp.com/"

while True:
    coord = ser.readline()
    coord = coord.strip('\n\r').split(',')
    postCoords = {"lat": coord[0], "lng": coord[1]}
    print postCoords
    requests.post(postURL, data=postCoords)
    print "Posted."
