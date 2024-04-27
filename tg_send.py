#!/usr/bin/env python3

import argparse
import time
import requests

def send_to_telegram(bot_id, chat_id, message):
    url = f"https://api.telegram.org/bot{bot_id}/sendMessage"
    data = {"chat_id": chat_id, "text": message}
    requests.post(url, data=data)

def follow_file(file, keywords, num_lines, bot_id, chat_id):
    file.seek(0,2)
    while True:
        line = file.readline()
        if not line:
            time.sleep(0.1)
            continue
        for keyword in keywords:
            if keyword in line:
                message = ' '.join(line.split()[:num_lines])
                send_to_telegram(bot_id, chat_id, message)
                break

def main(log_file, keywords, num_lines, bot_id, chat_id):
    with open(log_file, "r") as file:
        follow_file(file, keywords, num_lines, bot_id, chat_id)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Follow log file and send messages to Telegram")
    parser.add_argument("log_file", help="Path to the log file")
    parser.add_argument("keywords", nargs="+", help="Keyword(s) to search for in log file")
    parser.add_argument("num_lines", type=int, help="Number of lines to include in the message")
    parser.add_argument("bot_id", help="Telegram Bot ID")
    parser.add_argument("chat_id", help="Telegram Chat ID")
    args = parser.parse_args()

    main(args.log_file, args.keywords, args.num_lines, args.bot_id, args.chat_id)
