/**
 * @file    AdvanceGame.c
 * @author  Lucky
 * @date    2026-5-15
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
#define FNAME_LEN 32 /* "YYYY-MM-DD.md" = 14 ，留足余量 */
#define BAR_WIDTH 20

/* ANSI 颜色 */
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
    int skill_idx; /* 关联技能索引，-1 = 无 */
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

/*
 * 默认模板 —— 首次运行时写入 retrospectives/template.md。
 * 用户可以自行编辑该文件，只要保留 {DATE} 即可。
 * 也可以加入 {WEEKDAY} 等占位符。
 */
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

/* ───────────────────────────── Terminal / I/O helpers ──────────────── */

static void term_clear(void) { printf("\033[2J\033[H"); }
static void term_color(int c) { printf("\033[%dm", c); }
static void term_reset(void) { printf("\033[0m"); }

/**
 * 消耗 stdin 中剩余字符直到换行或 EOF。
 * @return 0 正常，-1 遇到 EOF（主循环应退出）
 */
static int flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    return (c == EOF) ? -1 : 0;
}

static int read_line(char* buf, int size) {
    if (!fgets(buf, size, stdin)) return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

static int read_int(int* out) {
    if (scanf("%d", out) != 1) {
        flush_stdin();
        return -1;
    }
    flush_stdin();
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

static void DisplayInfo(void) {
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

    if (player.skill_count > 0) {
        term_color(C_PURPLE);
        printf("\n  ┌─── Skills ─────────────────────────────────┐\n");
        for (int i = 0; i < player.skill_count; i++)
            printf("  │  [%2d] %-20s Exp: %-11d│\n", i, skills[i].name,
                   skills[i].exp);
        printf("  └────────────────────────────────────────────┘\n");
        term_reset();
    }

    // Task
    if (player.task_count > 0) {
        printf("\n");
        term_color(C_YELLOW);
        printf(
            "  ┌─── Tasks "
            "──────────────────────────────────────────────────────────────────"
            "───┐\n");
        printf(
            "  │  #   Status  Name                Start        Deadline     "
            "Diff   Imp    Exp  │\n");
        printf(
            "  │  ─── ──────  ──────────────────  ──────────── ──────────── "
            "────── ────── ──── │\n");
        for (int i = 0; i < player.task_count; i++) {
            char ds[16], dl[16];
            fmt_date(tasks[i].start_time, ds, sizeof(ds));
            fmt_date(tasks[i].deadline, dl, sizeof(dl));

            /* 索引 */
            printf("  │  %-3d ", i);

            /* 状态（带颜色） */
            if (tasks[i].done) {
                term_color(C_GREEN);
                printf(" [✓]    ");
            } else {
                term_color(C_RED);
                printf(" [ ]    ");
            }
            term_color(C_YELLOW);

            /* 名字 / 开始 / 截止 / 难度 / 重要性 / 经验 */
            printf("%-18s  %-12s %-12s %-6s %-6s %3d  │\n", tasks[i].name, ds,
                   dl, diff_str(tasks[i].difficulty),
                   imp_str(tasks[i].importance), tasks[i].exp_reward);
        }
        printf(
            "  "
            "└─────────────────────────────────────────────────────────────────"
            "─"
            "─────────────┘\n");
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

    /* ---- 表格 ---- */
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

        printf("%-18s  %-12s %-12s %-6s %-6s %4d │\n", tasks[i].name, ds, dl,
               diff_str(tasks[i].difficulty), imp_str(tasks[i].importance),
               tasks[i].exp_reward);
    }
    printf(
        "  "
        "└─────────────────────────────────────────────────────────────────────"
        "─────────┘\n");
    term_reset();

    /* ---- 完成任务 ---- */
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

    /* 关联技能也加经验 */
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
    if (read_line(name, sizeof(name)) == -1 || !name[0]) {
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
    if (read_line(t.name, sizeof(t.name)) == -1 || !t.name[0]) {
        printf("  Empty.\n");
        return;
    }

    /* 可选：关联技能 */
    if (player.skill_count > 0) {
        printf("  Link to skill? (y/n): ");
        char yn[8];
        if (read_line(yn, sizeof(yn)) == -1) return;
        if (yn[0] == 'y' || yn[0] == 'Y') {
            for (int i = 0; i < player.skill_count; i++)
                printf("    [%d] %s\n", i, skills[i].name);
            printf("  Skill # (-1 skip): ");
            if (read_int(&t.skill_idx) == -1 || t.skill_idx < -1 ||
                t.skill_idx >= player.skill_count)
                t.skill_idx = -1;
        }
    }

    /* 截止日期 */
    printf("  Deadline (YYYY-MM-DD): ");
    char dbuf[32];
    if (read_line(dbuf, sizeof(dbuf)) == -1) return;
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

    /* 难度 & 重要性 */
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

    /* 经验奖励 */
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
 *
 *  目录结构:
 *    retrospectives/
 *    ├── template.md        ← 用户可自定义的模板
 *    ├── 2026-05-13.md      ← 往日复盘
 *    ├── 2026-05-14.md
 *    └── 2026-05-15.md      ← 当日复盘
 *
 *  工作流:
 *    1. 用户输入 'ar' → 从模板生成当日 .md 文件
 *    2. 程序询问是否打开编辑器 → 用户在编辑器中填写内容
 *    3. 用户输入 'r'  → 在终端中浏览/查看已有复盘
 * ═══════════════════════════════════════════════════════════════════════ */

static void get_today_str(char* buf, int size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d", t);
}

/** 确保 retrospectives/ 目录存在 */
static int ensure_retro_dir(void) {
    struct stat st;
    if (stat(RETRO_DIR, &st) == 0) return 0; /* 已存在 */
    if (MKDIR(RETRO_DIR) == 0) return 0;     /* 创建成功 */
    perror("  Cannot create " RETRO_DIR);
    return -1;
}

/** 确保模板文件存在，不存在则写入默认模板 */
static int ensure_template(void) {
    FILE* fp = fopen(RETRO_TPL_FILE, "r");
    if (fp) {
        fclose(fp);
        return 0;
    } /* 已存在 */

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

/**
 * 将模板中的 {DATE} 和 {WEEKDAY} 替换为实际值，写入 out。
 */
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

/**
 * 用系统默认编辑器打开文件。
 * 优先使用 $EDITOR 环境变量，否则按平台选择。
 */
static int open_in_editor(const char* filepath) {
    char cmd[MAX_PATH_LEN + 64];
    const char* editor = getenv("EDITOR");

    if (editor && editor[0]) {
        snprintf(cmd, sizeof(cmd), "%s \"%s\"", editor, filepath);
    } else {
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "notepad \"%s\"", filepath);
#elif defined(__APPLE__)
        snprintf(cmd, sizeof(cmd), "open -t \"%s\"", filepath);
#else
        snprintf(cmd, sizeof(cmd), "nano \"%s\"", filepath);
#endif
    }
    printf("  Opening editor... (close editor to return)\n");
    return system(cmd);
}

/**
 * 扫描 retrospectives/ 目录中的 .md 文件（排除 template.md）。
 * 结果按文件名降序排列（即最新日期在前）。
 *
 * @param list  输出文件名数组
 * @param max   数组容量
 * @return      找到的文件数量
 */
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

    /* 冒泡排序：降序，最新日期排在最前面 */
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

/**
 * 通用的复盘文件选择器：列出文件 → 用户选择 → 输出完整路径。
 * @param out      输出缓冲区（完整路径）
 * @param out_size 缓冲区大小
 * @return         0 成功，-1 取消/无文件
 */
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
        /* 去掉 .md 后缀做显示 */
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

/**
 * 终端 Markdown 简易渲染器。
 * 对 # / ## / --- / > 四种元素做彩色显示。
 */
static void render_md_line(const char* line) {
    if (strncmp(line, "# ", 2) == 0) {
        term_color(C_YELLOW);
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

/**
 * ar — 生成当日复盘并可选打开编辑器。
 *
 * 流程:
 *   1. 确保目录和模板存在
 *   2. 如果今天的 .md 已存在 → 提示并询问是否打开编辑
 *   3. 如果不存在 → 读取模板，替换占位符，生成文件，询问是否打开编辑
 */
static void AddRetrospective(void) {
    if (ensure_retro_dir() == -1) return;

    /* 检查模板是否存在 */
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

    /* 今天是否已生成 */
    FILE* check = fopen(filepath, "r");
    if (check) {
        fclose(check);
        printf("  Today's retrospective exists:\n    %s\n", filepath);
    } else {
        /* 读取模板 */
        char tpl[MAX_LONG];
        int n = (int)fread(tpl, 1, sizeof(tpl) - 1, fp);
        tpl[n] = '\0';
        fclose(fp);

        /* 替换占位符 */
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        char content[MAX_LONG];
        apply_template(content, sizeof(content), tpl, date,
                       WEEKDAY_CN[tm_now->tm_wday]);

        /* 写入 */
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

    /* 询问打开编辑器 */
    printf("  Open in editor? (y/n): ");
    char yn[8];
    if (read_line(yn, sizeof(yn)) != -1 && (yn[0] == 'y' || yn[0] == 'Y')) {
        open_in_editor(filepath);
        term_clear();
        DisplayInfo();
    }
}

/**
 * r — 浏览已有复盘（在终端中渲染 Markdown）。
 */
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

/**
 * dr — 删除指定的复盘文件。
 */
static void DelRetrospective(void) {
    char filepath[MAX_PATH_LEN];
    if (pick_retro_file(filepath, sizeof(filepath)) == -1) return;

    printf("  Delete %s? (y/n): ", filepath);
    char yn[8];
    if (read_line(yn, sizeof(yn)) == -1 || (yn[0] != 'y' && yn[0] != 'Y')) {
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
        /* 首次运行 */
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

    /* 防御损坏文件 */
    if (player.skill_count < 0 || player.skill_count > MAX_SKILL)
        player.skill_count = 0;
    if (player.task_count < 0 || player.task_count > MAX_TASK)
        player.task_count = 0;

    if (player.skill_count > 0)
        fread(skills, sizeof(Skill), player.skill_count, fp);
    if (player.task_count > 0)
        fread(tasks, sizeof(Task), player.task_count, fp);
    /* 忽略旧版可能残留的 retro 数据（如有） */

    fclose(fp);
}

static void Quit(void) {
    WriteDBFile();
    printf("  Goodbye! Keep going!\n");
    exit(0);
}

/* ───────────────────────────── Input ───────────────────────────────── */

/**
 * 读取一条命令。
 * 单字符命令: h / i / t / r / q
 * 双字符命令: as / at / ar / ds / dt / dr
 * @return 0 正常，-1 EOF
 */
static int GetInput(char* input, char* target) {
    printf("  > ");
    fflush(stdout);

    int c = getchar();
    if (c == EOF) return -1;

    /* 检测 ESC 转义序列（方向键等），直接吞掉 */
    if (c == '\033') {
        int next = getchar();
        if (next == EOF) return -1;
        if (next == '[') {
            /* 标准 CSI 序列：ESC [ <参数字节...> <结尾字节> */
            while (1) {
                int ch = getchar();
                if (ch == EOF) return -1;
                /* 结尾字节范围 0x40-0x7E（A~Z, a~z, @, [, ], ^, _, {, |, } 等）
                 */
                if (ch >= 0x40 && ch <= 0x7E) break;
            }
        } else if (next == 'O') {
            /* SS3 序列（部分终端用 ESC O A 表示方向键） */
            int ch = getchar();
            if (ch == EOF) return -1;
        }
        /* 转义序列已全部消费，当作无效输入，重新提示 */
        *input = '\0';
        *target = '\0';
        return 0;
    }

    *input = (char)c;

    if (*input == '\n') {
        *input = '\0';
        *target = '\0';
        return 0;
    }

    if (*input == 'a' || *input == 'd') {
        c = getchar();
        if (c == EOF) return -1;
        *target = (char)c;
        if (*target == '\n') {
            *target = '\0';
            return 0;
        }
    } else {
        *target = '\0';
    }

    return flush_stdin();
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

    /* EOF — 自动保存退出 */
    WriteDBFile();
    printf("\n  EOF reached, data saved. Goodbye!\n");
    return 0;
}
