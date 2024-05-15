#!/usr/bin/env python3
import os, stat
import matplotlib.pyplot as plt
import base64

# python3 my_histogram.py /Users/kelvin
# run python3 my_histogram.py and then followed by the directory you want to traverse

def count_file_types(directory):
    file_types = {
        'regular': 0,
        'directory': 0,
        'link': 0,
        'fifo': 0,
        'socket': 0,
        'block': 0,
        'character': 0
    }

    for root, dirs, files in os.walk(directory):
        # Count directories (only those not followed, to avoid double counting)
        file_types['directory'] += len([d for d in dirs if not os.path.islink(os.path.join(root, d))])

        # Check each file in the directory
        for name in files + dirs:
            path = os.path.join(root, name)
            try:
                if os.path.islink(path):
                    file_types['link'] += 1
                    continue
                mode = os.stat(path).st_mode
                if stat.S_ISREG(mode):
                    file_types['regular'] += 1
                elif stat.S_ISDIR(mode):
                    # Directories are counted above
                    continue
                elif stat.S_ISFIFO(mode):
                    file_types['fifo'] += 1
                elif stat.S_ISSOCK(mode):
                    file_types['socket'] += 1
                elif stat.S_ISBLK(mode):
                    file_types['block'] += 1
                elif stat.S_ISCHR(mode):
                    file_types['character'] += 1
            except OSError:
                continue

    return file_types

def generate_histogram(file_types, output_file, directory):
    labels = list(file_types.keys())
    counts = list(file_types.values())
    plt.figure(figsize=(10, 6))
    plt.bar(labels, counts, color='red')
    plt.xlabel('File Types')
    plt.ylabel('Frequency')
    plt.title(f"Frequency of Different File Types Starting in {directory}")
    plt.savefig(output_file)
    plt.close()

# Function to encode the image as base64
def encode_image(image_path):
    with open(image_path, 'rb') as img_file:
        encoded_image = base64.b64encode(img_file.read()).decode('utf-8') 
    return encoded_image

if __name__ == "__main__":
#    Extract directory path from QUERY_STRING
    query_string = os.environ.get('QUERY_STRING', '')
    if not query_string.startswith('directory='):
        print("Content-type: text/plain\n\nError: Invalid query parameters.")

    else: 
        directory = query_string.split('=')[1]

        if not os.path.exists(directory):
            print(f"Content-type: text/plain\n\nError: Directory '{directory}' does not exist.")

        else:
            file_types = count_file_types(directory)
            output_file = "cgi-bin/histogram.png"
            generate_histogram(file_types, output_file, directory) # create histogram
            encoded_image = encode_image(output_file) # Encode the image as base64
            os.remove(output_file) # remove unneeded file

            # Generate the HTML page
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
            <img src="data:image/png;base64,{encoded_image}" alt="Histogram">
            </body>
            </html>
            """

            print(f"Content-type: text/html\n\n{html_content}") # send to client as text/html