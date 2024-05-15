CFLAGS = -Wall -Wextra -g
DFLAGS = -g -O0
CC = gcc

webserv: webserv.c
	$(CC) $(CFLAGS) -o webserv webserv.c my_threads.c cache.c

serial_com_html_res: serial_com_html_res.c
	$(CC) $(CFLAGS) -o serial_com_html_res.cgi serial_com_html_res.c

clean:
	rm -f *.o webserv
