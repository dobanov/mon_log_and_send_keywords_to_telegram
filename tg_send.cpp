#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <curl/curl.h>

std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

void sendToTelegram(std::string botId, std::string chatId, std::string message) {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string url = "https://api.telegram.org/bot" + botId + "/sendMessage";
        std::string data = "chat_id=" + chatId + "&text=" + message;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}

void printUsage(const std::string& programName) {
    std::cerr << "Usage: " << programName << " --filename <filename> --keyword <keyword1> <keyword2> ... <keywordN> --n <n> --bot-id <bot_id> --chat-id <chat_id>" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --filename   Path to the log file" << std::endl;
    std::cerr << "  --keyword    Keyword(s) to watch for in the log file" << std::endl;
    std::cerr << "  --n          Number of words to include in the message" << std::endl;
    std::cerr << "  --bot-id     Telegram Bot ID" << std::endl;
    std::cerr << "  --chat-id    Telegram Chat ID" << std::endl;
}

bool isTextFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    char c;
    while (file.get(c)) {
        if (c == '\0') {
            return false;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 1;
    }

    std::string filename;
    std::vector<std::string> keywords;
    int n;
    std::string botId;
    std::string chatId;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--filename") {
            filename = argv[++i];
        } else if (arg == "--keyword") {
            while (i + 1 < argc && argv[i + 1][0] != '-' ) {
                keywords.push_back(argv[++i]);
            }
        } else if (arg == "--n") {
            n = std::stoi(argv[++i]);
        } else if (arg == "--bot-id") {
            botId = argv[++i];
        } else if (arg == "--chat-id") {
            chatId = argv[++i];
        }
    }

    if (filename.empty() || keywords.empty() || n == 0 || botId.empty() || chatId.empty()) {
        std::cerr << "Missing arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    if (!isTextFile(filename)) {
        std::cerr << "Error: " << filename << " is not a text file." << std::endl;
        return 1;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file " << filename << std::endl;
        return 1;
    }

    file.seekg(0, std::ios::end); // Переместить указатель в конец файла

    std::string line;
    while (true) {
        while (std::getline(file, line)) {
            for (const auto& keyword : keywords) {
                if (line.find(keyword) != std::string::npos) {
                    std::vector<std::string> words = split(line, ' ');
                    int count = 0;
                    std::string messageToSend;
                    for (const auto& word : words) {
                        if (count == n) break;
                        messageToSend += word + " ";
                        count++;
                    }
                    sendToTelegram(botId, chatId, messageToSend);
                    for (int i = 0; i < n && std::getline(file, line); ++i) {
                        // Ничего не делать
                    }
                    break;
                }
            }
        }
        file.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Подождать 1 секунду перед проверкой новых данных
    }

    return 0;
}

