#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <math.h>
#include <signal.h>
#include <time.h>

// Конфигурация
#define BASE_ADDR 0x43c30000
#define PAGE_SIZE 4096
#define CONFIG_FILE "/etc/fm_transmitter.conf"
#define REFRESH_RATE 25    // Обновлений в секунду
#define FRAME_DELAY (1000000 / REFRESH_RATE)  // мкс на кадр
#define PEAK_HOLD_TIME 500  // Удержание пика в миллисекундах

// Адреса регистров
#define REG_VERSION   0x00
#define REG_CTRL      0x04
#define REG_FREQ      0x08
#define REG_MPXLVL    0x0C
#define REG_LEFT      0x10
#define REG_RIGHT     0x14
#define REG_STATUS    0x18
#define REG_BALANCE   0x1C

// Бит mute в контрольном регистре
#define CTRL_MUTE_BIT (1 << 5)

// Биты преэмфаза в контрольном регистре (3-4 биты)
#define PREEMPHASIS_MASK 0x18  // биты 3-4 (00011000)
#define PREEMPHASIS_BYPASS 0x00    // 00 (байпас)
#define PREEMPHASIS_50US   0x08    // 01 (50 µs)
#define PREEMPHASIS_75US   0x10    // 10 (75 µs)

// Константы
#define DDS_STEP 0.0286086784756944  // Шаг частоты в Гц
#define MPX_MAX 0xFFFFFF  // Максимальное значение MPX (24 бита)
#define AUDIO_MAX 32767   // Максимальное значение аудио (16 бит)

// Уровни в дБ относительно полной шкалы
#define DBFS_FULL_SCALE 0.0      // 0 dBFS = 32767
#define DBFS_MINUS_12 (-12.0)    // -12 dBFS
#define DBFS_MINUS_9 (-9.0)      // -9 dBFS (75 кГц девиации)
#define DBFS_TO_LIN(db) (pow(10.0, (db) / 20.0) * AUDIO_MAX)

// Пороговые значения для цветов аудио
#define AUDIO_GREEN_MAX DBFS_TO_LIN(DBFS_MINUS_12)   // -12 dBFS = 8202
#define AUDIO_YELLOW_MAX DBFS_TO_LIN(DBFS_MINUS_9)   // -9 dBFS = 11601

// Пороговые значения для MPX (кГц) - ИЗМЕНЕНО
#define MPX_GREEN_MAX 60.0    // до 60 кГц - зеленый
#define MPX_YELLOW_MAX 75.0   // 60-75 кГц - желтый
                           // выше 75 кГц - красный (новый порог)

// Цвета ANSI
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define BOLD          "\033[1m"
#define COLOR_MAGENTA "\033[35m"
#define BG_RED        "\033[41m"
#define BG_GREEN      "\033[42m"
#define BG_YELLOW     "\033[43m"

// Структура для управления
typedef struct {
    uint32_t base_addr;
    volatile uint32_t *regs;
    void *map_base;
    int fd;
    int tx_en;
    int stereo_en;
    int rds_en;
    int mute_en;
    int preemphasis_mode;  // 0=bypass, 1=50us, 2=75us
    double freq_mhz;
    int auto_refresh;      // Автообновление уровней
    volatile int running;  // Флаг работы программы
    int screen_height;     // Высота экрана в строках
    int menu_height;       // Высота меню в строках
} fm_transmitter_t;

// Глобальные переменные для обработки сигналов
fm_transmitter_t *global_tx = NULL;

// Структура для удержания пиковых значений
typedef struct {
    double mpx_khz;
    int left;
    int right;
    long timestamp;  // Время последнего обновления в мс
} peak_holder_t;

peak_holder_t peak_values = {0, 0, 0, 0};

// Прототипы функций
void signal_handler(int sig);
double lin_to_dbfs(int value);
double mpx_to_khz(uint32_t mpx_raw);
void update_peak_values(uint32_t mpx_raw, int16_t left, int16_t right);
const char* get_audio_color(int value);
const char* get_mpx_color(double khz);
void print_audio_bar(int value, int max_value, int width);
void print_mpx_bar(double khz, int width);
double str_to_double(const char *str);
int fm_init(fm_transmitter_t *tx, uint32_t base_addr);
void fm_close(fm_transmitter_t *tx);
uint32_t fm_read(fm_transmitter_t *tx, uint32_t offset);
void fm_write(fm_transmitter_t *tx, uint32_t offset, uint32_t value);
void fm_update_state(fm_transmitter_t *tx);
void fm_set_frequency(fm_transmitter_t *tx, double freq_mhz);
void fm_update_control(fm_transmitter_t *tx);
void fm_toggle_preemphasis(fm_transmitter_t *tx);
const char* get_preemphasis_str(int mode);
void save_settings(const fm_transmitter_t *tx);
int load_settings(fm_transmitter_t *tx);
void auto_apply_settings(fm_transmitter_t *tx);
void clear_screen();
int kbhit();
int getch_nonblock();
void print_menu(fm_transmitter_t *tx, int clear_before);
void frequency_dialog(fm_transmitter_t *tx);
void print_help();

// Обработчик сигналов для корректного завершения
void signal_handler(int sig) {
    if (global_tx) {
        global_tx->running = 0;
    }
}

// Преобразование линейного значения в дБFS
double lin_to_dbfs(int value) {
    if (value == 0) return -100.0;  // -100 дБ как минимальное
    double ratio = fabs((double)value) / AUDIO_MAX;
    return 20.0 * log10(ratio);
}

// Преобразование MPX в кГц (исправленная версия)
double mpx_to_khz(uint32_t mpx_raw) {
    // MPX регистр 24-битный знаковый
    int32_t mpx_signed = (int32_t)mpx_raw;
    
    // Проверяем знак (23-й бит)
    if (mpx_signed & 0x00800000) {  // Бит 23 установлен = отрицательное
        // Расширяем знак до 32 бит
        mpx_signed |= 0xFF000000;
    } else {
        // Очищаем старшие биты для положительных
        mpx_signed &= 0x00FFFFFF;
    }
    
    // Преобразуем в кГц
    return fabs(mpx_signed * DDS_STEP) / 1000.0;
}

// Обновление пиковых значений
void update_peak_values(uint32_t mpx_raw, int16_t left, int16_t right) {
    long current_time = clock() * 1000 / CLOCKS_PER_SEC;
    double mpx_khz = mpx_to_khz(mpx_raw);
    
    // Всегда обновляем текущие значения MPX (они должны меняться быстро)
    // Но пиковые значения держим 500 мс
    
    // Если пики устарели, сбрасываем
    if (current_time - peak_values.timestamp > PEAK_HOLD_TIME) {
        peak_values.mpx_khz = mpx_khz;
        peak_values.left = abs(left);
        peak_values.right = abs(right);
        peak_values.timestamp = current_time;
    } else {
        // Обновляем если текущие значения больше
        if (mpx_khz > peak_values.mpx_khz) {
            peak_values.mpx_khz = mpx_khz;
            peak_values.timestamp = current_time;
        }
        if (abs(left) > peak_values.left) {
            peak_values.left = abs(left);
            peak_values.timestamp = current_time;
        }
        if (abs(right) > peak_values.right) {
            peak_values.right = abs(right);
            peak_values.timestamp = current_time;
        }
    }
}

// Получение цвета для аудио уровня по ГОСТ
const char* get_audio_color(int value) {
    int abs_value = abs(value);
    
    if (abs_value <= AUDIO_GREEN_MAX) {
        return COLOR_GREEN;      // до -12 dBFS
    } else if (abs_value <= AUDIO_YELLOW_MAX) {
        return COLOR_YELLOW;     // -12 dBFS до -9 dBFS
    } else {
        return COLOR_RED;        // выше -9 dBFS
    }
}

// Получение цвета для MPX уровня по ГОСТ
const char* get_mpx_color(double khz) {
    if (khz <= MPX_GREEN_MAX) {
        return COLOR_GREEN;      // до 60 кГц
    } else if (khz <= MPX_YELLOW_MAX) {
        return COLOR_YELLOW;     // 60-75 кГц (ИЗМЕНЕНО)
    } else {
        return COLOR_RED;        // выше 75 кГц (ИЗМЕНЕНО)
    }
}

// Отображение ползунка с цветовой индикацией по ГОСТ (ИСПРАВЛЕННОЕ МАСШТАБИРОВАНИЕ)
void print_audio_bar(int value, int max_value, int width) {
    int abs_value = abs(value);
    
    // Определяем границы цветовых зон
    // Шкала 16 символов: 1 символ красная зона, 15 символов для зеленой+желтой
    int red_width = 1;  // Красная зона - 1 символ
    int available_width = width - red_width;  // 15 символов для зеленой+желтой
    
    // Рассчитываем позицию на шкале
    double normalized;
    if (abs_value <= AUDIO_YELLOW_MAX) {
        // Значение в зеленой или желтой зоне (0-11601)
        normalized = (abs_value * available_width) / AUDIO_YELLOW_MAX;
    } else {
        // Значение в красной зоне (>11601)
        normalized = available_width + ((abs_value - AUDIO_YELLOW_MAX) * red_width) / (max_value - AUDIO_YELLOW_MAX);
        if (normalized > width) normalized = width;
    }
    
    int bars = (int)(normalized + 0.5);  // Округляем
    if (bars > width) bars = width;
    if (bars < 0) bars = 0;
    
    // Граница между зеленой и желтой зонами (в пикселях)
    int green_limit = (AUDIO_GREEN_MAX * available_width) / AUDIO_YELLOW_MAX;
    
    for (int i = 0; i < width; i++) {
        if (i < bars) {
            if (i < green_limit) {
                printf("%s█", COLOR_GREEN);
            } else if (i < available_width) {
                printf("%s█", COLOR_YELLOW);
            } else {
                printf("%s█", COLOR_RED);  // Красная зона (последний символ)
            }
        } else {
            // Фон ползунка
            if (i < green_limit) {
                printf("%s░", COLOR_GREEN);
            } else if (i < available_width) {
                printf("%s░", COLOR_YELLOW);
            } else {
                printf("%s░", COLOR_RED);  // Красная зона (последний символ)
            }
        }
    }
    printf("%s", COLOR_RESET);
}

// Отображение шкалы MPX с цветовой индикацией - ИСПРАВЛЕНА
void print_mpx_bar(double khz, int width) {
    // Нормализуем к максимальному значению (100 кГц = 100%) - ИЗМЕНЕНО
    double normalized = (khz / 100.0) * width;
    int bars = (int)normalized;
    if (bars > width) bars = width;
    if (bars < 0) bars = 0;
    
    // Определяем цветовые зоны - ИЗМЕНЕНО
    int green_limit = (MPX_GREEN_MAX / 100.0) * width;
    int yellow_limit = (MPX_YELLOW_MAX / 100.0) * width;
    
    for (int i = 0; i < width; i++) {
        if (i < bars) {
            if (i < green_limit) {
                printf("%s█", COLOR_GREEN);
            } else if (i < yellow_limit) {
                printf("%s█", COLOR_YELLOW);
            } else {
                printf("%s█", COLOR_RED);
            }
        } else {
            // Фон ползунка
            if (i < green_limit) {
                printf("%s░", COLOR_GREEN);
            } else if (i < yellow_limit) {
                printf("%s░", COLOR_YELLOW);
            } else {
                printf("%s░", COLOR_RED);
            }
        }
    }
    printf("%s", COLOR_RESET);
}

// Функции преобразования
double str_to_double(const char *str) {
    char buffer[256];
    int j = 0;
    
    for (int i = 0; str[i] != '\0' && i < sizeof(buffer) - 1; i++) {
        if (str[i] == ',') buffer[j++] = '.';
        else buffer[j++] = str[i];
    }
    buffer[j] = '\0';
    
    return atof(buffer);
}

// Инициализация
int fm_init(fm_transmitter_t *tx, uint32_t base_addr) {
    tx->base_addr = base_addr;
    tx->fd = open("/dev/mem", O_RDWR | O_SYNC);
    
    if (tx->fd == -1) {
        printf("%sОшибка: Не могу открыть /dev/mem%s\n", COLOR_RED, COLOR_RESET);
        printf("%sЗапустите программу с sudo!%s\n", COLOR_YELLOW, COLOR_RESET);
        return -1;
    }
    
    off_t page_base = base_addr & ~(PAGE_SIZE - 1);
    off_t offset = base_addr - page_base;
    
    tx->map_base = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_SHARED, tx->fd, page_base);
    
    if (tx->map_base == MAP_FAILED) {
        perror("Ошибка маппирования");
        close(tx->fd);
        return -1;
    }
    
    tx->regs = (volatile uint32_t*)((char*)tx->map_base + offset);
    tx->auto_refresh = 0;  // По умолчанию автообновление выключено
    tx->running = 1;
    tx->screen_height = 0;
    tx->menu_height = 27;  // Высота меню по умолчанию
    return 0;
}

// Закрытие
void fm_close(fm_transmitter_t *tx) {
    if (tx->map_base != MAP_FAILED) munmap(tx->map_base, PAGE_SIZE);
    if (tx->fd != -1) close(tx->fd);
}

// Чтение регистра
uint32_t fm_read(fm_transmitter_t *tx, uint32_t offset) {
    if (!tx || !tx->regs) return 0;
    return tx->regs[offset / 4];
}

// Запись регистра
void fm_write(fm_transmitter_t *tx, uint32_t offset, uint32_t value) {
    if (!tx || !tx->regs) return;
    tx->regs[offset / 4] = value;
    usleep(1000);
}

// Обновление состояния из регистров
void fm_update_state(fm_transmitter_t *tx) {
    uint32_t ctrl = fm_read(tx, REG_CTRL);
    tx->tx_en = (ctrl & 0x1) ? 1 : 0;
    tx->stereo_en = (ctrl & 0x2) ? 1 : 0;
    tx->rds_en = (ctrl & 0x4) ? 1 : 0;
    tx->mute_en = (ctrl & CTRL_MUTE_BIT) ? 1 : 0;
    
    // Чтение преэмфаза (биты 3-4)
    uint32_t preemp = (ctrl & PREEMPHASIS_MASK);
    if (preemp == PREEMPHASIS_BYPASS) tx->preemphasis_mode = 0;
    else if (preemp == PREEMPHASIS_50US) tx->preemphasis_mode = 1;
    else if (preemp == PREEMPHASIS_75US) tx->preemphasis_mode = 2;
    else tx->preemphasis_mode = 0;
    
    uint32_t ftw = fm_read(tx, REG_FREQ);
    tx->freq_mhz = (double)ftw * DDS_STEP / 1000000.0;
}

// Установка частоты
void fm_set_frequency(fm_transmitter_t *tx, double freq_mhz) {
    if (freq_mhz < 0) return;
    
    double freq_hz = freq_mhz * 1000000.0;
    uint32_t ftw = (uint32_t)(freq_hz / DDS_STEP + 0.5);
    fm_write(tx, REG_FREQ, ftw);
    tx->freq_mhz = freq_mhz;
}

// Обновление управления
void fm_update_control(fm_transmitter_t *tx) {
    uint32_t ctrl = 0;
    ctrl |= tx->tx_en ? 0x1 : 0x0;
    ctrl |= tx->stereo_en ? 0x2 : 0x0;
    ctrl |= tx->rds_en ? 0x4 : 0x0;
    ctrl |= tx->mute_en ? CTRL_MUTE_BIT : 0x0;
    
    switch (tx->preemphasis_mode) {
        case 1: ctrl |= PREEMPHASIS_50US; break;
        case 2: ctrl |= PREEMPHASIS_75US; break;
        default: ctrl |= PREEMPHASIS_BYPASS; break;
    }
    
    fm_write(tx, REG_CTRL, ctrl);
}

// Переключение преэмфаза
void fm_toggle_preemphasis(fm_transmitter_t *tx) {
    tx->preemphasis_mode = (tx->preemphasis_mode + 1) % 3;
    fm_update_control(tx);
}

// Получение строки преэмфаза
const char* get_preemphasis_str(int mode) {
    switch (mode) {
        case 0: return " Bypass         ";
        case 1: return " 50 µs (Europe) ";
        case 2: return " 75 µs (America)";
        default: return " Unknown";
    }
}

// Сохранение настроек
void save_settings(const fm_transmitter_t *tx) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    
    fprintf(f, "TX=%d\n", tx->tx_en);
    fprintf(f, "STEREO=%d\n", tx->stereo_en);
    fprintf(f, "RDS=%d\n", tx->rds_en);
    fprintf(f, "MUTE=%d\n", tx->mute_en);
    fprintf(f, "PREEMPHASIS=%d\n", tx->preemphasis_mode);
    fprintf(f, "FREQUENCY=%.6f\n", tx->freq_mhz);
    
    fclose(f);
}

// Загрузка настроек
int load_settings(fm_transmitter_t *tx) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return 0;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\n")] = 0;
        char *value = strchr(line, '=');
        if (!value) continue;
        *value++ = 0;
        
        if (strcmp(line, "TX") == 0) tx->tx_en = atoi(value);
        else if (strcmp(line, "STEREO") == 0) tx->stereo_en = atoi(value);
        else if (strcmp(line, "RDS") == 0) tx->rds_en = atoi(value);
        else if (strcmp(line, "MUTE") == 0) tx->mute_en = atoi(value);
        else if (strcmp(line, "PREEMPHASIS") == 0) {
            tx->preemphasis_mode = atoi(value);
            if (tx->preemphasis_mode < 0 || tx->preemphasis_mode > 2) tx->preemphasis_mode = 0;
        }
        else if (strcmp(line, "FREQUENCY") == 0) tx->freq_mhz = str_to_double(value);
    }
    
    fclose(f);
    return 1;
}

// Автоматическое применение настроек
void auto_apply_settings(fm_transmitter_t *tx) {
    if (tx->freq_mhz > 0 && tx->freq_mhz < 200) {
        fm_set_frequency(tx, tx->freq_mhz);
    }
    fm_update_control(tx);
}

// Очистка экрана
void clear_screen() {
    printf("\033[2J\033[H");
}

// Функция проверки доступности ввода
int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

// Чтение символа без блокировки
int getch_nonblock() {
    if (!kbhit()) return -1;
    
    struct termios oldt, newt;
    int ch;
    
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// Функция для определения размеров терминала
void get_terminal_size(int *width, int *height) {
    // По умолчанию
    *width = 80;
    *height = 24;
    
    // Пробуем получить размер через ioctl
#ifdef TIOCGSIZE
    struct ttysize ts;
    if (ioctl(STDIN_FILENO, TIOCGSIZE, &ts) == 0) {
        *width = ts.ts_cols;
        *height = ts.ts_lines;
    }
#elif defined(TIOCGWINSZ)
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
    }
#endif
}

// Отображение меню
void print_menu(fm_transmitter_t *tx, int clear_before) {
    static struct timespec last_update = {0, 0};
    struct timespec now;
    long elapsed_ms;
    
    // Если автообновление включено, проверяем время
    if (tx->auto_refresh && !clear_before) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (last_update.tv_sec != 0) {
            elapsed_ms = (now.tv_sec - last_update.tv_sec) * 1000 +
                        (now.tv_nsec - last_update.tv_nsec) / 1000000;
            if (elapsed_ms < (1000 / REFRESH_RATE)) {
                return;  // Слишком рано для обновления
            }
        }
        last_update = now;
    }
    
    if (clear_before || !tx->auto_refresh) {
        clear_screen();
    } else {
        // В режиме автообновления перемещаем курсор в начало
        printf("\033[H");
    }
    
    // Заголовок
    printf("%s┌────────────────────────────────────────────────────────────────┐%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s│   %sAntminer S9 FM TRANSMITTER by Denis Koryakin @denisfk1985%s    %s│%s\n", COLOR_BLUE, BOLD, COLOR_RESET, COLOR_BLUE, COLOR_RESET);
    printf("%s└────────────────────────────────────────────────────────────────┘%s\n\n", COLOR_BLUE, COLOR_RESET);
    
    // Частота
    printf("%s  ═══ %s%.1f MHz%s%s ═══%s\n\n", 
           COLOR_CYAN, BOLD, tx->freq_mhz, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    
    // Статусы
    printf("%s[%s1]%s TX:     %s%s%s\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_CYAN,
           tx->tx_en ? COLOR_GREEN : COLOR_RED,
           tx->tx_en ? " ● ON Air!    " : " ○ No carrier",
           COLOR_RESET);
    
    printf("%s[%s2]%s STEREO: %s%s%s\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_CYAN,
           tx->stereo_en ? COLOR_GREEN : COLOR_RED,
           tx->stereo_en ? " ● ON " : " ○ OFF",
           COLOR_RESET);
    
    printf("%s[%s3]%s RDS:    %s%s%s\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_CYAN,
           tx->rds_en ? COLOR_GREEN : COLOR_RED,
           tx->rds_en ? " ● ON " : " ○ OFF",
           COLOR_RESET);
    
    printf("%s[%s4]%s MUTE:   %s%s%s\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_CYAN,
           tx->mute_en ? COLOR_MAGENTA : COLOR_YELLOW,
           tx->mute_en ? " ● MUTED " : " ○ OFF  ",
           COLOR_RESET);
    
    printf("%s[%s5]%s PRE:    %s%s%s\n\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_CYAN,
           tx->preemphasis_mode == 0 ? COLOR_YELLOW : COLOR_GREEN,
           get_preemphasis_str(tx->preemphasis_mode),
           COLOR_RESET);
    
    // Заголовок для уровней
    printf("%sAUDIO LEVELS ", COLOR_BLUE);
    if (tx->auto_refresh) {
        printf("%s[AUTO REFRESH]  ", COLOR_GREEN);
    } else {
        printf("%s[MANUAL]        ", COLOR_YELLOW);
    }
    printf("%s\n%s", COLOR_BLUE, COLOR_RESET);
    
    uint32_t left_raw = fm_read(tx, REG_LEFT) & 0xFFFF;
    uint32_t right_raw = fm_read(tx, REG_RIGHT) & 0xFFFF;
    uint32_t mpxlvl_raw = fm_read(tx, REG_MPXLVL) & 0xFFFFFF;
    
    int16_t left = (int16_t)left_raw;
    int16_t right = (int16_t)right_raw;
    
    // Преобразуем MPX в килогерцы (исправленная функция)
    double mpx_khz = mpx_to_khz(mpxlvl_raw);
    
    // Обновляем пиковые значения (текущие MPX всегда обновляются)
    update_peak_values(mpxlvl_raw, left, right);
    
    // Левый канал
    printf("    L: ");
    print_audio_bar(left, AUDIO_MAX, 16);
    printf(" %s%6.1f dBFS%s", get_audio_color(left), lin_to_dbfs(left), COLOR_RESET);
    
    // Пиковый индикатор
    if (abs(left) == peak_values.left && peak_values.left > AUDIO_GREEN_MAX) {
        printf(" %s▲", get_audio_color(peak_values.left));
    }
    printf("\n");
    
    // Правый канал
    printf("    R: ");
    print_audio_bar(right, AUDIO_MAX, 16);
    printf(" %s%6.1f dBFS%s", get_audio_color(right), lin_to_dbfs(right), COLOR_RESET);
    
    // Пиковый индикатор
    if (abs(right) == peak_values.right && peak_values.right > AUDIO_GREEN_MAX) {
        printf(" %s▲", get_audio_color(peak_values.right));
    }
    printf("\n");
    
    // Шкала аудио с подписями
    printf("%s", COLOR_GREEN);
    for (int i = 0; i < 7; i++) printf(" ");
    printf("-12dB%s", COLOR_RESET);
    
    printf("%s", COLOR_YELLOW);
    for (int i = 0; i < 5; i++) printf(" ");
    printf("-9dB%s", COLOR_RESET);
    
    printf("%s", COLOR_RED);
    printf(" O%s\n", COLOR_RESET);
    
    printf("\n");
    
    // MPX в кГц с ползунком
    printf("  MPX: ");
    print_mpx_bar(mpx_khz, 16);
    printf(" %s%6.1f kHz%s", 
           get_mpx_color(mpx_khz),
           mpx_khz,
           COLOR_RESET);
    
    // Индикатор пика для MPX
    if (fabs(mpx_khz - peak_values.mpx_khz) < 0.1 && mpx_khz > MPX_GREEN_MAX) {
        printf(" %s▲", get_mpx_color(mpx_khz));
    }
    printf("\n");
    
    // Шкала MPX - ИЗМЕНЕНО
    printf("%s", COLOR_GREEN);
    printf("       60%s", COLOR_RESET);
    
    printf("%s", COLOR_YELLOW);
    printf("        75%s", COLOR_RESET);
    
    printf("%s", COLOR_RED);
    printf(" 100%s\n", COLOR_RESET);
    
    printf("\n");
    
    // Управление
    printf("%s[1-5]%s Toggles  %s[F]%s Freq  %s[A]%s Auto(%s) %s[L]%s Load %s[S]%s Save  %s[Q]%s Quit %s\n",
           COLOR_YELLOW, COLOR_RESET,
           COLOR_YELLOW, COLOR_RESET,
           COLOR_YELLOW, COLOR_RESET,
           tx->auto_refresh ? COLOR_GREEN "ON" : COLOR_RED "OFF",
           COLOR_RESET,
           COLOR_YELLOW, COLOR_RESET,
           COLOR_YELLOW, COLOR_RESET,
           COLOR_YELLOW, COLOR_RESET,
           tx->mute_en ? COLOR_MAGENTA : COLOR_RESET);
    
    if (!tx->auto_refresh) {
        printf("\n%s>%s ", COLOR_GREEN, COLOR_RESET);
    }
    fflush(stdout);
}

// Диалог установки частоты
void frequency_dialog(fm_transmitter_t *tx) {
    char input[256];
    struct termios oldt, newt;
    
    // Сохраняем текущие настройки терминала
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag |= (ICANON | ECHO);  // Включаем канонический режим и эхо
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    clear_screen();
    
    printf("%s┌─────────────────────────────────┐%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s│         SET FREQUENCY          │%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s└─────────────────────────────────┘%s\n\n", COLOR_BLUE, COLOR_RESET);
    
    printf("Current: %s%.6f МГц%s\n\n", COLOR_CYAN, tx->freq_mhz, COLOR_RESET);
    printf("New frequency (MHz): ");
    fflush(stdout);
    
    if (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) > 0) {
            double new_freq = str_to_double(input);
            if (new_freq > 0 && new_freq < 200) {
                fm_set_frequency(tx, new_freq);
                printf("\n%s✓ Set to %.6f MHz%s\n", COLOR_GREEN, new_freq, COLOR_RESET);
            } else {
                printf("\n%s✗ Invalid frequency!%s\n", COLOR_RED, COLOR_RESET);
            }
        }
    }
    
    printf("\nPress any key...");
    fflush(stdout);
    
    // Ждем нажатия любой клавиши
    system("stty raw -echo");
    getchar();
    
    // Восстанавливаем настройки терминала
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    
    // Очищаем экран и показываем меню
    print_menu(tx, 1);
}

// Справка
void print_help() {
    printf("%sFM Transmitter Control v2.7 (Professional)%s\n\n", BOLD, COLOR_RESET);
    printf("%sUsage:%s\n", BOLD, COLOR_RESET);
    printf("  fm_ctrl [--auto | -a]    Apply saved settings and exit\n");
    printf("  fm_ctrl [--help | -h]    Show this help\n");
    printf("  fm_ctrl                  Interactive mode\n\n");
    printf("%sInteractive controls:%s\n", BOLD, COLOR_RESET);
    printf("  1-5    Toggle TX/Stereo/RDS/Mute/Preemphasis\n");
    printf("  F      Set frequency\n");
    printf("  A      Toggle auto-refresh (%d Hz)\n", REFRESH_RATE);
    printf("  S      Save settings to %s\n", CONFIG_FILE);
    printf("  L      Load settings\n");
    printf("  Q      Quit\n");
    printf("\n%sColor scales (ГОСТ):%s\n", BOLD, COLOR_RESET);
    printf("  Audio: %sGreen (-∞ to -12 dBFS)%s, %sYellow (-12 to -9 dBFS)%s, %sRed (> -9 dBFS)%s\n",
           COLOR_GREEN, COLOR_RESET, COLOR_YELLOW, COLOR_RESET, COLOR_RED, COLOR_RESET);
    printf("  MPX:   %sGreen (0-60 kHz)%s, %sYellow (60-75 kHz)%s, %sRed (>75 kHz)%s\n",
           COLOR_GREEN, COLOR_RESET, COLOR_YELLOW, COLOR_RESET, COLOR_RED, COLOR_RESET);
}

// Главный цикл
int main(int argc, char *argv[]) {
    fm_transmitter_t tx = {0};
    char ch;
    int auto_mode = 0;
    
    // Настройка обработки сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    global_tx = &tx;
    
    // Обработка аргументов
    if (argc > 1) {
        if (strcmp(argv[1], "--auto") == 0 || strcmp(argv[1], "-a") == 0) {
            auto_mode = 1;
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            return 0;
        } else {
            printf("%sUnknown argument: %s%s\n", COLOR_RED, argv[1], COLOR_RESET);
            print_help();
            return 1;
        }
    }
    
    printf("%sInitializing...%s\n", COLOR_BLUE, COLOR_RESET);
    
    if (fm_init(&tx, BASE_ADDR) != 0) {
        return 1;
    }
    
    // Настройка терминала для неблокирующего ввода
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    
    fm_update_state(&tx);
    
    // Автоматический режим
    if (auto_mode) {
        if (load_settings(&tx)) {
            auto_apply_settings(&tx);
            printf("%sSettings applied%s\n", COLOR_GREEN, COLOR_RESET);
        }
        fm_close(&tx);
        return 0;
    }
    
    // Инициализация пиковых значений
    peak_values.timestamp = clock() * 1000 / CLOCKS_PER_SEC;
    
    // Первоначальное отображение
    print_menu(&tx, 1);
    
    // Интерактивный режим
    while(tx.running) {
        if (tx.auto_refresh) {
            // В режиме автообновления проверяем ввод без блокировки
            ch = getch_nonblock();
            if (ch != -1) {
                // Обработка ввода
                switch(ch) {
                    case '1': tx.tx_en = !tx.tx_en; fm_update_control(&tx); break;
                    case '2': tx.stereo_en = !tx.stereo_en; fm_update_control(&tx); break;
                    case '3': tx.rds_en = !tx.rds_en; fm_update_control(&tx); break;
                    case '4': tx.mute_en = !tx.mute_en; fm_update_control(&tx); break;
                    case '5': fm_toggle_preemphasis(&tx); break;
                    case 'a': case 'A': 
                        tx.auto_refresh = !tx.auto_refresh; 
                        print_menu(&tx, 1);
                        break;
                    case 's': case 'S': 
                        save_settings(&tx);
                        printf("\n%sSettings saved%s\n", COLOR_GREEN, COLOR_RESET);
                        usleep(500000);
                        print_menu(&tx, 1);
                        break;
                    case 'l': case 'L':
                        if (load_settings(&tx)) {
                            auto_apply_settings(&tx);
                            printf("\n%sSettings loaded%s\n", COLOR_GREEN, COLOR_RESET);
                        } else {
                            printf("\n%sNo saved settings%s\n", COLOR_YELLOW, COLOR_RESET);
                        }
                        usleep(500000);
                        print_menu(&tx, 1);
                        break;
                    case 'q': case 'Q': tx.running = 0; break;
                    case 'f': case 'F':
                        frequency_dialog(&tx);
                        break;
                }
                // Обновляем состояние после действий
                fm_update_state(&tx);
            }
            
            // Автообновление экрана
            if (tx.auto_refresh) {
                print_menu(&tx, 0);
                usleep(FRAME_DELAY);
            }
        } else {
            // В режиме ручного обновления ждем ввода
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            ch = getchar();
            
            switch(ch) {
                case '1': tx.tx_en = !tx.tx_en; fm_update_control(&tx); break;
                case '2': tx.stereo_en = !tx.stereo_en; fm_update_control(&tx); break;
                case '3': tx.rds_en = !tx.rds_en; fm_update_control(&tx); break;
                case '4': tx.mute_en = !tx.mute_en; fm_update_control(&tx); break;
                case '5': fm_toggle_preemphasis(&tx); break;
                case 'a': case 'A': tx.auto_refresh = !tx.auto_refresh; break;
                case 'f': case 'F': 
                    frequency_dialog(&tx);
                    break;
                case 's': case 'S': 
                    save_settings(&tx);
                    printf("%sSettings saved%s\n", COLOR_GREEN, COLOR_RESET);
                    usleep(300000);
                    break;
                case 'l': case 'L':
                    if (load_settings(&tx)) {
                        auto_apply_settings(&tx);
                        printf("%sSettings loaded%s\n", COLOR_GREEN, COLOR_RESET);
                    } else {
                        printf("%sNo saved settings%s\n", COLOR_YELLOW, COLOR_RESET);
                    }
                    usleep(300000);
                    break;
                case 'q': case 'Q': tx.running = 0; break;
            }
            
            fm_update_state(&tx);
            print_menu(&tx, 1);
        }
    }
    
    // Восстановление терминала
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    clear_screen();
    fm_close(&tx);
    
    return 0;
}