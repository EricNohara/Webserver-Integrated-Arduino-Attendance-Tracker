#!/usr/bin/python3

import os, stat
import matplotlib.pyplot as plt
import base64

count = os.environ.get('COUNT', '')

html_content = f"""
            <!DOCTYPE html>
            <html>
            <head>
            <title>CS410 Webserver</title>
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
            </style>
            </head>
            <body>
            <h1>CS410 Webserver</h1>
            <br>
            <a href="http://localhost:8080/cgi-bin/button_test.cgi">
            <button>Get Count</button>
            </a>
            <br>
            {count}
            <br>
            </body>
            </html>
            """


print(f"Content-type: text/html\n\n{html_content}") # send to client as text/html
