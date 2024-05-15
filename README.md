# CS 410 Assignment 3: Attendance Metric Tracker
## Contributors: Eric, Howell, Kelvin

# Important Webserver Notes:
- Machine Specifications: x86_64 GNU/Linux, gcc version 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04)
- Must set environment variable absolute path to directory of webserver
```
export WEBROOT_PATH="/path/to/webserv"
```
- Must set -p flag to specify port number, -t to specify threaded mode, -c size to specify cache and size
```
./webserv -p portnum [-t] [-c size]
```
- must set all CGI scripts as executable before use
```
chmod +755 script.cgi
```
- If using WSL, must forward USB port using usbipd library

## Testing:
- Make webserv using command...
```
make webserv
```
- All static files are located in /static directory
- All CGI scripts are located in cgi-bin directory
- To list a directory, make a client request for the name of the directory
- To test my_histogram, pass in starting directory as a parameter in the query string like so:
- http://localhost:port-number/cgi-bin/my_histogram.cgi?directory=/path/to/directory
- If on Macos, you may need to include the following definition in the relevant C files
- #define _POSIX_C_SOURCE 200809L

## Multi Threaded Web Server
- Implemented using setjmp, longjmp to change contexts, and sigaltstack to create alternate stacks for each thread
- Add -t flag to indicate whether the server is to be ran as a multi threaded process or not
```
./webserv -p port-number [-t] [-c cache-size]
```

## Cached Weserver
- Add -c flag along with cache size to indicate whether the server is to be ran with a cache or not
```
./webserv -p port-number [-t] [-c cache-size]
```
- To request a file on another server, include the server ip/host name as a parameter in the client request as follows:
- http://localhost:port-number/wireshark-labs/INTRO-wireshark-file1.html?server=128.119.245.12:80
- If the port number is not specified, the program assumes the remote server uses port 80. You can also use the host name instead of the ip address.
- http://localhost:port-number/wireshark-labs/INTRO-wireshark-file1.html?server=gaia.cs.umass.edu  

# Project Details
- Use 2 IR sensors, one on each side of an open doorway
- If sensor 1 then sensor 2 detects, that means someone entered, and if opposite, someone exited
- Arduino keeps track of number of people entering/exiting a room and keeps a running total
- Interacts with our web server by updating the running total server side
- Arduino can also get input from webserver to reset various default settings
- GET request to web server from client returns current total
- Data resetting and plotting of session data
- Live plotting capabilities
- Data emailed to user
"# Webserver-Integrated-Arduino-Attendance-Tracker" 
"# Webserver-Integrated-Arduino-Attendance-Tracker" 