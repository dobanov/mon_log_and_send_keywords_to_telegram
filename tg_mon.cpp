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
    std::cerr << "Usage: " << programName << " --filename <filename1,filename2,...> --keyword <keyword1,keyword2,...> --n <n> --bot-id <bot_id> --chat-id <chat_id> [--debug]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --filename   Path to the log file(s), separated by commas" << std::endl;
    std::cerr << "  --keyword    Keyword(s) to watch for in the log file" << std::endl;
    std::cerr << "  --n          Number of words to include in the message" << std::endl;
    std::cerr << "  --bot-id     Telegram Bot ID" << std::endl;
    std::cerr << "  --chat-id    Telegram Chat ID" << std::endl;
    std::cerr << "  --debug      Enable debug mode (optional)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "If no command-line arguments are provided, the program will read configuration from ~/.config/tg_log.ini" << std::endl;
    std::cerr << "Ensure the configuration file exists with the following format:" << std::endl;
    std::cerr << "filename=<path_to_log_file1,path_to_log_file2,...>" << std::endl;
    std::cerr << "keyword=<keyword1,keyword2,...>" << std::endl;
    std::cerr << "n=<number_of_words>" << std::endl;
    std::cerr << "bot_id=<telegram_bot_id>" << std::endl;
    std::cerr << "chat_id=<telegram_chat_id>" << std::endl;
    std::cerr << "debug=<true|false>" << std::endl;
}

// Function to read configuration from a file
bool readConfig(const std::string& configPath, std::vector<std::string>& filenames, std::vector<std::string>& keywords, int& n, std::string& botId, std::string& chatId, bool& debug) {
    std::ifstream config(configPath);
    if (!config.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(config, line)) {
        if (line.find("filename=") != std::string::npos) {
            filenames = split(line.substr(line.find("=") + 1), ',');
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
    std::vector<std::string> filenames;
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
            printUsage(argv[0]);
            return 1;
        }

        if (!readConfig(configPath, filenames, keywords, n, botId, chatId, debug)) {
            std::cerr << "Failed to read configuration file." << std::endl;
            return 1;
        }
    } else {
        // Parsing command-line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--filename") {
                filenames = split(argv[++i], ',');
            } else if (arg == "--keyword") {
                keywords = split(argv[++i], ',');
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
    if (filenames.empty() || keywords.empty() || n == 0 || botId.empty() || chatId.empty()) {
        std::cerr << "Missing arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Checking if the files exist and are regular files
    for (const auto& filename : filenames) {
        if (!std::filesystem::exists(filename)) {
            std::cerr << "Error: " << filename << " does not exist." << std::endl;
            return 1;
        }

        if (!std::filesystem::is_regular_file(filename)) {
            std::cerr << "Error: " << filename << " is not a regular file." << std::endl;
            return 1;
        }
    }

    // Setting up inotify
    int inotifyFd = inotify_init();
    if (inotifyFd == -1) {
        std::cerr << "Failed to initialize inotify." << std::endl;
        return 1;
    }

    std::vector<int> watchFds;
    for (const auto& filename : filenames) {
        int watchFd = inotify_add_watch(inotifyFd, filename.c_str(), IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
        if (watchFd == -1) {
            std::cerr << "Failed to add inotify watch for " << filename << std::endl;
            close(inotifyFd);
            return 1;
        }
        watchFds.push_back(watchFd);
    }

    std::vector<std::ifstream> files(filenames.size());
    std::vector<std::streampos> lastPositions(filenames.size());

    for (size_t i = 0; i < filenames.size(); ++i) {
        files[i].open(filenames[i]);
        if (!files[i].is_open()) {
            std::cerr << "Unable to open file " << filenames[i] << std::endl;
            return 1;
        }
        files[i].seekg(0, std::ios::end);
        lastPositions[i] = files[i].tellg();
    }

    // Monitoring files in an infinite loop
    while (true) {
        char buffer[1024 * (sizeof(struct inotify_event) + 16)];
        ssize_t length = read(inotifyFd, buffer, sizeof(buffer));
        if (length == -1) {
            std::cerr << "Error reading from inotify file descriptor." << std::endl;
            continue;
        }

        for (char* ptr = buffer; ptr < buffer + length; ) {
            struct inotify_event* event = (struct inotify_event*) ptr;
            ptr += sizeof(struct inotify_event) + event->len;

            std::string filename;
            for (size_t i = 0; i < watchFds.size(); ++i) {
                if (watchFds[i] == event->wd) {
                    filename = filenames[i];
                    break;
                }
            }

            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
                if (debug) {
                    std::cerr << "File moved or deleted: " << filename << std::endl;
                }
                for (size_t i = 0; i < watchFds.size(); ++i) {
                    if (watchFds[i] == event->wd) {
                        inotify_rm_watch(inotifyFd, watchFds[i]);
                        watchFds[i] = inotify_add_watch(inotifyFd, filenames[i].c_str(), IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
                        if (watchFds[i] == -1) {
                            std::cerr << "Failed to add inotify watch for " << filenames[i] << std::endl;
                            close(inotifyFd);
                            return 1;
                        }
                        files[i].close();
                        files[i].clear();
                        files[i].open(filenames[i]);
                        if (!files[i].is_open()) {
                            std::cerr << "Unable to open file " << filenames[i] << std::endl;
                            return 1;
                        }
                        files[i].seekg(0, std::ios::end);
                        lastPositions[i] = files[i].tellg();
                    }
                }
            } else if (event->mask & IN_MODIFY) {
                if (debug) {
                    std::cerr << "File modified: " << filename << std::endl;
                }

                for (size_t i = 0; i < watchFds.size(); ++i) {
                    if (watchFds[i] == event->wd) {
                        if (files[i].tellg() < lastPositions[i]) {
                            if (debug) {
                                std::cerr << "File truncated: " << filenames[i] << std::endl;
                            }
                            files[i].seekg(0, std::ios::end);
                            lastPositions[i] = files[i].tellg();
                        }

                        // Checking each line for keywords
                        std::string line;
                        files[i].seekg(lastPositions[i]);
                        while (std::getline(files[i], line)) {
                            for (const auto& keyword : keywords) {
                                if (line.find(keyword) != std::string::npos) {
                                    std::vector<std::string> words = split(line, ' ');
                                    std::ostringstream messageToSend;
                                    for (int j = 0; j < std::min(static_cast<int>(words.size()), n); ++j) {
                                        messageToSend << words[j] << " ";
                                    }
                                    sendToTelegram(botId, chatId, messageToSend.str());
                                    if (debug) {
                                        std::cerr << "Sent message to Telegram: " << messageToSend.str() << std::endl;
                                    }
                                    break;
                                }
                            }
                        }
                        files[i].clear();
                        lastPositions[i] = files[i].tellg();
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    close(inotifyFd);
    return 0;
}
