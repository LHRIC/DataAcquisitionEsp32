b:
	idf.py build
f:
	idf.py -p /dev/ttyACM0 flash
m:
	idf.py -p /dev/ttyACM0 monitor
