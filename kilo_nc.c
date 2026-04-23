/*include*/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <unistd.h> 

/*define*/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

/*data*/
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

typedef struct editorConfig {
    int cx, cy;
    int rx;
    int coloff;
    int rowoff;
    int screenrows, screencols;
    int numrows;
    int dirty; //文件是否被修改过
    char *filename;
    char statusmsg[80]; //状态消息缓冲区
    time_t statusmsg_time;
    erow* row; 
} editorConfig;

editorConfig E;

/*prototypes*/
void editorSetStatusMessage(const char *fmt, ...);

/*key*/
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

/*row operations*/

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        //在制表符处，光标位置需要跳到下一个制表符的对齐位置(KILO_TAB_STOP的倍数)
        if (row->chars[j] == '\t')
        rx += (KILO_TAB_STOP-1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow *row){
    int tabs=0;
    int j=0;
    for(j=0;j<row->size;j++){
    if(row->chars[j]=='\t') tabs++;
    }

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
    int idx=0;
    for(j=0;j<row->size;j++){
    if(row->chars[j]=='\t'){
        row->render[idx++]=' ';
        while(idx%KILO_TAB_STOP!=0) {
            row->render[idx++]=' ';
        }
        }else row->render[idx++]=row->chars[j];
}
    row->render[idx]='\0';
    row->rsize=idx;
}

void editorInsertRow(char *s, int at,size_t len){
    if (at < 0 || at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow)*(E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));//在at位置插入新行，原at及其后的行向后移动一位

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

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
} 

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow* row, int at, int c){
    if (at < 0 || at > row->size) at=row->size;//如果at位置不合法，默认插入到行末
    row->chars = realloc(row->chars, row->size + 2);//为新字符和行尾的\0分配空间
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);//at处和其后的字符向后移一位，包括行尾的\0
    row->size++;
    row->chars[at]=c;
    editorUpdateRow(row);
    E.dirty++;
}

/*editor operations*/
void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        //光标在文件末尾行，添加新行
        editorInsertRow("", E.numrows, 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;//插入字符后光标右移
}
void editorInsertNewline(){
    //在行首插入空行
    if(E.cx==0){
        editorInsertRow("", E.cy, 0);
    }else{  //在光标处拆分当前行为两行
        erow*row=&E.row[E.cy];
        editorInsertRow(&row->chars[E.cx], E.cy+1, row->size - E.cx);
        row=&E.row[E.cy];
        row->size=E.cx;
        row->chars[row->size]='\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx=0; 
}

void editorRowAppendString(erow* row, char* s, size_t len){
    row->chars=realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size+=len;
    row->chars[row->size]='\0';
    editorUpdateRow(row);      
    E.dirty++; 
}

void editorRowDelChar(erow* row,int at){
    if(at<0||at>=row->size) return; //at位置不合法
    memmove(&row->chars[at], &row->chars[at+1], row->size - at);//at+1处和其前的字符向前移一位
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorDelChar(){
    if(E.cy==E.numrows) return; 
    if(E.cx==0&&E.cy==0) return; 

    erow* row=&E.row[E.cy];
    if(E.cx>0){
    editorRowDelChar(row,E.cx-1);
    E.cx--;
    }
    else{
        //如果行首被删除，当前行内容追加到上一行末尾，并删除当前行
        editorRowAppendString(&E.row[E.cy-1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
        E.cx=E.row[E.cy].size - row->size;
    }
}

/*file I/O*/

int editorOpen(char *filename){
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    //读取每行文本（不包括行尾换行符）并分配内存，存入行数组
    while((linelen = getline(&line, &linecap, fp))!= -1){
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')){
            linelen--;
        }
        editorInsertRow(line, E.numrows, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0; //文件刚打开时没有修改
    return 0;
}

//将行数组拼接成一个带"\n"的字符串，返回到调用处
char* editorRowsToString(int *buflen) {
    int totlen = 0;//计算所有行内容的总长度，包括每行的换行符
    for (int j = 0; j < E.numrows; j++) {
        totlen += E.row[j].size + 1; 
    }

    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorSave() {
    if (E.filename == NULL) return; //没有文件名

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);//创建并可读写
    if (fd != -1) {
        if(ftruncate(fd, len)!=-1){ //调整文件大小
        if (write(fd, buf, len) == len) {
            close(fd);
            free(buf);
            E.dirty = 0; //保存成功，文件不再是修改状态
            editorSetStatusMessage("%d bytes written to disk", len);
            return;
        }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*terminal*/

void disableRawMode() {
    endwin();
}

void enableRawMode() {
    initscr();
    if (stdscr == NULL) {
        fprintf(stderr, "Error: Failed to initialize ncurses.\n");
        exit(1);
    }
    atexit(disableRawMode);
    raw();
    noecho();
    keypad(stdscr, TRUE);
    timeout(100);
}

int editorReadKey()
{
    int c = getch();
    if(c == ERR) return -1; //没有输入，返回-1
    switch (c) {
        case KEY_UP:    return ARROW_UP;
        case KEY_DOWN:  return ARROW_DOWN;
        case KEY_LEFT:  return ARROW_LEFT;
        case KEY_RIGHT: return ARROW_RIGHT;
        case KEY_HOME:  return HOME_KEY;
        case KEY_END:   return END_KEY;
        case KEY_PPAGE: return PAGE_UP;   
        case KEY_NPAGE: return PAGE_DOWN; 
        case KEY_DC:    return DEL_KEY;     
        case KEY_ENTER:   return '\r'; //某些终端s ENTER 是 \r
        case KEY_BACKSPACE:
        case 127:       return BACKSPACE; // 某些终端s BACKSPACE 是 127
    }
       // 如果不是功能键常量，检查是否为 ESC 转义序列
    if (c == '\x1b') {
        int seq1 = getch();
        if (seq1 == ERR) {
            // 超时，说明用户只是按了 ESC 键
            return '\x1b';
        }

        int seq2 = getch();
        if (seq2 == ERR) {
            // 只读到一个字符，可能是不完整的序列，当作 ESC 处理
            return '\x1b';
        }
        // 处理形如 ESC [ A 的方向键
        if (seq1 == '[') {
            // 处理带数字的序列，如 ESC [ 5 ~ (PageUp)
            if (seq2 >= '0' && seq2 <= '9') {
                int seq3 = getch();
                if (seq3 == '~') {
                    switch (seq2) {
                        case '1': // fall through
                        case '7': return HOME_KEY;
                        case '4': // fall through
                        case '8': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '3': return DEL_KEY;
                    }
                }
            } else {
                // 处理不带数字的序列，如 ESC [ A (上箭头)
                switch (seq2) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        // 处理形如 ESC O H 的序列（某些终端对 Home/End 的编码）
        else if (seq1 == 'O') {
            switch (seq2) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        // 无法识别的转义序列，返回 ESC
        return '\x1b';
    }
    return c;
}


int getWindowSize(int *rows, int *cols) {
    getmaxyx(stdscr, *rows, *cols);
    return 0;
}

/*output*/

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    //光标在屏幕上方
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    //光标超出屏幕底部
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    //光标在屏幕左侧
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    //光标超出屏幕右侧
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(){
    int y;
    //逐行绘制
    for (y = 0; y < E.screenrows; y++) {
        move(y, 0);
        int filerow = y + E.rowoff; //当前行对应的文件行
        //当前绘制的行在文件行外，显示~
        if(filerow>=E.numrows){
            //空文本，中央显示欢迎信息
        if(y==E.screenrows/3&& E.numrows==0){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor");
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                addch('~');
                padding--;
            }
            while (padding--) addch(' ');
            addnstr(welcome, welcomelen);
        } else {
            addch('~');
        }
        }
        else {
            int len = 0; //该行能展示的内容长度
            if (E.coloff < E.row[filerow].rsize) {
                len = E.row[filerow].rsize - E.coloff;
            }
                //如果行内容超出屏幕右侧，截断显示
                if (len > E.screencols) len = E.screencols;
                addnstr(&E.row[filerow].render[E.coloff], len);
            
        }
        //清除光标到行尾
        clrtoeol();
       //再打印一行用于状态栏
        addch('\n');
    }
    
}

void editorDrawStatusBar(){
//左侧显示文件名和行数，右侧显示总行数和光标所在行列

    attron(A_REVERSE);
    char lstatus[80], rstatus[80];

    int llen=snprintf(lstatus, sizeof(lstatus), "Filename: %s | %d %s", 
    E.filename ? E.filename : "[No Name]", E.numrows,E.dirty ? "(modified)" : "");

    int rlen=snprintf(rstatus, sizeof(rstatus), "%d/%d",E.cy+1,E.numrows);

    if(llen > E.screencols) llen = E.screencols;
    addnstr(lstatus, llen);

    while (llen < E.screencols) {
        if (E.screencols - llen == rlen) {
            addnstr(rstatus, rlen);
            break;
        } else {
            addch(' ');
            llen++;
        }
    }
    attroff(A_REVERSE);
    //用于消息栏
    addstr("\r\n");
}

void editorDrawMessageBar() {
    clrtoeol();
    int msglen = strlen(E.statusmsg);
    if(msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5) { //状态消息显示5秒
        addnstr(E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    editorScroll();
    curs_set(0);
    editorDrawRows();
    editorDrawStatusBar();
    editorDrawMessageBar();
    move(E.cy-E.rowoff, E.rx-E.coloff);
    refresh();
    curs_set(1);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL); //记录状态消息的时间戳
}

/*input*/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx !=0) E.cx--; 
            else if(E.cy>0){
                E.cy--;
                E.cx=E.row[E.cy].size;
            }  
            break;
        case ARROW_RIGHT:
        //在行内可移动，在行末按→，光标跳到下一行行首
            if (row&&E.cx<row->size) E.cx++;
            else if(row&&E.cx==row->size) {
                E.cy++;
                E.cx=0;
            } 
            break;
        case ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
    }
    //限制光标在当前行的内容范围内
    row=(E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
    if(row) E.rx=editorRowCxToRx(row, E.cx);
    else E.rx=0;
}

void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES; //记录剩余的强制退出次数
    int c=editorReadKey();
    if(c==-1) return; 
    switch (c) {
        //回车键
        case('\r'):
        case('\n'):
            editorInsertNewline();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            endwin();
            exit(0);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
            E.cy = E.rowoff;
            } 
            else if (c == PAGE_DOWN) {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows) E.cy = E.numrows;
            }
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if(E.cy<E.numrows)
                E.cx = E.row[E.cy].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
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

        case CTRL_KEY('s'):
            editorSave();
            editorSetStatusMessage("File saved!");
            break;

        case KEY_RESIZE:
            resizeterm(0, 0);
            getmaxyx(stdscr, E.screenrows, E.screencols);
            E.screenrows -= 2;
            return;

        default:
            editorInsertChar(c);
            break;
    }
    quit_times = KILO_QUIT_TIMES; //任何按键都重置强制退出计数
}

/*init*/
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.coloff = 0;
    E.rowoff = 0; 
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0; 
    E.filename = NULL;
    E.statusmsg[0] = '\0'; 
    E.statusmsg_time = 0;

    getWindowSize(&E.screenrows, &E.screencols);
    E.screenrows -= 2; //留2行给状态栏和消息栏
}

/*main*/

int main(int argc, char *argv[])
{    
    enableRawMode();
    initEditor();
    if (argc < 2) {
        fprintf(stderr, "Usage: kilo <filename>\n");
        exit(1);
    }else if(editorOpen(argv[1])!=-1){

        editorSetStatusMessage("Help: Ctrl-Q = quit| Ctrl-S = save");

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }   
}
    return 0;
}