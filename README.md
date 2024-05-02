This program sends new lines from the log file containing keywords to the Telegram chat/bot.

g++ -o tg_send tg_send.cpp -lcurl


Options:
  --filename   Path to the log file

  --keyword    Keyword(s) to watch for in the log file

  --n          Number of words to include in the message

  --bot-id     Telegram Bot ID

  --chat-id    Telegram Chat ID
  
