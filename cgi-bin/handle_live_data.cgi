#!/usr/bin/env python3
import serial, os
import matplotlib.pyplot as plt
from datetime import datetime

def find_arduino_port():
    for i in range(10):
        try:
            ard = serial.Serial(port=f"/dev/ttyACM{i}", baudrate=9600, timeout=0.1)
            ard.close()
            return i
        except:
            continue

    return -1   

def handle_serial_read():
    arduino = serial.Serial(port=f"/dev/ttyACM{port_num}", baudrate=9600, timeout=0.1)

    while True:
        data = arduino.read_until().decode('utf-8').strip()
        if data != "":
            arduino.close()
            return data
        
def handle_file_write(data):
    is_fresh = False
    
    with open('static/live_data.txt', 'r') as file:
        file_data = file.readlines()
        last_data = file_data[-1].strip() if file_data else ""

        if last_data == "" or int(last_data) != int(data):
            is_fresh = True

    if is_fresh:
        # Write data to the data file, and the time associated with the data gathered in the time file
        now = datetime.now()
        current_time = now.strftime("%H:%M:%S").strip()

        with open('static/live_data.txt', '+a') as file:
            file.write(f"{data}\n")
        
        with open('static/live_time.txt', 'a+') as file:
            file.write(f"{current_time}\n")

def handle_plot():
    x_vals = []
    y_vals = []

    with open('static/live_data.txt', 'r') as file:
        file_data = file.readlines()

        for i in range(len(file_data)):
            y = int(file_data[i].strip())
            y_vals.append(y)

    with open('static/live_time.txt', 'r') as file:
        file_data = file.readlines()

        for i in range(len(file_data)):
            h, m, s = file_data[i].strip().split(':')
            total_secs = int(h) * 3600 + int(m) * 60 + int(s)

            if i == 0:
                time_begin = total_secs
                x = 0
            else:
                x = total_secs - time_begin

            x_vals.append(x)

    # Plot x and y values
    plt.plot(x_vals, y_vals)
    plt.xlabel('Time in Seconds')
    plt.ylabel('Attendance Data')
    plt.title('Attendance Live Plot in Seconds')
    plt.savefig('static/live_plot.png')
        
if __name__ == "__main__":
    port_num = find_arduino_port()

    if port_num == -1:
        print(f"Content-type: text/plain\n\nError: Cannot find Arduino on any ACM port.\n")
    else:
        data = handle_serial_read()
        handle_file_write(data)
        handle_plot()
        print(f"Content-type: text/plain\n\n{data}\n")