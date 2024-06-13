import argparse
import os
import time
import requests
import logging
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(message)s', datefmt='%Y-%m-%d %H:%M:%S')

def send_text_to_telegram(bot_id, chat_ids, message, debug_mode):
    url = f"https://api.telegram.org/bot{bot_id}/sendMessage"
    for chat_id in chat_ids:
        payload = {
            'chat_id': chat_id,
            'text': message
        }
        response = requests.post(url, data=payload)
        if debug_mode:
            logging.info(f"Sent message to chat ID {chat_id}: {message}")
            logging.info(f"Response: {response.text}")

class LogFileEventHandler(FileSystemEventHandler):
    def __init__(self, keywords, bot_id, chat_ids, n, debug_mode):
        self.keywords = keywords
        self.bot_id = bot_id
        self.chat_ids = chat_ids
        self.n = n
        self.debug_mode = debug_mode
        self.files = {}
        self.positions = {}

    def on_modified(self, event):
        if event.is_directory:
            return
        if self.debug_mode:
            logging.info(f"File modified: {event.src_path}")
        self.check_file(event.src_path)

    def check_file(self, file_path):
        if file_path not in self.files:
            self.files[file_path] = open(file_path, 'r')
            self.files[file_path].seek(0, os.SEEK_END)  # Move to the end of the file
            self.positions[file_path] = self.files[file_path].tell()

        file = self.files[file_path]
        file.seek(self.positions[file_path])
        for line in file:
            for keyword in self.keywords:
                if keyword in line:
                    words = line.split()
                    message = ' '.join(words[:self.n])
                    send_text_to_telegram(self.bot_id, self.chat_ids, message, self.debug_mode)
                    if self.debug_mode:
                        logging.info(f"Detected keyword '{keyword}' in line: {line.strip()}")
                    break
        self.positions[file_path] = file.tell()

def read_config(config_path):
    config = {}
    with open(config_path, 'r') as f:
        for line in f:
            if '=' in line:
                key, value = line.strip().split('=', 1)
                config[key.strip()] = value.strip()

    filenames = config['filename'].split(',')
    keywords = config['keyword'].split(',')
    n = int(config['n'])
    bot_id = config['bot_id']
    chat_ids = config['chat_id'].split(',')
    debug = config['debug'].lower() == 'true'
    return filenames, keywords, n, bot_id, chat_ids, debug

def create_default_config(config_path):
    with open(config_path, 'w') as configfile:
        configfile.write('filename=\n')
        configfile.write('keyword=\n')
        configfile.write('n=0\n')
        configfile.write('bot_id=\n')
        configfile.write('chat_id=\n')
        configfile.write('debug=false\n')

def parse_arguments():
    parser = argparse.ArgumentParser(description='Monitor log files and send alerts to Telegram.')
    parser.add_argument('--filename', type=str, help='Path to the log file(s), separated by commas')
    parser.add_argument('--keyword', type=str, help='Keyword(s) to watch for in the log file, separated by commas')
    parser.add_argument('--n', type=int, help='Number of words to include in the message')
    parser.add_argument('--bot-id', type=str, help='Telegram Bot ID')
    parser.add_argument('--chat-id', type=str, help='Telegram Chat IDs, separated by commas')
    parser.add_argument('--debug', action='store_true', help='Enable debug mode (optional)')
    return parser.parse_args()

def main():
    home = os.path.expanduser("~")
    config_path = os.path.join(home, '.config', 'tg_log.ini')

    args = parse_arguments()

    if not any(vars(args).values()):
        if not os.path.exists(config_path):
            create_default_config(config_path)
            logging.error(f"Configuration file created at {config_path}. Please fill in the required parameters.")
            return

        filenames, keywords, n, bot_id, chat_ids, debug = read_config(config_path)
    else:
        filenames = args.filename.split(',')
        keywords = args.keyword.split(',')
        n = args.n
        bot_id = args.bot_id
        chat_ids = args.chat_id.split(',')
        debug = args.debug

    if not filenames or not keywords or not n or not bot_id or not chat_ids:
        logging.error("Missing arguments! Please provide all required arguments.")
        return

    event_handler = LogFileEventHandler(keywords, bot_id, chat_ids, n, debug)
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
