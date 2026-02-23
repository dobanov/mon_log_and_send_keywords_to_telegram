#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <curl/curl.h>
#include <sys/inotify.h>
#include <unistd.h>

// ─── Утилиты ────────────────────────────────────────────────────────────────

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream ss(s);
    while (std::getline(ss, token, delimiter))
        if (!token.empty())          // пропускаем пустые токены
            tokens.push_back(token);
    return tokens;
}

// ─── CURL ───────────────────────────────────────────────────────────────────

// callback: пишет данные в std::string
size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(contents, size * nmemb);
    return size * nmemb;
}

void sendTextToTelegram(const std::string& botId,
                        const std::vector<std::string>& chatIds,
                        const std::string& message,
                        bool debugMode)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL\n";
        return;
    }

    const std::string url = "https://api.telegram.org/bot" + botId + "/sendMessage";

    // Экранируем один раз и сразу освобождаем через RAII-обёртку
    char* raw = curl_easy_escape(curl, message.c_str(), 0);
    std::string escapedMessage = raw ? raw : "";
    curl_free(raw);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);

    for (const auto& chatId : chatIds) {
        std::string data = "chat_id=" + chatId + "&text=" + escapedMessage;
        std::string response;

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Failed to send to Telegram: " << curl_easy_strerror(res) << '\n';
        } else if (debugMode) {
            std::cout << "Response for " << chatId << ": " << response << '\n';
        }
    }

    curl_easy_cleanup(curl);
}

// ─── Конфиг ─────────────────────────────────────────────────────────────────

void printUsage(const std::string& prog) {
    std::cerr
        << "Usage: " << prog
        << " --filename <f1,f2,...> --keyword <k1,k2,...>"
           " --n <n> --bot-id <id> --chat-id <c1,c2,...> [--debug]\n"
        << "Or configure via ~/.config/tg_log.ini\n";
}

bool readConfig(const std::string& path,
                std::vector<std::string>& filenames,
                std::vector<std::string>& keywords,
                int& n,
                std::string& botId,
                std::vector<std::string>& chatIds,
                bool& debug)
{
    std::ifstream cfg(path);
    if (!cfg.is_open()) return false;

    std::string line;
    while (std::getline(cfg, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "filename") filenames = split(val, ',');
        else if (key == "keyword")  keywords  = split(val, ',');
        else if (key == "n")        n         = std::stoi(val);
        else if (key == "bot_id")   botId     = val;
        else if (key == "chat_id")  chatIds   = split(val, ',');
        else if (key == "debug")    debug     = (val == "true");
    }
    return true;
}

void createDefaultConfig(const std::string& path) {
    std::ofstream cfg(path);
    cfg << "filename=\nkeyword=\nn=0\nbot_id=\nchat_id=\ndebug=false\n";
    std::cerr << "Config created at " << path << ". Fill in the parameters.\n";
}

// ─── Вспомогательная: (пере)открыть файл и перемотать в конец ───────────────

bool reopenAtEnd(std::ifstream& f, const std::string& name) {
    f.close();
    f.clear();
    f.open(name);
    if (!f.is_open()) { std::cerr << "Cannot open " << name << '\n'; return false; }
    f.seekg(0, std::ios::end);
    return true;
}

// ─── Вспомогательная: добавить watch с retry ────────────────────────────────

int addWatch(int fd, const std::string& name, bool debug) {
    constexpr int kRetries = 10;
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        int wd = inotify_add_watch(fd, name.c_str(),
                                   IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
        if (wd != -1) return wd;
        if (debug)
            std::cerr << "Waiting for " << name << " (attempt " << attempt+1 << ")\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cerr << "Failed to watch " << name << '\n';
    return -1;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* homeEnv = getenv("HOME");
    if (!homeEnv) { std::cerr << "HOME not set\n"; return 1; }

    const std::string configPath = std::string(homeEnv) + "/.config/tg_log.ini";

    std::vector<std::string> filenames, keywords, chatIds;
    std::string botId;
    int n = 0;
    bool debug = false;

    if (argc == 1) {
        if (!std::filesystem::exists(configPath)) {
            createDefaultConfig(configPath);
            return 1;
        }
        if (!readConfig(configPath, filenames, keywords, n, botId, chatIds, debug)) {
            std::cerr << "Failed to read config\n"; return 1;
        }
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            // Безопасная проверка наличия следующего аргумента
            auto next = [&]() -> const char* {
                if (i + 1 >= argc) {
                    std::cerr << "Missing value for " << arg << '\n';
                    std::exit(1);
                }
                return argv[++i];
            };
            if      (arg == "--filename") filenames = split(next(), ',');
            else if (arg == "--keyword")  keywords  = split(next(), ',');
            else if (arg == "--n")        n         = std::stoi(next());
            else if (arg == "--bot-id")   botId     = next();
            else if (arg == "--chat-id")  chatIds   = split(next(), ',');
            else if (arg == "--debug")    debug     = true;
            else { std::cerr << "Unknown argument: " << arg << '\n'; }
        }
    }

    if (filenames.empty() || keywords.empty() || n <= 0 || botId.empty() || chatIds.empty()) {
        std::cerr << "Missing required arguments!\n";
        printUsage(argv[0]);
        return 1;
    }

    if (debug) {
        std::cerr << "Files: ";
        for (auto& f : filenames) std::cerr << f << ' ';
        std::cerr << "\nKeywords: ";
        for (auto& k : keywords) std::cerr << k << ' ';
        std::cerr << "\nn=" << n << " bot=" << botId << " debug=true\n";
    }

    // ── inotify ──
    int ifd = inotify_init();
    if (ifd == -1) { std::cerr << "inotify_init failed\n"; return 1; }

    const size_t count = filenames.size();
    std::vector<int> wds(count, -1);
    std::vector<std::ifstream> files(count);

    for (size_t i = 0; i < count; ++i) {
        wds[i] = addWatch(ifd, filenames[i], debug);
        if (wds[i] == -1) { close(ifd); return 1; }
        if (!reopenAtEnd(files[i], filenames[i])) { close(ifd); return 1; }
    }

    // ── Основной цикл (блокирующий read — sleep не нужен) ──
    constexpr size_t kBufSize = 4096;
    char buffer[kBufSize] __attribute__((aligned(__alignof__(struct inotify_event))));

    while (true) {
        ssize_t length = read(ifd, buffer, kBufSize);
        if (length == -1) {
            std::cerr << "inotify read error\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        for (char* ptr = buffer; ptr < buffer + length; ) {
            auto* ev = reinterpret_cast<struct inotify_event*>(ptr);
            ptr += sizeof(struct inotify_event) + ev->len;

            // Найдём индекс файла по wd
            size_t idx = count;
            for (size_t i = 0; i < count; ++i)
                if (wds[i] == ev->wd) { idx = i; break; }
            if (idx == count) continue;

            if (ev->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
                if (debug) std::cerr << "File moved/deleted: " << filenames[idx] << '\n';
                inotify_rm_watch(ifd, wds[idx]);
                wds[idx] = addWatch(ifd, filenames[idx], debug);
                if (wds[idx] == -1) { close(ifd); return 1; }
                if (!reopenAtEnd(files[idx], filenames[idx])) { close(ifd); return 1; }

            } else if (ev->mask & IN_MODIFY) {
                if (debug) std::cerr << "File modified: " << filenames[idx] << '\n';

                // Обнаружение усечения: текущая позиция > размер файла
                auto curPos = files[idx].tellg();
                auto fileSize = static_cast<std::streampos>(
                    std::filesystem::file_size(filenames[idx]));
                if (curPos > fileSize) {
                    if (debug) std::cerr << "File truncated: " << filenames[idx] << '\n';
                    files[idx].seekg(0);
                }

                std::string line;
                while (std::getline(files[idx], line)) {
                    for (const auto& kw : keywords) {
                        if (line.find(kw) == std::string::npos) continue;

                        std::vector<std::string> words = split(line, ' ');
                        std::ostringstream msg;
                        int limit = std::min(static_cast<int>(words.size()), n);
                        for (int j = 0; j < limit; ++j)
                            msg << words[j] << ' ';

                        sendTextToTelegram(botId, chatIds, msg.str(), debug);
                        if (debug)
                            std::cerr << "Sent: " << msg.str() << '\n';
                        break; // не проверяем остальные keywords для этой строки
                    }
                }
                files[idx].clear(); // сброс флага EOF
            }
        }
    }

    close(ifd);
    return 0;
}
