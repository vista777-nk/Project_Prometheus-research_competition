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

uart3 = UART(3, 115200)
uart3.init(115200,8,None,1)

btn1 = Pin('P7', Pin.IN)
btn2 = Pin('P9', Pin.IN)

while True:
    img = sensor.snapshot()
    lcd.write(sensor.snapshot())  # Take a picture and display the image.

    if btn1.value() == 0:
        uart3.write('#255P1400T1000!')
        while btn1.value() == 0:
            pass
        #time.sleep_ms(100)

    if btn2.value() == 0:
        uart3.write('#255P1600T1000!')
        while btn2.value() == 0:
            pass
        #time.sleep_ms(100)

    if uart3.any():
        data = uart3.read()
        uart3.write(data)
