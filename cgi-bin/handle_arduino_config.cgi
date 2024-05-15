#!/usr/bin/env python3
import os, serial, time

def handle_serial_write(r, d, t):
    found = False

    for i in range(10):
        try:
            arduino = serial.Serial(port=f"/dev/ttyACM{i}", baudrate=9600, timeout=0.1)
            found = True
            break
        except:
            continue

    if not found:
        print(f"Content-type: text/plain\n\nError: could not connect to Arduino port\n")
        return

    arduino.write(bytes(f"{r}%{d}%{t}", 'utf-8'))
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
                            <h1>Successfully Configured Attendance Counter</h1>
                            <form id="returnForm">
                                <input type="button" id="return-btn" value="View Data" onclick="window.location.href = '../cgi-bin/serial_com_html_res.cgi'">
                            </form>
                        </div>
                    </body>
                    </html>"""

    print(f"Content-type: text/html\n\n{html}\n")

if __name__ == "__main__":
    query_string = os.environ.get('QUERY_STRING', '')
    range_input, delay_input, total_input = query_string.split("&")
    _, r = range_input.split("=")
    _, d = delay_input.split("=")
    _, t = total_input.split("=")

    handle_serial_write(r, d, t)
