import os
import sys
import time
import argparse
import requests
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

# Function to split a string by a delimiter
def split(s, delimiter):
    return s.split(delimiter)

# Function to send a message to Telegram
def send_to_telegram(bot_id, chat_id, message):
    url = f"https://api.telegram.org/bot{bot_id}/sendMessage"
    data = {"chat_id": chat_id, "text": message}
    response = requests.post(url, data=data)
    if response.status_code != 200:
        print(f"Failed to send message: {response.text}", file=sys.stderr)

# Function to read configuration from a file
def read_config(config_path):
    config = {}
    with open(config_path, 'r') as f:
        for line in f:
            key, value = line.strip().split('=', 1)
            config[key] = value
    return config

# Function to create a default configuration file
def create_default_config(config_path):
    with open(config_path, 'w') as f:
        f.write("filename=\n")
        f.write("keyword=\n")
        f.write("n=0\n")
        f.write("bot_id=\n")
        f.write("chat_id=\n")
        f.write("debug=false\n")

class LogFileHandler(FileSystemEventHandler):
    def __init__(self, filenames, keywords, n, bot_id, chat_id, debug):
        self.filenames = filenames
        self.keywords = keywords
        self.n = n
        self.bot_id = bot_id
        self.chat_id = chat_id
        self.debug = debug
        self.file_pointers = {filename: open(filename, 'r') for filename in filenames}
        for fp in self.file_pointers.values():
            fp.seek(0, os.SEEK_END)

    def on_modified(self, event):
        if event.src_path in self.file_pointers:
            if self.debug:
                print(f"File modified: {event.src_path}", file=sys.stderr)
            self.process_file(event.src_path)

    def process_file(self, filename):
        fp = self.file_pointers[filename]
        lines = fp.readlines()
        for line in lines:
            if any(keyword in line for keyword in self.keywords):
                words = line.split()
                message_to_send = ' '.join(words[:self.n])
                send_to_telegram(self.bot_id, self.chat_id, message_to_send)
                if self.debug:
                    print(f"Sent message to Telegram: {message_to_send}", file=sys.stderr)

def main():
    home_env = os.getenv("HOME")
    if not home_env:
        print("Error: HOME environment variable is not set.", file=sys.stderr)
        sys.exit(1)

    config_path = os.path.join(home_env, ".config", "tg_log.ini")

    parser = argparse.ArgumentParser(description="Monitor log files and send alerts to Telegram.")
    parser.add_argument("--filename", type=str, help="Path to the log file(s), separated by commas")
    parser.add_argument("--keyword", type=str, help="Keyword(s) to watch for in the log file, separated by commas")
    parser.add_argument("--n", type=int, help="Number of words to include in the message")
    parser.add_argument("--bot-id", type=str, help="Telegram Bot ID")
    parser.add_argument("--chat-id", type=str, help="Telegram Chat ID")
    parser.add_argument("--debug", action="store_true", help="Enable debug mode")
    args = parser.parse_args()

    if len(sys.argv) == 1:
        if not os.path.exists(config_path):
            create_default_config(config_path)
            print(f"Configuration file created at {config_path}. Please fill in the required parameters.")
            sys.exit(1)
        config = read_config(config_path)
        filenames = split(config.get("filename", ""), ',')
        keywords = split(config.get("keyword", ""), ',')
        n = int(config.get("n", 0))
        bot_id = config.get("bot_id", "")
        chat_id = config.get("chat_id", "")
        debug = config.get("debug", "false").lower() == "true"
    else:
        filenames = split(args.filename, ',') if args.filename else []
        keywords = split(args.keyword, ',') if args.keyword else []
        n = args.n if args.n else 0
        bot_id = args.bot_id if args.bot_id else ""
        chat_id = args.chat_id if args.chat_id else ""
        debug = args.debug

    if not filenames or not keywords or n == 0 or not bot_id or not chat_id:
        print("Missing arguments!", file=sys.stderr)
        parser.print_usage()
        sys.exit(1)

    if debug:
        print(f"Parsed arguments:\n  filenames: {filenames}\n  keywords: {keywords}\n  n: {n}\n  bot_id: {bot_id}\n  chat_id: {chat_id}\n  debug: {debug}", file=sys.stderr)

    event_handler = LogFileHandler(filenames, keywords, n, bot_id, chat_id, debug)
    observer = Observer()
    for filename in filenames:
        observer.schedule(event_handler, path=filename, recursive=False)
    observer.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()

if __name__ == "__main__":
    main()
