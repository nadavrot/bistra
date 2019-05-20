#!/usr/bin/env python

import sys
from PIL import Image

# This script is used to visualize memory access patterns.
#
# Usage: ./visualize.py dump.txt
#
# The script will dump a sequence of bitmap files that can be combined into a
# video. Example: heap100123.bmp, heap heap100124.bmp, heap100125.bmp ...  )
#
# On mac and linux this command will generate a gif file:
#    convert -delay 10 -loop 0 *.bmp video.gif
#
# The input file should contain a list of access commands.
# Allocation commands, marked with the letter 'a', followed by the buffer
# index, and coordinates. You can generate these command lists by inserting
# printf calls into the low-level memory access routines.
#
# Example input:
#    a 1 30 40 // Access buffer #1 at the coordinate (30,40)

content = open(sys.argv[1]).read()
lines = content.split('\n')

canvas_size = 180
pixelsize = 8

img = Image.new("RGB", (canvas_size * 3, canvas_size), "black")
pixels = img.load()


# Use this number to assign file names to frames in the video.
filename_counter = 10000000

# Maps from address to size
sizes={}

color_index = 0
colors=[(218, 102, 114), (218, 102, 114), (105, 255, 193), (150, 135, 215), (255, 250, 205),
        (210, 105, 30), (210, 180, 140), (188, 143, 143), (255, 240, 245),
        (230, 230, 250), (255, 255, 240)]

def getColor(idx):
    return colors[idx]
    global color_index
    color_index+=1
    return colors[color_index % len(colors)]

def fade():
    for x in range(canvas_size * 3):
        for y in range(canvas_size):
            r,g,b = pixels[x, y]
            if (r > 0): r = r - 1
            if (g > 0): g = g - 1
            if (b > 0): b = b - 1
            pixels[x, y] = (r, g, b)

def setPixel(x, y, width, color):
    for i in range(width):
        if ((i + x) < canvas_size * 3):
            pixels[x + i, y] = color

def saveFrame():
    global filename_counter
    filename_counter+=1
    img.save("heap" + str(filename_counter) + ".bmp")

cnt = 0
for line in lines:
    tokens = line.split()
    if (len(tokens) < 1): break

    print(tokens)
    if (tokens[0] == 'a'):
        buff = int(tokens[1])
        x = int(tokens[3]) + ((buff - 1) * canvas_size)
        y = int(tokens[2])
        cc = getColor(buff)
        print (x, y, cc)
        if (buff == 2):
            setPixel(x, y, 1, cc)
        else:
            setPixel(x, y, 8, cc)
        cnt = cnt + 1
        if (cnt % 153 == 0):
            saveFrame()
        if (cnt % 15 == 0):
           fade()
