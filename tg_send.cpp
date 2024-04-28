#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <curl/curl.h>
#include <filesystem>

// Функция для разделения строки на подстроки по заданному разделителю
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Функция обратного вызова для записи данных CURL
size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

// Функция для отправки сообщения в Telegram
void sendToTelegram(const std::string& botId, const std::string& chatId, const std::string& message) {
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

// Функция для вывода информации о использовании программы
void printUsage(const std::string& programName) {
    std::cerr << "Usage: " << programName << " --filename <filename> --keyword <keyword1> <keyword2> ... <keywordN> --n <n> --bot-id <bot_id> --chat-id <chat_id> [--debug]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --filename   Path to the log file" << std::endl;
    std::cerr << "  --keyword    Keyword(s) to watch for in the log file" << std::endl;
    std::cerr << "  --n          Number of words to include in the message" << std::endl;
    std::cerr << "  --bot-id     Telegram Bot ID" << std::endl;
    std::cerr << "  --chat-id    Telegram Chat ID" << std::endl;
    std::cerr << "  --debug      Enable debug mode (optional)" << std::endl;
}

int main(int argc, char* argv[]) {
    // Проверка наличия аргументов командной строки
    if (argc < 2 || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 1;
    }

    std::string filename;
    std::vector<std::string> keywords;
    int n;
    std::string botId;
    std::string chatId;
    bool debug = false;

    // Парсинг аргументов командной строки
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--filename") {
            filename = argv[++i];
        } else if (arg == "--keyword") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                keywords.push_back(argv[++i]);
            }
        } else if (arg == "--n") {
            n = std::stoi(argv[++i]);
        } else if (arg == "--bot-id") {
            botId = argv[++i];
        } else if (arg == "--chat-id") {
            chatId = argv[++i];
        } else if (arg == "--debug") {
            debug = true;
        }
    }

    // Проверка наличия всех необходимых аргументов
    if (filename.empty() || keywords.empty() || n == 0 || botId.empty() || chatId.empty()) {
        std::cerr << "Missing arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Проверка существования и типа файла
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Error: " << filename << " does not exist." << std::endl;
        return 1;
    }

    if (!std::filesystem::is_regular_file(filename)) {
        std::cerr << "Error: " << filename << " is not a regular file." << std::endl;
        return 1;
    }

    std::ifstream file;
    std::string line;
    std::streampos lastPos;

    // Бесконечный цикл мониторинга файла
    while (true) {
        if (!file.is_open() || !file.good() || !std::filesystem::is_regular_file(filename)) {
            file.close();
            file.clear();
            file.open(filename);

            if (!file.is_open()) {
                std::cerr << "Unable to open file " << filename << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            file.seekg(0, std::ios::end);
            lastPos = file.tellg();
            if (debug) {
                std::cerr << "File opened: " << filename << std::endl;
            }
        }

        if (file.tellg() < lastPos) {
            if (debug) {
                std::cerr << "File truncated: " << filename << std::endl;
            }
            file.seekg(0, std::ios::end);
            lastPos = file.tellg();
        }

        // Проверка каждой строки файла на наличие ключевых слов
        while (std::getline(file, line)) {
            for (const auto& keyword : keywords) {
                if (line.find(keyword) != std::string::npos) {
                    std::vector<std::string> words = split(line, ' ');
                    std::ostringstream messageToSend;
                    for (int i = 0; i < std::min(static_cast<int>(words.size()), n); ++i) {
                        messageToSend << words[i] << " ";
                    }
                    sendToTelegram(botId, chatId, messageToSend.str());
                    if (debug) {
                        std::cerr << "Sent message to Telegram: " << messageToSend.str() << std::endl;
                    }
                    break;
                }
            }
        }
        file.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}
