/**
 * @file    AdvanceGame.c
 * @author  Lucky
 * @date    2026-5-15
 *
 *  Input system: raw-mode line editor with ←→ cursor movement,
 *  Backspace/Delete, UTF-8 & CJK support.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MKDIR(p) mkdir(p, 0755)

/* ───────────────────────────── Constants ───────────────────────────── */

#define DB_FILE "AdvanceGame.dat"
#define RETRO_DIR "retrospectives"
#define RETRO_TPL_FILE "retrospectives/template.md"

#define MAX_NAME 32
#define MAX_LONG 1024
#define MAX_PATH_LEN 512
#define MAX_SKILL 100
#define MAX_TASK 100
#define MAX_RETRO_FILES 365
#define FNAME_LEN 32
#define BAR_WIDTH 20
#define MAX_DISPLAY_TASK 5
#define EDIT_BUF_SIZE 256

/* ANSI colors */
#define C_RED 31
#define C_GREEN 32
#define C_YELLOW 33
#define C_PURPLE 35
#define C_CYAN 36

/* ───────────────────────────── Types ───────────────────────────────── */

typedef enum { EASY = 1, MEDIUM = 2, HARD = 3 } Difficulty;
typedef enum { MINOR = 1, NORMAL = 2, MAJOR = 3, FATAL = 4 } Importance;

typedef struct {
    char name[MAX_NAME];
    char grade[MAX_NAME];
    char major[MAX_NAME];
    int level;
    int exp;
    int exp_to_next;
    int skill_count;
    int task_count;
} Player;

typedef struct {
    char name[MAX_NAME];
    int exp;
} Skill;

typedef struct {
    char name[MAX_NAME];
    int skill_idx;
    time_t start_time;
    time_t deadline;
    int difficulty;
    int importance;
    int exp_reward;
    int done;
} Task;

/* ───────────────────────────── Globals ─────────────────────────────── */

static Player player;
static Skill skills[MAX_SKILL];
static Task tasks[MAX_TASK];

static const char* WEEKDAY_CN[] = {"周日", "周一", "周二", "周三",
                                   "周四", "周五", "周六"};

static const char* DEFAULT_TEMPLATE =
    "# {DATE} ({WEEKDAY}) 每日复盘\n"
    "\n"
    "---\n"
    "\n"
    "## 总结\n"
    "\n"
    "> 今天做了什么？完成了哪些任务？\n"
    "\n"
    "\n"
    "## 反思\n"
    "\n"
    "> 哪些做得好？哪些需要改进？\n"
    "\n"
    "\n"
    "## 规划\n"
    "\n"
    "> 明天的计划是什么？\n"
    "\n"
    "\n"
    "## 随笔\n"
    "\n"
    "> 任何想记录的想法、灵感、感悟...\n";

/* ───────────────────────────── Terminal helpers ────────────────────── */

static void term_clear(void) { printf("\033[2J\033[H"); }
static void term_color(int c) { printf("\033[%dm", c); }
static void term_bold(void) { printf("\033[1m"); }
static void term_reset(void) { printf("\033[0m"); }

/* ───────────────────────────── UTF-8 helpers ───────────────────────── */

static int utf8_len(unsigned char first) {
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) return 2;
    if ((first & 0xF0) == 0xE0) return 3;
    if ((first & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_width(const char* s, int bytes) {
    if (bytes <= 2) return 1;
    if (bytes == 3) {
        unsigned char c = (unsigned char)s[0];
        if (c >= 0xE4 && c <= 0xE9) return 2;                   /* CJK */
        if (c == 0xEF && (unsigned char)s[1] >= 0xBC) return 2; /* fullwidth */
        return 1;
    }
    return 2; /* 4-byte emoji etc. */
}

static int str_display_width(const char* buf, int start, int end) {
    int w = 0;
    for (int i = start; i < end;) {
        int cl = utf8_len((unsigned char)buf[i]);
        w += utf8_width(buf + i, cl);
        i += cl;
    }
    return w;
}

/* ───────────────────────────── Raw-mode line editor ────────────────── */

/**
 * 终端原始模式行编辑器。
 * 支持：←→ 移动光标、Backspace 删除前一个字符、Delete 删除当前字符、
 *       中英文混排、Ctrl-C 取消。
 * @return 0 正常提交, -1 取消/EOF
 */
static int read_line_raw(char* buf, int size) {
    struct termios oldt, raw;
    tcgetattr(STDIN_FILENO, &oldt);
    raw = oldt;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    int len = 0, pos = 0;
    buf[0] = '\0';

    while (1) {
        fflush(stdout);
        int c = getchar();
        if (c == EOF) goto fail;

        /* ── Enter ── */
        if (c == '\n' || c == '\r') break;

        /* ── Ctrl-C ── */
        if (c == 0x03) goto fail;

        /* ── Backspace (0x7F or 0x08) ── */
        if (c == 0x7F || c == 0x08) {
            if (pos <= 0) continue;

            /* find start of previous UTF-8 char */
            int p = pos - 1;
            while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80) p--;
            int dlen = pos - p;
            int dw = utf8_width(buf + p, dlen);

            /* move cursor back over deleted char */
            for (int i = 0; i < dw; i++) putchar('\b');

            int old_tw = str_display_width(buf, p, len);

            /* remove from buffer */
            memmove(buf + p, buf + pos, len - pos);
            len -= dlen;
            pos = p;
            buf[len] = '\0';

            /* redraw tail */
            int new_tw = 0;
            for (int i = pos; i < len;) {
                int cl = utf8_len((unsigned char)buf[i]);
                fwrite(buf + i, 1, cl, stdout);
                new_tw += utf8_width(buf + i, cl);
                i += cl;
            }
            /* clear leftover */
            int extra = old_tw - new_tw;
            for (int i = 0; i < extra; i++) putchar(' ');
            /* move cursor back to pos */
            for (int i = 0; i < new_tw + extra; i++) putchar('\b');
            continue;
        }

        /* ── ESC sequences ── */
        if (c == '\033') {
            int next = getchar();
            if (next == EOF) goto fail;
            if (next == '[') {
                int ch = getchar();
                if (ch == EOF) goto fail;
                switch (ch) {
                    case 'A':
                    case 'B':
                        /* up/down: ignore */
                        break;

                    case 'C': /* → right */
                        if (pos < len) {
                            int cl = utf8_len((unsigned char)buf[pos]);
                            fwrite(buf + pos, 1, cl, stdout);
                            pos += cl;
                        }
                        break;

                    case 'D': /* ← left */
                        if (pos > 0) {
                            int p = pos - 1;
                            while (p > 0 &&
                                   ((unsigned char)buf[p] & 0xC0) == 0x80)
                                p--;
                            int dw = utf8_width(buf + p, pos - p);
                            for (int i = 0; i < dw; i++) putchar('\b');
                            pos = p;
                        }
                        break;

                    case '3': { /* Delete (ESC [ 3 ~) */
                        int tilde = getchar();
                        if (tilde == EOF) goto fail;
                        if (pos >= len) break;

                        int dlen = utf8_len((unsigned char)buf[pos]);
                        int old_tw = str_display_width(buf, pos, len);

                        memmove(buf + pos, buf + pos + dlen, len - pos - dlen);
                        len -= dlen;
                        buf[len] = '\0';

                        int new_tw = 0;
                        for (int i = pos; i < len;) {
                            int cl = utf8_len((unsigned char)buf[i]);
                            fwrite(buf + i, 1, cl, stdout);
                            new_tw += utf8_width(buf + i, cl);
                            i += cl;
                        }
                        int extra = old_tw - new_tw;
                        for (int i = 0; i < extra; i++) putchar(' ');
                        for (int i = 0; i < new_tw + extra; i++) putchar('\b');
                        break;
                    }
                    default:
                        /* consume remaining CSI params */
                        if (ch >= 0x30 && ch <= 0x3F)
                            while ((ch = getchar()) != EOF && ch >= 0x20 &&
                                   ch < 0x40);
                        break;
                }
            } else if (next == 'O') {
                getchar(); /* SS3 sequence terminator */
            }
            continue;
        }

        /* ── Printable: ASCII or UTF-8 multi-byte start ── */
        if ((c >= 0x20 && c < 0x7F) || (c >= 0xC0 && c <= 0xF7)) {
            char chbuf[4];
            int clen;

            if (c < 0x80) {
                clen = 1;
                chbuf[0] = (char)c;
            } else if ((c & 0xE0) == 0xC0) {
                clen = 2;
                chbuf[0] = (char)c;
                for (int i = 1; i < 2; i++) {
                    int b = getchar();
                    if (b == EOF) goto fail;
                    chbuf[i] = (char)b;
                }
            } else if ((c & 0xF0) == 0xE0) {
                clen = 3;
                chbuf[0] = (char)c;
                for (int i = 1; i < 3; i++) {
                    int b = getchar();
                    if (b == EOF) goto fail;
                    chbuf[i] = (char)b;
                }
            } else {
                clen = 4;
                chbuf[0] = (char)c;
                for (int i = 1; i < 4; i++) {
                    int b = getchar();
                    if (b == EOF) goto fail;
                    chbuf[i] = (char)b;
                }
            }

            if (len + clen >= size) continue;

            /* insert at cursor */
            memmove(buf + pos + clen, buf + pos, len - pos);
            memcpy(buf + pos, chbuf, clen);
            len += clen;
            buf[len] = '\0';

            /* redraw from insert point to end */
            for (int i = pos; i < len; i++) putchar(buf[i]);
            pos += clen;

            /* move cursor back to correct position */
            int back = str_display_width(buf, pos, len);
            for (int i = 0; i < back; i++) putchar('\b');
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    putchar('\n');
    return 0;

fail:
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    putchar('\n');
    return -1;
}

/** read_int: 用 read_line_raw 读一行，再解析为整数 */
static int read_int(int* out) {
    char buf[32];
    if (read_line_raw(buf, sizeof(buf)) == -1 || buf[0] == '\0') return -1;
    char* end;
    long val = strtol(buf, &end, 10);
    if (end == buf) return -1;
    *out = (int)val;
    return 0;
}

/* ───────────────────────────── Display helpers ─────────────────────── */

static const char* diff_str(int d) {
    switch (d) {
        case EASY:
            return "Easy";
        case MEDIUM:
            return "Medium";
        case HARD:
            return "Hard";
        default:
            return "?";
    }
}

static const char* imp_str(int i) {
    switch (i) {
        case MINOR:
            return "Minor";
        case NORMAL:
            return "Normal";
        case MAJOR:
            return "Major";
        case FATAL:
            return "Fatal";
        default:
            return "?";
    }
}

static void print_bar(int cur, int max_val, int width) {
    int filled = (max_val > 0) ? (cur * width / max_val) : 0;
    if (filled > width) filled = width;

    putchar('[');
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            term_color(C_GREEN);
            putchar('=');
            term_reset();
        } else {
            putchar(' ');
        }
    }
    printf("] %d/%d", cur, max_val);
}

static void fmt_date(time_t t, char* buf, int size) {
    struct tm* tm_info = localtime(&t);
    if (tm_info)
        strftime(buf, size, "%Y-%m-%d", tm_info);
    else
        strncpy(buf, "----/--/--", size);
}

static void Welcome(void) {
    term_clear();
    term_color(C_YELLOW);
    printf("\n");
    printf("  ╔════════════════════════════════════════════╗\n");
    printf("  ║            Advance      Game               ║\n");
    printf("  ╚════════════════════════════════════════════╝\n");
    term_reset();
    printf("\n");
}

/* task display sort: undone first, then deadline, then importance, then
 * difficulty */
static int cmp_task_display(const void* pa, const void* pb) {
    int a = *(const int*)pa, b = *(const int*)pb;

    if (tasks[a].done != tasks[b].done) return tasks[a].done - tasks[b].done;

    if (tasks[a].deadline != tasks[b].deadline)
        return (tasks[a].deadline > tasks[b].deadline) ? 1 : -1;

    if (tasks[a].importance != tasks[b].importance)
        return tasks[b].importance - tasks[a].importance;

    return tasks[a].difficulty - tasks[b].difficulty;
}

static void DisplayInfo(void) {
    /* Player info */
    term_color(C_CYAN);
    printf("\n  ┌─── Information ───────────────────────────┐\n");
    printf("  │  Name : %-34s│\n", player.name);
    printf("  │  Grade: %-34s│\n", player.grade);
    printf("  │  Major: %-34s│\n", player.major);
    printf("  │  Level: %-34d│\n", player.level);
    printf("  │  Exp  : ");
    term_reset();
    print_bar(player.exp, player.exp_to_next, BAR_WIDTH);
    term_color(C_CYAN);
    printf("       │\n");
    printf("  └───────────────────────────────────────────┘\n");
    term_reset();

    /* Skills */
    if (player.skill_count > 0) {
        term_color(C_PURPLE);
        printf("\n  ┌─── Skills ─────────────────────────────────┐\n");
        for (int i = 0; i < player.skill_count; i++)
            printf("  │  [%2d] %-20s Exp: %-11d│\n", i, skills[i].name,
                   skills[i].exp);
        printf("  └────────────────────────────────────────────┘\n");
        term_reset();
    }

    /* Tasks */
    if (player.task_count > 0) {
        int indices[MAX_TASK];
        for (int i = 0; i < player.task_count; i++) indices[i] = i;
        qsort(indices, player.task_count, sizeof(int), cmp_task_display);

        int show = player.task_count;
        int hidden = 0;
        if (show > MAX_DISPLAY_TASK) {
            hidden = show - MAX_DISPLAY_TASK;
            show = MAX_DISPLAY_TASK;
        }

        term_color(C_YELLOW);
        printf("\n  ┌─── Tasks (Top %d)", show);
        if (hidden > 0) printf("  +%d more", hidden);
        printf(
            " ─────────────────────────────────────────────────────────────┐"
            "\n");
        printf(
            "  │  #   Status  Name                Start        Deadline     "
            "Diff   Imp    Exp  │\n");
        printf(
            "  │  ─── ──────  ──────────────────  ──────────── ──────────── "
            "────── ────── ──── │\n");

        for (int si = 0; si < show; si++) {
            int i = indices[si];
            char ds[16], dl[16];
            fmt_date(tasks[i].start_time, ds, sizeof(ds));
            fmt_date(tasks[i].deadline, dl, sizeof(dl));

            printf("  │  %-3d ", i);

            if (tasks[i].done) {
                term_color(C_GREEN);
                printf(" [✓]    ");
            } else {
                term_color(C_RED);
                printf(" [ ]    ");
            }
            term_color(C_YELLOW);

            printf("%-18s  %-12s %-12s %-6s %-6s %3d  │\n", tasks[i].name, ds,
                   dl, diff_str(tasks[i].difficulty),
                   imp_str(tasks[i].importance), tasks[i].exp_reward);
        }

        printf(
            "  └────────────────────────────────────────────────────────────"
            "───────────────────┘\n");
        term_reset();
    }
    printf("\n");
}

static void ShowHelp(void) {
    printf("\n");
    printf("  ┌─── Command List ──────────────────────────┐\n");
    printf("  │  h  - Show help                           │\n");
    printf("  │  i  - Player information                  │\n");
    printf("  │  as - Add skill       at - Add task       │\n");
    printf("  │  ar - Generate today's retrospective      │\n");
    printf("  │  ds - Delete skill    dt - Delete task    │\n");
    printf("  │  dr - Delete retrospective                │\n");
    printf("  │  t  - Show / finish tasks                 │\n");
    printf("  │  r  - Browse retrospectives               │\n");
    printf("  │  q  - Save and quit                       │\n");
    printf("  └───────────────────────────────────────────┘\n");
    printf("\n");
}

/* ───────────────────────────── Game logic ──────────────────────────── */

static void CheckLevelUp(void) {
    while (player.exp >= player.exp_to_next) {
        player.exp -= player.exp_to_next;
        player.level++;
        player.exp_to_next = player.level * 100;
        term_color(C_YELLOW);
        printf("\n  ★ Level Up! Lv.%d! ★\n", player.level);
        printf("  ★ Next: %d exp ★\n\n", player.exp_to_next);
        term_reset();
    }
}

static void ShowTask(void) {
    if (player.task_count == 0) {
        printf("  No tasks yet. Use 'at' to add.\n");
        return;
    }

    term_color(C_YELLOW);
    printf(
        "\n  ┌─── Tasks "
        "─────────────────────────────────────────────────────────────────────┐"
        "\n");
    printf(
        "  │  #   Status  Name                Start        Deadline     Diff   "
        "Imp    Exp  │\n");
    printf(
        "  │  ─── ──────  ──────────────────  ──────────── ──────────── ────── "
        "────── ──── │\n");
    for (int i = 0; i < player.task_count; i++) {
        char ds[16], dl[16];
        fmt_date(tasks[i].start_time, ds, sizeof(ds));
        fmt_date(tasks[i].deadline, dl, sizeof(dl));

        printf("  │  %-3d ", i);

        if (tasks[i].done) {
            term_color(C_GREEN);
            printf("[✓]   ");
        } else {
            term_color(C_RED);
            printf("[ ]   ");
        }
        term_color(C_YELLOW);

        printf("  %-18s  %-12s %-12s %-6s %-6s %4d │\n", tasks[i].name, ds, dl,
               diff_str(tasks[i].difficulty), imp_str(tasks[i].importance),
               tasks[i].exp_reward);
    }
    printf(
        "  "
        "└─────────────────────────────────────────────────────────────────────"
        "──────────┘\n");
    term_reset();

    printf("  Enter task # to complete (-1 cancel): ");
    int idx;
    if (read_int(&idx) == -1 || idx == -1) return;
    if (idx < 0 || idx >= player.task_count) {
        printf("  Invalid task number.\n");
        return;
    }
    if (tasks[idx].done) {
        printf("  [%s] is already completed.\n", tasks[idx].name);
        return;
    }

    tasks[idx].done = 1;
    player.exp += tasks[idx].exp_reward;

    if (tasks[idx].skill_idx >= 0 &&
        tasks[idx].skill_idx < player.skill_count) {
        skills[tasks[idx].skill_idx].exp += tasks[idx].exp_reward;
        printf("  Skill [%s] +%d exp\n", skills[tasks[idx].skill_idx].name,
               tasks[idx].exp_reward);
    }

    term_color(C_GREEN);
    printf("  Completed [%s], gained %d exp!\n", tasks[idx].name,
           tasks[idx].exp_reward);
    term_reset();
    CheckLevelUp();
}

/* ───────────────────────────── Skill CRUD ──────────────────────────── */

static void AddSkill(void) {
    if (player.skill_count >= MAX_SKILL) {
        term_color(C_RED);
        printf("  Skill list full!\n");
        term_reset();
        return;
    }
    char name[MAX_NAME];
    printf("  Skill name: ");
    if (read_line_raw(name, sizeof(name)) == -1 || !name[0]) {
        printf("  Empty name.\n");
        return;
    }
    for (int i = 0; i < player.skill_count; i++)
        if (!strcmp(skills[i].name, name)) {
            printf("  Already exists.\n");
            return;
        }

    strcpy(skills[player.skill_count].name, name);
    skills[player.skill_count].exp = 0;
    player.skill_count++;
    term_color(C_GREEN);
    printf("  [%s] added!\n", name);
    term_reset();
}

static void DelSkill(void) {
    if (player.skill_count == 0) {
        printf("  No skills.\n");
        return;
    }
    for (int i = 0; i < player.skill_count; i++)
        printf("  [%d] %s (Exp: %d)\n", i, skills[i].name, skills[i].exp);

    printf("  Delete (-1 cancel): ");
    int idx;
    if (read_int(&idx) == -1 || idx == -1) return;
    if (idx < 0 || idx >= player.skill_count) {
        printf("  Invalid.\n");
        return;
    }

    for (int i = 0; i < player.task_count; i++) {
        if (tasks[i].skill_idx == idx)
            tasks[i].skill_idx = -1;
        else if (tasks[i].skill_idx > idx)
            tasks[i].skill_idx--;
    }
    for (int i = idx; i < player.skill_count - 1; i++)
        skills[i] = skills[i + 1];
    player.skill_count--;
    term_color(C_GREEN);
    printf("  Deleted.\n");
    term_reset();
}

/* ───────────────────────────── Task CRUD ───────────────────────────── */

static void AddTask(void) {
    if (player.task_count >= MAX_TASK) {
        term_color(C_RED);
        printf("  Task list full!\n");
        term_reset();
        return;
    }

    Task t;
    memset(&t, 0, sizeof(t));
    t.start_time = time(NULL);
    t.skill_idx = -1;

    printf("  Task name: ");
    if (read_line_raw(t.name, sizeof(t.name)) == -1 || !t.name[0]) {
        printf("  Empty.\n");
        return;
    }

    /* optional: link to skill */
    if (player.skill_count > 0) {
        printf("  Link to skill? (y/n): ");
        char yn[8];
        if (read_line_raw(yn, sizeof(yn)) == -1) return;
        if (yn[0] == 'y' || yn[0] == 'Y') {
            for (int i = 0; i < player.skill_count; i++)
                printf("    [%d] %s\n", i, skills[i].name);
            printf("  Skill # (-1 skip): ");
            if (read_int(&t.skill_idx) == -1 || t.skill_idx < -1 ||
                t.skill_idx >= player.skill_count)
                t.skill_idx = -1;
        }
    }

    /* deadline */
    printf("  Deadline (YYYY-MM-DD): ");
    char dbuf[32];
    if (read_line_raw(dbuf, sizeof(dbuf)) == -1) return;
    int y, m, d;
    if (sscanf(dbuf, "%d-%d-%d", &y, &m, &d) != 3 || y < 1900 || m < 1 ||
        m > 12 || d < 1 || d > 31) {
        printf("  Invalid date.\n");
        return;
    }
    struct tm dl;
    memset(&dl, 0, sizeof(dl));
    dl.tm_year = y - 1900;
    dl.tm_mon = m - 1;
    dl.tm_mday = d;
    dl.tm_isdst = -1;
    t.deadline = mktime(&dl);

    /* difficulty & importance */
    printf("  Difficulty (1-Easy 2-Medium 3-Hard): ");
    if (read_int(&t.difficulty) == -1 || t.difficulty < 1 || t.difficulty > 3) {
        printf("  Invalid.\n");
        return;
    }
    printf("  Importance (1-Minor 2-Normal 3-Major 4-Fatal): ");
    if (read_int(&t.importance) == -1 || t.importance < 1 || t.importance > 4) {
        printf("  Invalid.\n");
        return;
    }

    /* exp reward */
    printf("  Exp reward: ");
    if (read_int(&t.exp_reward) == -1 || t.exp_reward <= 0) {
        printf("  Invalid.\n");
        return;
    }

    tasks[player.task_count++] = t;
    term_color(C_GREEN);
    printf("  [%s] added! %s / %s, deadline %d-%02d-%02d (+%d exp)\n", t.name,
           diff_str(t.difficulty), imp_str(t.importance), y, m, d,
           t.exp_reward);
    term_reset();
}

static void DelTask(void) {
    if (player.task_count == 0) {
        printf("  No tasks.\n");
        return;
    }
    for (int i = 0; i < player.task_count; i++)
        printf("  [%d] %s %s\n", i, tasks[i].done ? "[✓]" : "[ ]",
               tasks[i].name);

    printf("  Delete (-1 cancel): ");
    int idx;
    if (read_int(&idx) == -1 || idx == -1) return;
    if (idx < 0 || idx >= player.task_count) {
        printf("  Invalid.\n");
        return;
    }

    for (int i = idx; i < player.task_count - 1; i++) tasks[i] = tasks[i + 1];
    player.task_count--;
    term_color(C_GREEN);
    printf("  Deleted.\n");
    term_reset();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Retrospective — Markdown 文件系统
 * ═══════════════════════════════════════════════════════════════════════ */

static void get_today_str(char* buf, int size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d", t);
}

static int ensure_retro_dir(void) {
    struct stat st;
    if (stat(RETRO_DIR, &st) == 0) return 0;
    if (MKDIR(RETRO_DIR) == 0) return 0;
    perror("  Cannot create " RETRO_DIR);
    return -1;
}

static int ensure_template(void) {
    FILE* fp = fopen(RETRO_TPL_FILE, "r");
    if (fp) {
        fclose(fp);
        return 0;
    }

    fp = fopen(RETRO_TPL_FILE, "w");
    if (!fp) {
        perror("  Cannot create template");
        return -1;
    }
    fputs(DEFAULT_TEMPLATE, fp);
    fclose(fp);
    printf("  Default template created: %s\n", RETRO_TPL_FILE);
    return 0;
}

static void apply_template(char* out, int out_size, const char* tpl,
                           const char* date, const char* weekday) {
    int pos = 0;
    for (const char* p = tpl; *p && pos < out_size - 1;) {
        if (strncmp(p, "{DATE}", 6) == 0) {
            int n = (int)strlen(date);
            if (pos + n < out_size - 1) {
                memcpy(out + pos, date, n);
                pos += n;
            }
            p += 6;
        } else if (strncmp(p, "{WEEKDAY}", 9) == 0) {
            int n = (int)strlen(weekday);
            if (pos + n < out_size - 1) {
                memcpy(out + pos, weekday, n);
                pos += n;
            }
            p += 9;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
}

static int open_in_editor(const char* filepath) {
    char cmd[MAX_PATH_LEN + 64];
    const char* editor = getenv("EDITOR");

    if (editor && editor[0])
        snprintf(cmd, sizeof(cmd), "%s \"%s\"", editor, filepath);
    else
        snprintf(cmd, sizeof(cmd), "vim \"%s\"", filepath);

    printf("  Opening editor... (close editor to return)\n");
    return system(cmd);
}

static int scan_retro_files(char list[][FNAME_LEN], int max) {
    DIR* dir = opendir(RETRO_DIR);
    if (!dir) return 0;

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && count < max) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "template.md") == 0) continue;

        size_t len = strlen(entry->d_name);
        if (len < 4 || strcmp(entry->d_name + len - 3, ".md") != 0) continue;

        strncpy(list[count], entry->d_name, FNAME_LEN - 1);
        list[count][FNAME_LEN - 1] = '\0';
        count++;
    }
    closedir(dir);

    /* sort descending */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (strcmp(list[i], list[j]) < 0) {
                char tmp[FNAME_LEN];
                strcpy(tmp, list[i]);
                strcpy(list[i], list[j]);
                strcpy(list[j], tmp);
            }

    return count;
}

static int pick_retro_file(char* out, int out_size) {
    if (ensure_retro_dir() == -1) return -1;

    char files[MAX_RETRO_FILES][FNAME_LEN];
    int count = scan_retro_files(files, MAX_RETRO_FILES);

    if (count == 0) {
        printf("  No retrospectives yet. Use 'ar' to create today's.\n");
        return -1;
    }

    term_color(C_PURPLE);
    printf("\n  ┌─── Retrospectives ────────────────────────┐\n");
    for (int i = 0; i < count; i++) {
        char label[FNAME_LEN];
        strcpy(label, files[i]);
        label[strlen(label) - 3] = '\0';
        printf("  │  [%2d]  %-34s │\n", i, label);
    }
    printf("  └───────────────────────────────────────────┘\n");
    term_reset();

    printf("  Select (-1 cancel): ");
    int idx;
    if (read_int(&idx) == -1 || idx == -1) return -1;
    if (idx < 0 || idx >= count) {
        printf("  Invalid.\n");
        return -1;
    }

    snprintf(out, out_size, "%s/%s", RETRO_DIR, files[idx]);
    return 0;
}

static void render_md_line(const char* line) {
    if (strncmp(line, "# ", 2) == 0) {
        term_color(C_YELLOW);
        term_bold();
        printf("  ▎ %s\n", line + 2);
        term_reset();
    } else if (strncmp(line, "## ", 3) == 0) {
        term_color(C_CYAN);
        printf("    ▸ %s\n", line + 3);
        term_reset();
    } else if (strncmp(line, "---", 3) == 0) {
        term_color(C_GREEN);
        printf("  ────────────────────────────────────────────\n");
        term_reset();
    } else if (strncmp(line, "> ", 2) == 0) {
        term_color(C_PURPLE);
        printf("    │ %s\n", line + 2);
        term_reset();
    } else if (line[0] == '\0') {
        putchar('\n');
    } else {
        printf("    %s\n", line);
    }
}

static void AddRetrospective(void) {
    if (ensure_retro_dir() == -1) return;

    FILE* fp = fopen(RETRO_TPL_FILE, "r");
    if (!fp) {
        term_color(C_RED);
        printf("  Template not found: %s\n", RETRO_TPL_FILE);
        printf("  Please create it manually.\n");
        term_reset();
        return;
    }

    char date[16];
    get_today_str(date, sizeof(date));

    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s.md", RETRO_DIR, date);

    FILE* check = fopen(filepath, "r");
    if (check) {
        fclose(check);
        printf("  Today's retrospective exists:\n    %s\n", filepath);
    } else {
        char tpl[MAX_LONG];
        int n = (int)fread(tpl, 1, sizeof(tpl) - 1, fp);
        tpl[n] = '\0';
        fclose(fp);

        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        char content[MAX_LONG];
        apply_template(content, sizeof(content), tpl, date,
                       WEEKDAY_CN[tm_now->tm_wday]);

        FILE* out = fopen(filepath, "w");
        if (!out) {
            perror("  Cannot create file");
            return;
        }
        fputs(content, out);
        fclose(out);

        term_color(C_GREEN);
        printf("  Created: %s\n", filepath);
        term_reset();
    }

    printf("  Open in editor? (y/n): ");
    char yn[8];
    if (read_line_raw(yn, sizeof(yn)) != -1 && (yn[0] == 'y' || yn[0] == 'Y')) {
        open_in_editor(filepath);
        term_clear();
        DisplayInfo();
    }
}

static void ShowRetrospective(void) {
    char filepath[MAX_PATH_LEN];
    if (pick_retro_file(filepath, sizeof(filepath)) == -1) return;

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        perror("  Cannot open");
        return;
    }

    term_color(C_CYAN);
    printf("\n  ─── %s ───\n\n", filepath);
    term_reset();

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        render_md_line(line);
    }
    fclose(fp);
    printf("\n");
}

static void DelRetrospective(void) {
    char filepath[MAX_PATH_LEN];
    if (pick_retro_file(filepath, sizeof(filepath)) == -1) return;

    printf("  Delete %s? (y/n): ", filepath);
    char yn[8];
    if (read_line_raw(yn, sizeof(yn)) == -1 || (yn[0] != 'y' && yn[0] != 'Y')) {
        printf("  Cancelled.\n");
        return;
    }

    if (remove(filepath) == 0) {
        term_color(C_GREEN);
        printf("  Deleted.\n");
        term_reset();
    } else {
        perror("  Delete failed");
    }
}

/* ───────────────────────────── Persistence ─────────────────────────── */

static void WriteDBFile(void) {
    FILE* fp = fopen(DB_FILE, "wb");
    if (!fp) {
        perror("  Save failed");
        return;
    }

    fwrite(&player, sizeof(Player), 1, fp);
    if (player.skill_count > 0)
        fwrite(skills, sizeof(Skill), player.skill_count, fp);
    if (player.task_count > 0)
        fwrite(tasks, sizeof(Task), player.task_count, fp);
    fclose(fp);
    printf("  Data saved!\n");
}

static void ReadDBFile(void) {
    FILE* fp = fopen(DB_FILE, "rb");
    if (!fp) {
        strcpy(player.name, "Yu Lucky");
        strcpy(player.grade, "Sophomore");
        strcpy(player.major, "Integrated Circuit Design");
        player.level = 1;
        player.exp = 0;
        player.exp_to_next = 10;
        player.skill_count = 0;
        player.task_count = 0;
        return;
    }

    if (fread(&player, sizeof(Player), 1, fp) != 1) {
        memset(&player, 0, sizeof(player));
        player.level = 1;
        player.exp_to_next = 10;
        fclose(fp);
        return;
    }

    if (player.skill_count < 0 || player.skill_count > MAX_SKILL)
        player.skill_count = 0;
    if (player.task_count < 0 || player.task_count > MAX_TASK)
        player.task_count = 0;

    if (player.skill_count > 0)
        fread(skills, sizeof(Skill), player.skill_count, fp);
    if (player.task_count > 0)
        fread(tasks, sizeof(Task), player.task_count, fp);

    fclose(fp);
}

static void Quit(void) {
    WriteDBFile();
    printf("  Goodbye! Keep going!\n");
    exit(0);
}

/* ───────────────────────────── Input ───────────────────────────────── */

static int GetInput(char* input, char* target) {
    printf("  > ");
    fflush(stdout);

    char line[16];
    if (read_line_raw(line, sizeof(line)) == -1) return -1;

    if (line[0] == '\0') {
        *input = '\0';
        *target = '\0';
        return 0;
    }

    *input = line[0];

    if (*input == 'a' || *input == 'd')
        *target = line[1] ? line[1] : '\0';
    else
        *target = '\0';

    return 0;
}

/* ───────────────────────────── Main ────────────────────────────────── */

int main(void) {
    char input, target;

    ReadDBFile();
    Welcome();
    DisplayInfo();
    ShowHelp();

    while (1) {
        if (GetInput(&input, &target) == -1) break;
        if (input == '\0') continue;

        switch (input) {
            case 'h':
                ShowHelp();
                break;
            case 'i':
                DisplayInfo();
                break;
            case 't':
                ShowTask();
                break;
            case 'r':
                ShowRetrospective();
                break;
            case 'q':
                Quit();
                break;
            case 'a':
                switch (target) {
                    case 's':
                        AddSkill();
                        break;
                    case 't':
                        AddTask();
                        break;
                    case 'r':
                        AddRetrospective();
                        break;
                    default:
                        printf(
                            "  as = Add Skill\n"
                            "  at = Add Task\n"
                            "  ar = Add Retrospective\n");
                }
                break;
            case 'd':
                switch (target) {
                    case 's':
                        DelSkill();
                        break;
                    case 't':
                        DelTask();
                        break;
                    case 'r':
                        DelRetrospective();
                        break;
                    default:
                        printf(
                            "  ds = Delete Skill\n"
                            "  dt = Delete Task\n"
                            "  dr = Delete Retrospective\n");
                }
                break;
            default:
                printf("  Unknown '%c'. Type 'h' for help.\n", input);
        }
    }

    WriteDBFile();
    printf("\n  EOF reached, data saved. Goodbye!\n");
    return 0;
}
