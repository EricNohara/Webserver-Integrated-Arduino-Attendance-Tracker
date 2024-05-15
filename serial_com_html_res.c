#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h> // write(), read(), close()

int serial_port = 0;

const char* format_html_res = "<html>\n"
                              "<head>\n"
                              "    <title>Attendance Data</title>\n"
                              "    <style>\n"
                              "        body {\n"
                              "            font-family: Arial, sans-serif;\n"
                              "            margin: 0;\n"
                              "            padding: 0;\n"
                              "            background-color: #f4f4f4;\n"
                              "            color: #333;\n"
                              "            display: flex;\n"
                              "            align-items: center;\n"
                              "            justify-content: center;\n"
                              "        }\n"
                              "        h1 {\n"
                              "            color: #333;\n"
                              "            text-align: center;\n"
                              "            font-size: 50px;\n"
                              "        }\n"
                              "        #data {\n"
                              "            font-size: 70px !important;\n"
                              "            margin: 40px;\n"
                              "        }\n"
                              "        form {\n"
                              "            text-align: center;\n"
                              "            margin-top: 20px;\n"
                              "        }\n"
                              "        input[type='button'] {\n"
                              "            padding: 14px 28px;\n"
                              "            color: white;\n"
                              "            border: none;\n"
                              "            border-radius: 5px;\n"
                              "            cursor: pointer;\n"
                              "            font-size: 20px;\n"
                              "            margin: 12px;\n"
                              "        }\n"
                              "        input[value='Update Data'] {\n"
                              "            background-color: #4CAF50;\n"
                              "        }\n"
                              "        input[value='Update Data']:hover {\n"
                              "            background-color: #45a049;\n"
                              "        }\n"
                              "        input[value='Live Mode'] {\n"
                              "            background-color: #3498DB;\n"
                              "        }\n"
                              "        input[value='Live Mode']:hover {\n"
                              "            background-color: #2E86C1;\n"
                              "        }\n"
                              "        input[value='Plot Data'] {\n"
                              "            background-color: #FFBF00;\n"
                              "        }\n"
                              "        input[value='Plot Data']:hover {\n"
                              "            background-color: #E49B0F;\n"
                              "        }\n"
                              "        input[value='Reset Data'] {\n"
                              "            background-color: #EC5800;\n"
                              "        }\n"
                              "        input[value='Reset Data']:hover {\n"
                              "            background-color: #ba1f00;\n"
                              "        }\n"
                              "        input[value='Email'] {\n"
                              "            background-color: #0047AB;\n"
                              "        }\n"
                              "        input[value='Email']:hover {\n"
                              "            background-color: #003682;\n"
                              "        }\n"
                              "        input[value='Exit'] {\n"
                              "            background-color: #8B0000;\n"
                              "        }\n"
                              "        input[value='Exit']:hover {\n"
                              "            background-color: #5e0000;\n"
                              "        }\n"
                              "        input[type='email'] {\n"
                              "            padding: 12px;\n"
                              "            border: 2px solid #ccc;\n"
                              "            border-radius: 5px;\n"
                              "            width: 300px;\n"
                              "            margin-bottom: 4px;\n"
                              "            font-size: 20px;\n"
                              "        }\n"
                              "        input[type='email']:hover {\n"
                              "            background-color : #c6c6c6;\n"
                              "        }\n"
                              "    </style>\n"
                              "</head>\n"
                              "<body>\n"
                              "    <div>\n"
                              "        <h1>Current Attendance:</h1>\n"
                              "        <h1 id='data'>%s</h1>\n"
                              "        <form>\n"
                              "            <input type='button' value='Update Data' onClick=\"window.location.href='serial_com_html_res.cgi'\">\n"
                              "            <input type='button' value='Live Mode' onClick=\"window.location.href='../static/live-mode.html'\">\n"
                              "            <input type='button' value='Plot Data' onClick=\"window.location.href='handle_plot.cgi?data=%d'\">\n"
                              "            <input type='button' value='Reset Data' onClick=\"window.location.href='../static/reset_page.html'\">\n"
                              "            <input type='button' value='Exit' onClick=\"window.location.href='../static/project.html'\">\n"
                              "        </form>\n"
                              "        <form>\n"
                              "            <input type='email' id='emailInput' placeholder='Enter your email'>\n"
                              "            <input type='button' value='Email' id='email-btn' onClick=\"window.location.href='handle_email.cgi'\">\n"
                              "        </form>\n"
                              "    </div>\n"
                              "</body>\n"
                              "<script>\n"
                              "document.getElementById('email-btn').addEventListener('click', function(e) {\n"
                              "e.preventDefault();\n"
                              "const email = document.getElementById('emailInput').value.trim();\n"
                              "window.location.href = '../cgi-bin/handle_email.cgi?email=' + email + '&data=' + %s;\n"
                              "});\n"
                              "</script>\n"
                              "</html>\n";

int find_arduino_port()
{
    char path[20];

    for (int i = 0; i < 10; ++i) {
        sprintf(path, "/dev/ttyACM%d", i);

        if ((serial_port = open(path, O_RDWR)) != -1)
            return i;
    }
    return -1; // Not found
}

int main()
{
    // Open the serial port. Change device path as needed (currently set to an standard FTDI USB-UART cable type device)
    int port_num = find_arduino_port();
    if (serial_port == -1) {
        perror("Error: could not find Arduino!\n");
        return 1;
    }

    // Create new termios struct, we call it 'tty' for convention
    struct termios tty;

    // Read in existing settings, and handle any error
    if (tcgetattr(serial_port, &tty) != 0) {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        return 1;
    }

    tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
    tty.c_cflag |= CS8; // 8 bits per byte (most common)
    tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

    tty.c_cc[VTIME] = 10; // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 0;

    // Set in/out baud rate to be 9600
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    // Save tty settings, also checking for error
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        return 1;
    }

    char read_buf[256]; // Allocate memory for read buffer

    memset(&read_buf, '\0', sizeof(read_buf)); // initialize everything to null

    int status = 0;

    // Read bytes until a newline character is encountered, then print the received message
    while (1) {
        char* newline_pos = strchr(read_buf, '\n'); // Find the newline character
        if (newline_pos != NULL) {
            *newline_pos = '\0'; // Null-terminate the string at the newline position
            printf("HTTP/1.1 200 OK\r\n");
            printf("Content-Type: text/html\r\n\r\n");
            printf(format_html_res, read_buf, atoi(read_buf), read_buf);
            fflush(stdout);
            break; // exit the loop
        } else {
            // No newline character found, continue reading more data
            int num_bytes = read(serial_port, read_buf + strlen(read_buf), sizeof(read_buf) - strlen(read_buf) - 1);
            if (num_bytes < 0) {
                printf("Error reading: %s\n", strerror(errno));
                status = 1;
                break;
            }
            read_buf[strlen(read_buf) + num_bytes] = '\0'; // Null-terminate buffer
        }
    }

    close(serial_port);

    return status == 1 ? 1 : 0; // return correct exit code
};