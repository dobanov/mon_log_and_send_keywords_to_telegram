#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <curl/curl.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <filesystem>

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// CURL write callback function
size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

// Function to send a message to Telegram
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

// Function to print the usage information
void printUsage(const std::string& programName) {
    std::cerr << "Usage: " << programName << " --filename <filename> --keyword <keyword1> <keyword2> ... <keywordN> --n <n> --bot-id <bot_id> --chat-id <chat_id> [--debug]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --filename   Path to the log file" << std::endl;
    std::cerr << "  --keyword    Keyword(s) to watch for in the log file" << std::endl;
    std::cerr << "  --n          Number of words to include in the message" << std::endl;
    std::cerr << "  --bot-id     Telegram Bot ID" << std::endl;
    std::cerr << "  --chat-id    Telegram Chat ID" << std::endl;
    std::cerr << "  --debug      Enable debug mode (optional)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "If no command-line arguments are provided, the program will read configuration from ~/.config/tg_log.ini" << std::endl;
}

// Function to read configuration from a file
bool readConfig(const std::string& configPath, std::string& filename, std::vector<std::string>& keywords, int& n, std::string& botId, std::string& chatId, bool& debug) {
    std::ifstream config(configPath);
    if (!config.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(config, line)) {
        if (line.find("filename=") != std::string::npos) {
            filename = line.substr(line.find("=") + 1);
        } else if (line.find("keyword=") != std::string::npos) {
            keywords = split(line.substr(line.find("=") + 1), ',');
        } else if (line.find("n=") != std::string::npos) {
            n = std::stoi(line.substr(line.find("=") + 1));
        } else if (line.find("bot_id=") != std::string::npos) {
            botId = line.substr(line.find("=") + 1);
        } else if (line.find("chat_id=") != std::string::npos) {
            chatId = line.substr(line.find("=") + 1);
        } else if (line.find("debug=") != std::string::npos) {
            debug = (line.substr(line.find("=") + 1) == "true");
        }
    }

    config.close();
    return true;
}

// Function to create a default configuration file
void createDefaultConfig(const std::string& configPath) {
    std::ofstream config(configPath);
    config << "filename=\n";
    config << "keyword=\n";
    config << "n=0\n";
    config << "bot_id=\n";
    config << "chat_id=\n";
    config << "debug=false\n";
    config.close();
}

int main(int argc, char* argv[]) {
    std::string configPath = std::string(getenv("HOME")) + "/.config/tg_log.ini";
    std::string filename;
    std::vector<std::string> keywords;
    int n = 0;
    std::string botId;
    std::string chatId;
    bool debug = false;

    // Check if no command-line arguments are provided
    bool noArguments = (argc == 1);

    if (noArguments) {
        if (!std::filesystem::exists(configPath)) {
            createDefaultConfig(configPath);
            std::cerr << "Configuration file created at " << configPath << ". Please fill in the required parameters." << std::endl;
            std::cerr << "Configuration format:" << std::endl;
            std::cerr << "filename=<path_to_log_file>" << std::endl;
            std::cerr << "keyword=<keyword1,keyword2,...>" << std::endl;
            std::cerr << "n=<number_of_words>" << std::endl;
            std::cerr << "bot_id=<telegram_bot_id>" << std::endl;
            std::cerr << "chat_id=<telegram_chat_id>" << std::endl;
            std::cerr << "debug=<true|false>" << std::endl;
            return 1;
        }

        if (!readConfig(configPath, filename, keywords, n, botId, chatId, debug)) {
            std::cerr << "Failed to read configuration file." << std::endl;
            return 1;
        }
    } else {
        // Parsing command-line arguments
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
    }

    // Checking for missing arguments
    if (filename.empty() || keywords.empty() || n == 0 || botId.empty() || chatId.empty()) {
        std::cerr << "Missing arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Checking if the file exists and is a regular file
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Error: " << filename << " does not exist." << std::endl;
        return 1;
    }

    if (!std::filesystem::is_regular_file(filename)) {
        std::cerr << "Error: " << filename << " is not a regular file." << std::endl;
        return 1;
    }

    // Setting up inotify
    int inotifyFd = inotify_init();
    if (inotifyFd == -1) {
        std::cerr << "Failed to initialize inotify." << std::endl;
        return 1;
    }

    int watchFd = inotify_add_watch(inotifyFd, filename.c_str(), IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
    if (watchFd == -1) {
        std::cerr << "Failed to add inotify watch for " << filename << std::endl;
        close(inotifyFd);
        return 1;
    }

    std::ifstream file;
    std::string line;
    std::streampos lastPos;

    // Infinite loop to monitor the file
    while (true) {
        if (!file.is_open() || !file.good()) {
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

        char buffer[1024];
        ssize_t length = read(inotifyFd, buffer, sizeof(buffer));
        if (length == -1) {
            std::cerr << "Error reading from inotify file descriptor." << std::endl;
            continue;
        }

        for (char* ptr = buffer; ptr < buffer + length; ) {
            struct inotify_event* event = (struct inotify_event*) ptr;
            ptr += sizeof(struct inotify_event) + event->len;

            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
                if (debug) {
                    std::cerr << "File moved or deleted: " << filename << std::endl;
                }
                inotify_rm_watch(inotifyFd, watchFd);
                watchFd = inotify_add_watch(inotifyFd, filename.c_str(), IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
                if (watchFd == -1) {
                    std::cerr << "Failed to add inotify watch for " << filename << std::endl;
                    close(inotifyFd);
                    return 1;
                }
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
                    std::cerr << "File re-opened: " << filename << std::endl;
                }
            } else if (event->mask & IN_MODIFY) {
                if (debug) {
                    std::cerr << "File modified: " << filename << std::endl;
                }

                if (file.tellg() < lastPos) {
                    if (debug) {
                        std::cerr << "File truncated: " << filename << std::endl;
                    }
                    file.seekg(0, std::ios::end);
                    lastPos = file.tellg();
                }

                // Checking each line for keywords
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
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    close(inotifyFd);
    return 0;
}
