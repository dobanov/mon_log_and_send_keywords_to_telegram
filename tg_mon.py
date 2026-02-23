import argparse
import os
import sys
import time
import requests
import logging
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
log = logging.getLogger(__name__)


# ─── Telegram ────────────────────────────────────────────────────────────────

def send_text_to_telegram(bot_id: str, chat_ids: list[str],
                           message: str, debug_mode: bool) -> None:
    url = f"https://api.telegram.org/bot{bot_id}/sendMessage"
    for chat_id in chat_ids:
        payload = {'chat_id': chat_id, 'text': message}
        try:
            response = requests.post(url, data=payload, timeout=10)
            response.raise_for_status()
            if debug_mode:
                log.info("Sent to %s: %s", chat_id, message)
                log.debug("Response: %s", response.text)
        except requests.RequestException as e:
            log.error("Failed to send to Telegram (chat %s): %s", chat_id, e)


# ─── Watchdog handler ────────────────────────────────────────────────────────

class LogFileEventHandler(FileSystemEventHandler):
    """Следит за директориями, фильтрует события по именам файлов."""

    def __init__(self, filenames: list[str], keywords: list[str],
                 bot_id: str, chat_ids: list[str], n: int, debug_mode: bool):
        super().__init__()
        # Нормализуем пути для надёжного сравнения
        self.watched = {str(Path(f).resolve()) for f in filenames}
        self.keywords = keywords
        self.bot_id = bot_id
        self.chat_ids = chat_ids
        self.n = n
        self.debug_mode = debug_mode
        # file_path -> открытый файловый объект
        self._files: dict[str, object] = {}

    # ── открыть/переоткрыть файл, встав в конец ──
    def _open_at_end(self, path: str):
        f = self._files.get(path)
        if f:
            try:
                f.close()
            except OSError:
                pass
        try:
            f = open(path, 'r', errors='replace')
            f.seek(0, os.SEEK_END)
            self._files[path] = f
        except OSError as e:
            log.error("Cannot open %s: %s", path, e)
            self._files.pop(path, None)

    def _get_file(self, path: str):
        """Возвращает файловый объект, открывая при необходимости."""
        if path not in self._files:
            self._open_at_end(path)
        return self._files.get(path)

    def _check_truncation(self, path: str, f) -> bool:
        """Возвращает True и перематывает в начало, если файл усечён."""
        try:
            current_pos = f.tell()
            file_size = os.path.getsize(path)
            if current_pos > file_size:
                if self.debug_mode:
                    log.info("File truncated, rewinding: %s", path)
                f.seek(0)
                return True
        except OSError:
            pass
        return False

    def _process_file(self, path: str) -> None:
        f = self._get_file(path)
        if f is None:
            return

        self._check_truncation(path, f)

        try:
            for line in f:
                for keyword in self.keywords:
                    if keyword in line:
                        words = line.split()
                        message = ' '.join(words[:self.n])
                        send_text_to_telegram(
                            self.bot_id, self.chat_ids, message, self.debug_mode)
                        if self.debug_mode:
                            log.info("Keyword '%s' in: %s", keyword, line.rstrip())
                        break  # не проверяем другие keywords для этой строки
        except OSError as e:
            log.error("Error reading %s: %s", path, e)
            self._open_at_end(path)  # попробуем переоткрыть

    # ── события watchdog ──

    def on_modified(self, event):
        if event.is_directory:
            return
        path = str(Path(event.src_path).resolve())
        if path not in self.watched:
            return
        if self.debug_mode:
            log.info("Modified: %s", path)
        self._process_file(path)

    def on_moved(self, event):
        """Обработка ротации логов (rename/move)."""
        path = str(Path(event.src_path).resolve())
        if path not in self.watched:
            return
        if self.debug_mode:
            log.info("File moved/rotated: %s", path)
        # Переоткрываем (новый файл появится при следующем on_modified)
        self._open_at_end(path)

    def on_deleted(self, event):
        path = str(Path(event.src_path).resolve())
        if path not in self.watched:
            return
        if self.debug_mode:
            log.info("File deleted: %s", path)
        f = self._files.pop(path, None)
        if f:
            try:
                f.close()
            except OSError:
                pass

    def on_created(self, event):
        """Файл пересоздан после удаления/ротации."""
        path = str(Path(event.src_path).resolve())
        if path not in self.watched:
            return
        if self.debug_mode:
            log.info("File created: %s", path)
        self._open_at_end(path)

    def close_all(self):
        """Явное закрытие всех дескрипторов при завершении."""
        for f in self._files.values():
            try:
                f.close()
            except OSError:
                pass
        self._files.clear()


# ─── Конфиг ──────────────────────────────────────────────────────────────────

def read_config(config_path: str) -> tuple:
    required = {'filename', 'keyword', 'n', 'bot_id', 'chat_id', 'debug'}
    config: dict[str, str] = {}

    try:
        with open(config_path, 'r') as f:
            for lineno, line in enumerate(f, 1):
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' not in line:
                    log.warning("Config line %d skipped (no '='): %s", lineno, line)
                    continue
                key, _, value = line.partition('=')
                config[key.strip()] = value.strip()
    except OSError as e:
        log.error("Cannot read config: %s", e)
        sys.exit(1)

    missing = required - config.keys()
    if missing:
        log.error("Config missing keys: %s", ', '.join(missing))
        sys.exit(1)

    try:
        return (
            [s for s in config['filename'].split(',') if s],
            [s for s in config['keyword'].split(',') if s],
            int(config['n']),
            config['bot_id'],
            [s for s in config['chat_id'].split(',') if s],
            config['debug'].lower() == 'true',
        )
    except ValueError as e:
        log.error("Invalid config value: %s", e)
        sys.exit(1)


def create_default_config(config_path: str) -> None:
    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    with open(config_path, 'w') as f:
        f.write(
            "filename=\nkeyword=\nn=0\n"
            "bot_id=\nchat_id=\ndebug=false\n"
        )
    log.info("Default config created at %s — please fill it in.", config_path)


# ─── CLI ─────────────────────────────────────────────────────────────────────

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description='Monitor log files and send alerts to Telegram.')
    parser.add_argument('--filename', help='Log file(s), comma-separated')
    parser.add_argument('--keyword',  help='Keyword(s), comma-separated')
    parser.add_argument('--n',        type=int, help='Words to include in message')
    parser.add_argument('--bot-id',   dest='bot_id', help='Telegram Bot ID')
    parser.add_argument('--chat-id',  dest='chat_id', help='Chat ID(s), comma-separated')
    parser.add_argument('--debug',    action='store_true', help='Enable debug mode')
    return parser.parse_args()


# ─── main ─────────────────────────────────────────────────────────────────────

def main() -> None:
    config_path = os.path.join(os.path.expanduser("~"), '.config', 'tg_log.ini')
    args = parse_arguments()

    # Используем конфиг только если ни один аргумент не передан
    use_config = not any([args.filename, args.keyword, args.n,
                           args.bot_id, args.chat_id, args.debug])

    if use_config:
        if not os.path.exists(config_path):
            create_default_config(config_path)
            return
        filenames, keywords, n, bot_id, chat_ids, debug = read_config(config_path)
    else:
        # Проверяем что все обязательные аргументы переданы
        missing = [name for name, val in [
            ('--filename', args.filename), ('--keyword', args.keyword),
            ('--n',        args.n),        ('--bot-id',  args.bot_id),
            ('--chat-id',  args.chat_id),
        ] if not val]
        if missing:
            log.error("Missing required arguments: %s", ', '.join(missing))
            sys.exit(1)

        filenames = [s for s in args.filename.split(',') if s]
        keywords  = [s for s in args.keyword.split(',')  if s]
        n         = args.n
        bot_id    = args.bot_id
        chat_ids  = [s for s in args.chat_id.split(',')  if s]
        debug     = args.debug

    if not filenames or not keywords or n <= 0 or not bot_id or not chat_ids:
        log.error("Invalid arguments (check n > 0 and non-empty lists).")
        sys.exit(1)

    if debug:
        log.setLevel(logging.DEBUG)
        log.debug("filenames=%s keywords=%s n=%d bot_id=%s chat_ids=%s",
                  filenames, keywords, n, bot_id, chat_ids)

    # Проверяем существование файлов
    for filename in filenames:
        if not os.path.exists(filename):
            log.warning("File does not exist yet, will watch directory: %s", filename)

    # watchdog наблюдает за директориями, а handler фильтрует по именам файлов
    event_handler = LogFileEventHandler(filenames, keywords, bot_id, chat_ids, n, debug)
    observer = Observer()

    watched_dirs: set[str] = set()
    for filename in filenames:
        directory = str(Path(filename).resolve().parent)
        if directory not in watched_dirs:
            observer.schedule(event_handler, path=directory, recursive=False)
            watched_dirs.add(directory)
            if debug:
                log.debug("Watching directory: %s", directory)

    observer.start()
    log.info("Monitoring started. Press Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Stopping...")
    finally:
        observer.stop()
        observer.join()
        event_handler.close_all()
        log.info("Done.")


if __name__ == "__main__":
    main()
