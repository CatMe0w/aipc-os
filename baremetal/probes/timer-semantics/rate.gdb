set confirm off
target remote /dev/cu.usbmodem00011

# count = 0x3ffffff, EN (bit 26), LOAD (bit 27)
set *(unsigned int *)0x0800001C = 0x0FFFFFFF

shell python3 -c "import time;print('HOSTTIME',time.time())"
x/1xw 0x08000104
shell python3 -c "import time;print('HOSTTIME',time.time())"
shell sleep 3
shell python3 -c "import time;print('HOSTTIME',time.time())"
x/1xw 0x08000104
shell python3 -c "import time;print('HOSTTIME',time.time())"
shell sleep 3
shell python3 -c "import time;print('HOSTTIME',time.time())"
x/1xw 0x08000104
shell python3 -c "import time;print('HOSTTIME',time.time())"
shell sleep 3
shell python3 -c "import time;print('HOSTTIME',time.time())"
x/1xw 0x08000104
shell python3 -c "import time;print('HOSTTIME',time.time())"
