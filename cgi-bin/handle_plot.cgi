#!/usr/bin/env python3
import matplotlib.pyplot as plt

x_vals = []
y_vals = []

with open('static/attendance_data.txt', 'r') as file:
    x = 1
    data = file.readline()
    if data == "":
        print(f"Content-type: text/plain\n\nNo data saved to be plotted! Please save data by clicking reset and clicking the yes button.\n")
        
    while data != "":
        y = int(data.replace(data.lstrip('0123456789'), ''))
        x_vals.append(x)
        y_vals.append(y)

        # read next line in file
        data = file.readline()
        x += 1

# Plot x and y values
plt.plot(x_vals, y_vals)
plt.xlabel('Data Session Number')
plt.ylabel('Attendance Value')
plt.title('Attendance Trends Per Data Session')
plt.savefig('static/attendance_plot.png')

# Generate the HTML page
html_content = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>Attendance Data Plot</title>
        <style>
            body {{
                background-color: white;
            }}
            h1 {{
                font-size: 16pt;
                color: red;
                text-align: center;
            }}
            img {{
                display: block;
                margin: auto;
            }}
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
            form {{
                display: flex;
                align-items: center;
                justify-content: center;
                margin-top: 50px;
            }}
        </style>
    </head>
    <body>
        <h1>Attendance Data Plot</h1>
        <br>
        <img src="/static/attendance_plot.png" alt="Histogram">
        <form id="returnForm">
            <input type="button" id="return-btn" value="Return" onclick="window.location.href = '../cgi-bin/serial_com_html_res.cgi'">
        </form>
    </body>
    </html>
    """

print(f"Content-type: text/html\n\n{html_content}\n")