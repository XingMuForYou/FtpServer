main: *.h *.c
	g++  -Wall *.h *.c -lpthread -o FtpServer
clean:
	rm FtpServer
