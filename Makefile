CFLAGS = -Wall -Wextra -g
DFLAGS = -g -O0
CC = gcc

webserv: webserv.c
	$(CC) $(CFLAGS) -o webserv webserv.c my_threads.c howell_cache.c

howell_cache_webserv: howell_cache_webserv.c howell_cache.c
	$(CC) $(CFLAGS) -o howell_cache_webserv howell_cache_webserv.c howell_cache.c my_threads.c 

serial_com_html_res: serial_com_html_res.c
	$(CC) $(CFLAGS) -o serial_com_html_res.cgi serial_com_html_res.c

clean:
	rm -f *.o webserv test-serial howell_cache_webserv
