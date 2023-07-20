// 特性测试宏, 是代码更具可移植性
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/******************** defines ********************/
#define QEDITOR_VERSION "0.0.1"
#define QEDITOR_TAB_STOP 8
#define QEDITOR_QUIT_TIMES 3

#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/******************** data********************/
// 一个用于存储一行文本的数据类型
typedef struct erow
{
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

// 编辑器配置
struct editorConfig
{
    int cx, cy;     // 光标当前所在的列, 行
    int rx;         // 光标在渲染后的行中的横向位置
    int rowoff;     // 行偏移量
    int coloff;     // 列偏移量
    int screenrows; // 行数
    int screencols; // 列数
    int numrows;    // 整个文件行数
    erow *row;      // 存储每一行的文本信息与渲染信息
    int dirty;
    char *filename;
    char statusmsg[80];          // 状态栏的状态消息文本
    time_t statusmsg_time;       // 状态消息的显示时间戳
    struct termios orig_termios; // 原始的终端属性
};
struct editorConfig E;


/******************** prototypes ********************/
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(char* prompt);






/******************** terminal ********************/

// error handling
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

// 恢复终端的原始模式
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

// 启用原始模式 设置终端特性
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode); // 程序退出时

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

// 读取用户按键输入
int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        // EAGAIN（表示暂时无可用数据）
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    // 控制码的处理
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        // 如果第一个后续字符是 [，则可能是功能键或控制序列。
        if (seq[0] == '[')
        {
            // 如果第二个后续字符是数字字符，表示功能键，需要继续读取一个后续字符
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                /*如果第三个后续字符是波浪符 ~，则根据第二个后续字符确定具体的功能键，
                并返回相应的键盘码（例如 HOME_KEY、DEL_KEY 等）*/

                if (seq[2] == '~')
                {
                    //      \x1b[1~
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            /*根据第二个后续字符确定具体的控制序列，
            并返回相应的键盘码（例如 ARROW_UP、ARROW_DOWN 等）。*/
            {
                //      \x1b[H
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == '0')
        {
            //      \x1b0H
            switch (seq[1])
            {
            case 'H': //\x1b0H 表示将光标定位到屏幕的起始位置。
                return HOME_KEY;
            case 'F': //\x1b0F 表示将光标定位到屏幕的最后一行。
                return END_KEY;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

// 获取终端光标的位置
int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// 获取终端窗口的大小
int getWindowSize2(int *rows, int *cols)
{
    struct winsize ws;
    // 获取终端窗口的大小信息
    // TIOCGWINSZ是一个控制码，表示获取窗口大小的操作
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // 向终端输出一个控制序列（\x1b[999C\x1b[999B,
        // 将光标向右移动999列和向下移动999行
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        // 获取光标的位置，从而获得窗口的大小
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/******************** row operations ********************/

// 将制表符（\t）转换为相应的空格数量
int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
        {
            // 根据余数计算出当前渲染坐标距离下一个制表符位置还需添加的空格数
            rx += (QEDITOR_TAB_STOP - 1) - (rx % QEDITOR_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0; // 制表符数量
    int j;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
            tabs++;
    }
    free(row->render);
    // 原始文本字符数加上需要插入的空格数量, +1 是为了预留 '\0'结尾
    row->render = malloc(row->size + tabs * (QEDITOR_TAB_STOP - 1) + 1);

    int idx = 0; // 记录渲染数据数组的索引
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % QEDITOR_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len)
{
    if(at<0 || at>E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow* row){
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at){
    if(at<0 || at>=E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows-at-1));
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;
    // 调整文本行的字符数组的大小
    row->chars = realloc(row->chars, row->size + 2);
    // 将插入位置之后的字符向后移动一位，为新字符 c 腾出位置
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void EditorRowApendString(erow* row, char* s, size_t len){
    row->chars = realloc(row->chars, row->size+len+1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow* row, int at){
    if(at<0 || at>= row->size) return;
    memmove(&row->chars[at], &row->chars[at+1], row->size-at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}


/******************** editor operations ********************/
void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline(){
    if(E.cx == 0){
        //所在行之前插入一个新空行
        editorInsertRow(E.cy, "", 0);
    }else{
        erow* row = &E.row[E.cy];
        editorInsertRow(E.cy+1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar(){
    if(E.cy == E.numrows) return;
    if(E.cx == 0 && E.cy == 0) return;

    erow* row = &E.row[E.cy];
    if(E.cx >0){
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }else{
        E.cx = E.row[E.cy-1].size;
        EditorRowApendString(&E.row[E.cy-1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/******************** file i/o ********************/
//缓冲区 erow 的数组转换单独字符串
char* editorRowsToString(int* buflen){
    int totlen = 0;
    int j;
    for(j=0; j<E.numrows; j++){
        totlen += E.row[j].size+1;
    }
    *buflen = totlen;

    char* buf = malloc(totlen);
    char* p = buf;
    for(j = 0; j<E.numrows; j++){
        memcpy(p, E.row[j].chars, E.row[j].size);
        p+=E.row[j].size;
        *p='\n';
        p++;
    }
    return buf;
}



// 打开文件并将其内容读取到编辑器的行数组中
void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        {
            linelen--;
        }
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave(){
    if(E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)");
        if(E.filename == NULL){
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char* buf=editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR|O_CREAT, 0664);
    if(fd!=-1){
        if(ftruncate(fd, len) != -1){
            if(write(fd, buf, len) == len){
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/******************** append buffer ********************/
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0};

// append a string S to an abuf
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len); // copy the string S after the end of the current data
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/******************** output ********************/
// 滚动编辑器的内容并调整光标位置
void editorscroll()
{
    E.rx = 0;
    // 光标在有效行范围内
    if (E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

// 绘制屏幕上的每一行内容，并将绘制结果追加到字符缓冲区 abuf
void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;

        // 超出了文本文件的行数，表示需要绘制空行
        if (filerow >= E.numrows)
        {
            // 当缓冲区为空时, 才会显示欢迎消息
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "QEditor -- version %s", QEDITOR_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        // 未超出文本文件的行数，表示需要绘制实际的文本内容
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;

            // 将当前行的渲染内容从列偏移量开始的指定长度 len 追加到字符缓冲区 abuf
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3); // 清除当前行的部分内容
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4); // 反转颜色显示
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.numrows,
                       E.dirty ? "(modified)": "");

    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numrows);
    if (len > E.screencols)
        len = E.screencols;
    abAppend(ab, status, len);

    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // 关闭反转颜色显示 默认0
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

/*
刷新屏幕显示。函数通过操作字符缓冲区 abuf 实现绘制并输出到屏幕上。
*/

void editorRefreshScreen()
{
    editorscroll();

    struct abuf ab = ABUF_INIT;
    /*
    ?25l 是用于隐藏光标的控制码，其中：
        ? 表示参数序列的开始。
        25 是控制码的参数，表示光标的显示/隐藏。
        l 表示将参数应用到相应的设置，这里是将参数应用到光标显示/隐藏设置。
    */
    abAppend(&ab, "\x1b[?25l", 6); // 隐藏光标
    abAppend(&ab, "\x1b[H", 3);    // 将光标移动到屏幕的左上角位置

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    /*将光标位置信息格式化为字符串，并将其添加到缓冲区 ab 中*/
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    /*添加控制码 \x1b[?25h 到缓冲区 ab 中，用于恢复显示光标*/
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len); // buffer's contents out to standard output
    abFree(&ab);                        // free the memory
}

// 设置编辑器状态栏中的消息
void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    // 将可变参数列表初始化为位于fmt之后的参数
    va_start(ap, fmt);
    // 将可变参数列表中的参数按照指定的格式写入到E.statusmsg缓冲区中
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);

    // 将当前时间（以秒为单位）存储在E.statusmsg_time
    E.statusmsg_time = time(NULL);
}

/******************** input ********************/
char* editorPrompt(char* prompt){
    size_t bufsize = 128;
    char* buf = malloc(bufsize);
    
    size_t buflen = 0;
    buf[0] = '\0';

    while(1){
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if(c==DEL_KEY || c== CTRL_KEY('h') || c==BACKSPACE){
            if(buflen != 0) buf[--buflen] = '\0';
        }else if(c == '\x1b'){
            editorSetStatusMessage("");
            free(buf);
            return NULL;
        }else if(c == '\r'){
            if(buflen != 0){
                editorSetStatusMessage("");
                return buf;
            }
        }else if(!iscntrl(c) && c<128){
            if(buflen == bufsize - 1){
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}


// 处理光标移动
void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
        {
            E.cy++;
        }
        break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

void editorProcessKeypress()
{
    static int quit_times = QEDITOR_QUIT_TIMES;

    int c = editorReadKey();
    switch (c)
    {
    case '\r':
        editorInsertNewline();
        break;

    case CTRL_KEY('q'):
        if(E.dirty && quit_times > 0){
            editorSetStatusMessage("WARNING!! File has unsaved changes." 
                "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }

        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if(c==DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;


    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
        {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;


    default:
        editorInsertChar(c);
        break;
    }

    quit_times = QEDITOR_QUIT_TIMES;
}

/*
\x1b 是 ASCII 转义字符的表示，十进制的27, 用于引导控制码的输入
[ 表示控制码的开始
*/

/******************** init ********************/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize2(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    // read keypresses from the user
    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
