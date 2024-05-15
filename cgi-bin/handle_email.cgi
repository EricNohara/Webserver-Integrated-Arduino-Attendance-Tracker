#!/usr/bin/python3
import os, smtplib, re
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.image import MIMEImage

def handle_email_sending():
    # parse out the email and date from the query string
    query_string = os.environ.get('QUERY_STRING', '')
    email_arg, data_arg = query_string.split("&")
    _,  receiver = email_arg.split("=")
    _, data = data_arg.split("=")

    # check if email is valid format
    if not re.match("[^@]+@[^@]+\.[^@]+", receiver):
        error_html = f"""<!DOCTYPE html>
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
                            <img src="../static/xmark.png" alt="X mark image" width="300px" height="300px">
                            <h1>Error: Invalid Email Address Inputted</h1>
                            <form id="returnForm">
                                <input type="button" id="return-btn" value="Return" onclick="window.location.href = '../cgi-bin/serial_com_html_res.cgi'">
                            </form>
                        </div>
                    </body>
                    </html>"""

        print(f"Content-type: text/html\n\n{error_html}\n")
        return

    sender = "eric.noharaleclair@gmail.com"
    app_password = "ugik cvqz lqji nssx"
    subject = "Current Attendence Data"
    message = f"Current attendance data: {data}. View attachment to see plotted samples."
    # text = f"Subject: {subject}\n\n{message}"

    # Create a multipart message
    msg = MIMEMultipart()
    msg['From'] = sender
    msg['To'] = receiver
    msg['Subject'] = subject

    # Add message body
    msg.attach(MIMEText(message, 'plain'))

    # Add image attachment
    with open('static/attendance_plot.png', 'rb') as img_file:
        img = MIMEImage(img_file.read())
        img.add_header('Content-Disposition', 'attachment', filename='attendance_plot.png')
        msg.attach(img)

    server = smtplib.SMTP("smtp.gmail.com", 587)
    server.starttls()
    server.login(sender, app_password)
    server.sendmail(sender, receiver, msg.as_string())
    server.quit()

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
                        <h1>Data value {data} successfully sent to {receiver}!</h1>
                        <form id="returnForm">
                            <input type="button" id="return-btn" value="Return" onclick="window.location.href = '../cgi-bin/serial_com_html_res.cgi'">
                        </form>
                    </div>
                </body>
                </html>"""

    print(f"Content-type: text/html\n\n{html}\n")


if __name__ == "__main__":
    handle_email_sending()