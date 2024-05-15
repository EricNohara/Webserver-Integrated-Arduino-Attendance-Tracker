#!/usr/bin/python3
import os, serial, time

def find_arduino_port():
    for i in range(10):
        try:
            ard = serial.Serial(port=f"/dev/ttyACM{i}", baudrate=9600, timeout=0.1)
            ard.close()
            return i
        except:
            continue

    return -1    

def handle_serial_write(port_num):
    arduino = serial.Serial(port=f"/dev/ttyACM{port_num}", baudrate=9600, timeout=0.1)

    arduino.write(bytes("reset", 'utf-8'))
    time.sleep(0.05)

    html = f"""<!DOCTYPE html>
                <html lang="en">
                <head>
                    <meta charset="UTF-8">
                    <meta name="viewport" content="width=device-width, initial-scale=1.0">
                    <title>Document</title>
                    <style>
                        #return-btn {{
                            border-radius: 3px;
                            border: none;
                            font-size: 22px;
                            height: 40px;
                            margin-left: 20px;
                            background-color: #b8a79d;
                            padding-left: 20px;
                            padding-right: 20px;
                            cursor: pointer;
                        }}
                        #return-btn:hover {{
                            background-color: #746862;
                            color: white;
                        }}
                        body {{
                            display: flex;
                            align-items: center;
                            justify-content: center;
                        }}
                        .content {{
                            display: grid;
                        }}
                        img, h1, form {{
                            align-self: center;
                            justify-self: center;
                            margin: 24px;
                        }}
                        img {{
                                margin-top: 100px;
                            }}
                    </style>
                </head>
                <body>
                    <div class="content">
                        <img src="../static/checkmark.png" alt="checkmark image" width="300px" height="300px">
                        <h1>Successfully Reset Attendance Counter</h1>
                        <form id="returnForm">
                            <input type="button" id="return-btn" value="Return" onclick="window.location.href = '../cgi-bin/serial_com_html_res.cgi'">
                        </form>
                    </div>
                </body>
                </html>"""

    print(f"Content-type: text/html\n\n{html}\n")
    arduino.close()

def handle_serial_read():
    arduino = serial.Serial(port=f"/dev/ttyACM{port_num}", baudrate=9600, timeout=0.1)
    data = arduino.read_until().decode('utf-8')
    arduino.close()
    return data

def clear_live_data():
    # remove live plot
    os.remove('static/live_plot.png')

    # clear live_data.txt file and live_time.txt
    open('static/live_data.txt', 'w').close()
    open('static/live_time.txt', 'w').close()


if __name__ == "__main__":
    query_string = os.environ.get('QUERY_STRING', '')
    _, action = query_string.split("=")
    port_num = find_arduino_port()

    if port_num == -1:
        print(f"Content-type: text/plain\n\nError: Cannot find Arduino on any ACM port.\n")
    else:
        # save data to file if specified, then reset the counter
        if action == "True":
            data = handle_serial_read()

            with open('static/attendance_data.txt', 'a+') as file:
                file.write(data)
        
        clear_live_data()
        handle_serial_write(port_num)
