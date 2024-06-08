This program monitors the log file(s) and sends new lines from the log file(s) containing keyword(s) to the Telegram chat/bot.


g++ -o tg_send tg_send.cpp -lcurl
/
g++ -std=c++17 -o tg_send tg_send.cpp -lcurl



Options:


  --filename   Path to the log file(s)

  --keyword    Keyword(s) to watch for in the log file(s)

  --n          Number of words to include in the message

  --bot-id     Telegram Bot ID

  --chat-id    Telegram Chat ID
  
