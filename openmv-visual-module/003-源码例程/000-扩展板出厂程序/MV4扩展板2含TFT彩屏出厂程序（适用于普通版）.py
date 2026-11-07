# LCD Example
#
# Note: To run this example you will need a LCD Shield for your OpenMV Cam.
#
# The LCD Shield allows you to view your OpenMV Cam's frame buffer on the go.

import display
import sensor, image, time
from pyb import UART, Pin, Timer
from pyb import millis

sensor.reset()  # Initialize the camera sensor.
sensor.set_pixformat(sensor.RGB565)  # or sensor.GRAYSCALE
sensor.set_framesize(sensor.LCD)  # Special 128x160 framesize for LCD Shield.
lcd = display.SPIDisplay()  # Initialize the lcd screen.

while True:
    img = sensor.snapshot()
    lcd.write(sensor.snapshot())  # Take a picture and display the image.
