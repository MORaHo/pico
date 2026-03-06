/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "tree.h"
#include "config.h"

/*** defines ***/

#define PICO_VERSION "0.0.3"
#define PICO_TAB_STOP 4
#define PICO_QUIT_TIMES 3

#define NUM_PADDING 6
#define TREE_PADDING 31

#define CTRL_KEY(k) ((k) & 0x1f) //bitmask the strips input to it's control key component (removes bits 6-8), so that we can map then check and map them to different behaviours.

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

#define COMMAND_MODE (1<<0)
#define TREE_MODE (1<<1)
#define EDIT_MODE (1<<2)

#define PAGINATED (1<<0)
#define BRANCHED (1<<3)

#define COMMAND_BOOL 0
#define SEARCH_BOOL 1

/*** data ***/

struct editorConfig E;
Folder* T;
Page* P;

int mode = COMMAND_MODE;
int treelength;
int saved_ry = 0;
int saved_cy = 0;
int paginated;
char directory[MAX_PATH_LEN] = "./";

/*** filetypes ***/

char *C_HL_extensions[] = {".c",".h",".cpp",NULL}; //Arrays must terminate with null
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|", NULL
}; //| to denote secondary keywords

char *PY_HL_extensions[] = {".py",NULL};
char *PY_HL_keywords[] = {
    "if", "while", "for", "match", "case", "from", "import", "as", "return", "else", "elif", "try", "except", "raise", "in"
    "not|", "lambda|", "self|", "in|", "and|", "def|", "class|", "or|", "is|", "True|", "False|", 
};

struct editorSyntax HLDB[] = { //Array of editorSyntax structs
    { "c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS }, //struct for .c
    { "py", PY_HL_extensions, PY_HL_keywords, "#", "\"""", "\"""", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, char mode_bool, void (*callback)(char *, int, int)); //callback is a generic function that passes the required inputs after, in this case we are asking a pointer to said function.
void initEditor();

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4); //clear screen
    write(STDOUT_FILENO, "\x1b[H", 3);  //an move cursor to top
    perror(s); //Prints out error message associated with errno associated with function that failed
    exit(1);
}

void disableRawMode()   {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) //runs tcsetattr and checks if it has failed
        die("tcsetattr");
}

void enableRawMode() {

    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // runs tcgetattr and checks if it has failed
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // We change this INPUT flag to stop our program from suspending input after pressing Ctrl+s, and needing to restart it with Ctrl+q, ICRNL turns off the translation of Ctrl+m to Enter, which changes the output value. Some miscellaneous tags were added too.
    raw.c_oflag &= ~(OPOST); // Turns off all output processing, like the conversion of \n to \r\n.
    raw.c_cflag |= (CS8); //sets character size to 8 bits per byte, useful in case program is run on older terminals.
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // We change this LOCAL flag to disable canonical mode, and allow us to read input byte-by-byte rather than line-by-line
    //changing from canonical to raw mode, now means that when we press q, the program will end, without having to first press enter to send the whole line the program

    //The following sets a time for read(), after the time has passed it will return, allow us to run any auxillary processes while not input is given.
    raw.c_cc[VMIN] = 0; //As soon as there is an input, read() returns an output
    raw.c_cc[VTIME] = 1; //These values are in tenths of seconds.


    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // runs tcsetattr and checks if it fails
}

int editorReadKey() { // function for recieving low-level keyboard input
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { //this means that will there is not keypress, the program is stuck here, and the screen will not refresh
        if (nread == -1 && errno != EAGAIN) die("read"); //if the read function fails send an error
    }

    if (c == '\x1b') {

        char seq[3]; //3 to be able to deal with longer escape sequences.

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; //if timeout then we act as if the escape key (which does nothing) was pressed
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; //idem

        if (seq[0] == '[') { //mapping all escape sequence related key to respective enumerated value, so we can then deal with them accordingly.

            if (seq[1] >= '0' && seq[1] <= '9') {

                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '2': return END_KEY;
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }

            }   else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }   else {
        return c; //if read() reads a 1-byte input, return the input, otherwise the loop ignores it.
    }             // This presumably also excludes cases in which read() times out.
}

int getCursorPosition(int *rows, int *cols) {

    char buf[32]; //buffer for finding window size
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;//escape sequence 6n, queries our terminal for the position of the cursor.

    while(i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i],1) != 1) break; //storing the character makeup of the escape sequence to determine help us find the window size.
        if (buf[i] == 'R') break; //since the escape sequence ends with "R", we know we won't need to store anymore characters.
        i++;
    }

    buf[i] = '\0'; //printf expects strings to finish with a 0 byte, so we add it. I don't think this is needed anymore

    if (buf[0] != '\x1b' || buf[1] != '[') return -1; //making sure it matches an escape sequence.
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; //scans the third(=2) element in the buffer to find the sizes and writes them to rows and cols

    return 0;

}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { //In some systems ioctl didn't work, so this branch circumvents the issues by playing around with the cursor position.
        if (write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12) != 12) return -1; //escape sequence C moves the cursor forward, while B moves it down.
        return getCursorPosition(rows,cols);
    }   else {
        *cols = ws.ws_col; //since pointers are passed, this just dereferences them and changes the inside value, without having to change anything else
        *rows = ws.ws_row;
        return 0;
    }
}

/*** syntax highlighting ***/

int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; //strchr returns the pointer of the position of the char within the string if it is present
}

void editorUpdateSyntax(erow *row)  {

    row->hl = realloc(row->hl, row->rsize); //allocate memory to be able to colour each character if need be
    memset(row->hl, HL_NORMAL, row->rsize); //set all character to be drawn normally by default
    //we can use rsize since as we said render and hl have the same length

    if (E.syntax == NULL) return; //if no file normal we return, there is no issue since we have already set hl to normal for every character.

    char **keywords = E.syntax->keywords; //keyword array

    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;
    
    int scs_len = scs ? strlen(scs) : 0; //check if there is effectively a way to write a comment
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1; //we consider the beginning of a line as a separator
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx-1].hl_open_comment); //check if previous row has open_comment

    int i = 0;
    while(i < row->rsize)   {
        char c =row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL; //highlighter or previous colour

        if (scs_len && !in_string & !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) { //check if character is the start of a single-line comment
                memset(&row->hl[i], HL_COMMENT, row->rsize - i); //set whole line to be a comment
                break;
            }
        }

        if (mcs_len && mce_len && !in_string)   {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len))    { //if the following characters match mce
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len); //end the comment
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else  {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) { //if the following characters match mcs
                memset(&row->hl[i], HL_COMMENT, mcs_len); //start the comment
                i += mcs_len;
                in_comment = 1; //if it now inside a comment.
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string)  {
                
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize)    { //this is to take care of cases where there are escape quotes within a string.
                    row->hl[i+1] = HL_STRING;
                    i+= 2; //2 since also take care of the character after.
                    continue;
                }
                
                if (c == in_string) in_string = 0; //if the character matches the quote type with which we start a string, then we end the quote
                i++;
                prev_sep = 1; //if we are done highlighting, the quote can be seen as a separator.
                continue;
            }   else {
                if (c == '"' || c == '\'') { //we start and end a string where " and ' are present.
                    in_string = c; //we set this, so that when another character matches it we know we have finished the string.
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }


        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)){
                row->hl[i] = HL_NUMBER; //set associated character in hl to be coloured according to how render works.
                i++;
                prev_sep = 0; //since this is a number we must set this to zero for the next character.
                continue;
            }
        }

        if (prev_sep)   { //we make sure there is a separator before, so words that have keywords within them are not considered.
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;

                if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i+klen])) { //check if they are the same and if there is a separator.
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2: HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL)    { //check if the inner loop was broken out of
                prev_sep = 0;
                continue; //continue the outer loop
            }
        }

        prev_sep = is_separator(c);
        // we don't need to set highlighter colour due to memset
        i++;
    }

    int changed = (row->hl_open_comment != in_comment); //check whether row is still in multi-line comment.
    row->hl_open_comment = in_comment; //set whether multiline comment has been closed or not
    if (changed && row->idx + 1 < E.numrows) //if it has closed or been opened, and row there are more rows after.
        editorUpdateSyntax(&E.row[row->idx+1]); //update the syntax of the next line (recursive), to match altered status.
}

int editorSyntaxToColor(int hl) { //https://en.wikipedia.org/wiki/ANSI_escape_code
    switch(hl)  {
        case HL_NUMBER: return 31; //foreground red
        case HL_KEYWORD2: return 32; //green
        case HL_KEYWORD1: return 33; //yellow
        case HL_MATCH: return 34; //blue
        case HL_STRING: return 35; //magenta
        case HL_MLCOMMENT:
        case HL_COMMENT: return 36; //cyan
        default: return 37; //foreground white
    }
}

void editorSelectSyntaxHighlight() {

    E.syntax = NULL;
    if (E.filename == NULL) return;

    char *ext = strrchr(E.filename, '.'); //returns pointer to last occurance of a character in a string.
    // if there is no '.' then ext will be set to NULL

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) { //loop through HLDB entries
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) { //loop throught filematch array
            int is_ext = (s->filematch[i][0] == '.'); //if it starts with '.', then it is a file extension
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
                //strcmp returns 0, when both strings are the same.
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++)
                    editorUpdateSyntax(&E.row[filerow]); //after determining the file type we can highlight according to it's rules.
                return;
            }
            i++;
        }
    }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)  {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)  {
        if(row->chars[j] == '\t')
            rx += (PICO_TAB_STOP - 1) - (rx % PICO_TAB_STOP); //adds the number of spaces generated by the tab to our rendered position
        rx++;
    }
    return rx;
}

int editorRowRxtoCx(erow *row, int rx)  {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)  { //this essentially calculated cur_rx until it matches rx, since we know chars is compressed to 1 byte objects, when we match the two, we know we have the right cx.
        if (row->chars[cx] == '\t')
            cur_rx += (PICO_TAB_STOP - 1) - (cur_rx % PICO_TAB_STOP);
        cur_rx++;

        if (cur_rx > rx) return cx; // we want to stop when have reached rx, because th
    }
    return cx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;

    for(j = 0;j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size+tabs*(PICO_TAB_STOP-1) + 1); //allocating space for tabs, since \t already occupies one byte, all we need it to 

    int idx = 0;
    for (j = 0; j<row->size; j++) {
        
        if (row->chars[j] == '\t') { //because tab moves the mouse cursor, just like \r\n and doesn't effectively add any characters, so it never removes the text behind it, so we have to render it on our own.
            
            row->render[idx++] = ' ';
            while (idx % PICO_TAB_STOP != 0) row->render[idx++] = ' '; //we never reset idx because we increase idx as we count, so even if it is 0, after the check it is ticked up and we have to add up to another7 to end the loop
        
        }   else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row); //since have just updated render, we can also do so for hl
}

void editorInsertRow(int at, char *s, size_t len) {

    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); //reallocated a new pointer of the new memory to E.row
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at)); //move rows down one, to add a new row
    //because numrows is always one greater than the index of the last row, E.numrows will be the empty row at the end, which we just push down.
    for (int j = at+1; j <= E.numrows; j++) E.row[j].idx++; //update rows after when this is inserted.

    E.row[at].idx = at; //set own index

    E.row[at].size = len; //we have allocated an arrow of erow object
    E.row[at].chars = malloc(len+1); //allocate a block of memory of size length of linelen+1, and assign the pointer chars to that allocated memory. malloc allows write to the memory, but the size is static in size.
    memcpy(E.row[at].chars,s,len); //copying the line into the allocated memory
    E.row[at].chars[len] = '\0'; //in the row number 'at', at the last character we add '\0'

    //initialize render rows
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row)   {
    free(row->chars);
    free(row->render);
    free(row->hl);
}

void editorDelRow(int at)   {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]); //free memory
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at -1)); //move all rows up one
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--; //update all rows after once this is deleted.
    E.dirty++;
    if (at == E.numrows-1) {
        E.rowoff--;
        E.ry++;
    }
    E.numrows--;
}

void editorRowInsertChar(erow *row, int at, int c)  {

    if (at < 0 || at > row->size) at = row->size; //can't insert without the limits of the string + 1

    row->chars = realloc(row->chars, row->size + 2); //2 to make room for null byte
    memmove(&row->chars[at+1], &row->chars[at], row->size - at +1); //like memcpy but works better if arrays could overlap
    row->size++; //because we only increase it by one, so updateRow doesn't see it and will add it, while also making sure chars also has it.
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len)  {
    row->chars = realloc(row->chars,row->size + len + 1);
    memcpy(&row->chars[row->size],s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at)    {
    
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at); //we just move the characters after one position back, and we don't need space for the null byte since it creates itself.
    row->size--;
    editorUpdateRow(row);
    E.dirty++;

}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (E.cy == E.numrows) editorInsertRow(E.numrows,"",0); //adds row to the end if we are at tilde row
    editorRowInsertChar(&E.row[E.cy],E.cx,c); 
    E.cx++;
}

void editorInsertNewLine()  {
    if (E.cx == 0)  {
        editorInsertRow(E.cy,"",0);
    } else  {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx); //already calls editorUpdateRow()
        row->size = E.cx; //we cut the contents of the current line
        row->chars[row->size] = '\0';
        editorUpdateRow(row); //update row to definitively truncate row
    }
    E.cy++;
    E.ry++;
    E.cx = 0;
}

void editorDelChar() {
    if (E.cy == E.numrows) return; //cursor past end, so there is nothing to delete.
    if (E.cx == 0 && E.cy == 0) return; //can't delete anything before the file

    erow *row = &E.row[E.cy];
    if (E.cx > 0)   {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
        editorDelRow(E.cy);
        E.cy--; 
        E.ry--;
    }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen)   {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++) {
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size; //pointer math
        *p = '\n'; //due to pointer math we are not at the end of the string, so we dereference and place \n there.
        p++;
    }

    return buf;
}

void editorOpen(char *filename) { //This function initialized all the rows at the start, and then we operate on these. Only called if there are command line arguements.

    free(E.filename);
    E.filename = strdup(filename); //allocates memory and duplicates string into allocated memory, passes a pointer.

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL; //char* makes this read only, instead char[] creates a copy in the stack and allows write
    size_t linecap = 0; //default value since linecap will be allocated by linecap
    ssize_t linelen; // signed size_t, which is a signed version of a type that is used to represent the size of allocated block of memory.
    
    // getline(&buffer,&size,stdin); -> buffer where text is store, size of input buffer, and input file. Useful sinc we don't know how much memory to allocate for each line. getline returns -1 when it gets to the end of the file
    while((linelen = getline(&line,&linecap,fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            linelen--; //strip off \n and \r our erow only hold the text for each line, so \r\n are not necessary
        editorInsertRow(E.numrows,line, linelen); //appends text to respective row which is also initialized.
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave()   {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s",0,NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644); //O_CREAT create file if it doesn't exist, O_RDWR read and write if. 0644 is standard permission.
    if (fd != -1)   {
        if (ftruncate(fd, len) != -1) { //truncates file to specific length, makes writing safer.
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }

            editorSelectSyntaxHighlight(); //we call this whenever we save in case the filetype changes.
        }
        close(fd); //we close the file whether or not an error has occured.
    }
    E.dirty = 0;
    free(buf);
    editorSetStatusMessage("can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key, int buflen)   {

    static int last_match = -1; //row last match was on
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl)   {
        memcpy(E.row[saved_hl_line].hl,saved_hl,E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }
    
    if (key == '\r' || key == '\x1b') { //if Esc or Enter are pressed, then search is cancelled, and we reset the search parameters.
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP)    {
        direction = -1;
    } else  {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match; //index of current search row
    int i;
    for (i = 0; i < E.numrows; i++) { //when arrow as slicked they are a keypress, so this function resets, so it won't run out
        
        current += direction; //search is not independent of i, so we can start from where last left off.
        if (current == -1) current = E.numrows -1; //allows wrap around search
        else if (current == E.numrows) current = 0;
        erow *row = &E.row[current];
        char *match = strstr(row->render, query); //checks if query is present in row->render, and returns pointer to matching substring
        
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxtoCx(row, match - row->render); //since we use strstr on row->render, the pointer math be correct for render, but not chars, so we have to convert.
            E.rowoff = E.numrows; //we do this so that editscroll will scroll upwards until the matchinng line is at the top of the screen.
            
            saved_hl_line = current;
            saved_hl = malloc(row->rsize); //we save this, so we can restore it later.
            memcpy(saved_hl, row->hl , row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query)); //convert elements to coloured.
            break;
        }
    }
}

void editorFind()   {

    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",SEARCH_BOOL, editorFindCallback); //editorPrompt will call editorFindCallback every time the search string changes, so search can alwawys change.

    if (query)  {
        free(query);
    } else  {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

static char *rstrstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
        return &haystack[strlen(haystack)-1];//returns last element

    char *result = NULL;
    while (1) {
        char *p = strstr(haystack, needle);
        if (p == NULL)
            break;
        result = p;
        haystack = p + 1;
    }

    return result;
}

/*** command ***/

void editorCommandCallback(char *command, int key, int buflen) {

    if (key == '\x1b') return;
    else if (key == '\r') {
        char *space = strstr(command," ");
        size_t command_len;
        if (space > 0)
            command_len = space - command;
        else 
            command_len = (size_t) buflen;
        char *command_list = malloc(command_len);
        command_list = command;

        if (strstr(command_list,"w"))
            editorSave();
        if (strstr(command_list,"q")) {
            if (E.dirty == 0)   { //if the file has not been modified
                free(T);
                free(P);
                write(STDOUT_FILENO, "\x1b[2J", 4); // clears (whole visible) screen after each keypress (since we call it after each keypress). We remove this to optimize by clearing each line as we draw it.
                write(STDOUT_FILENO, "\x1b[H", 3);
                exit(0);
            } else if (E.dirty != 0 && (strstr(command_list, "!")-command_list == buflen -1)) { //we are sure that the user wants to close, in spite of having modified the file.
                free(T);
                free(P);
                write(STDOUT_FILENO, "\x1b[2J", 4); // clears (whole visible) screen after each keypress (since we call it after each keypress). We remove this to optimize by clearing each line as we draw it.
                write(STDOUT_FILENO, "\x1b[H", 3);
                exit(0);
            }   else
                editorSetStatusMessage("File has been modified, to force closure include ! at the END of the command.");
        }
        if (mode == COMMAND_MODE && strcmp(command_list,"np") == 0)  {
            paginated = PAGINATED;
            mode = TREE_MODE;
            saved_ry = E.ry;
            saved_cy = E.cy;
            E.cy = E.cy-E.ry;
            E.ry = 0;
            P = paginate(directory);
            treelength = pagelen(P);
        }   else if (mode == COMMAND_MODE && strstr(command_list,"n")) {
            paginated = BRANCHED;
            mode = TREE_MODE;
            saved_ry = E.ry;
            saved_cy = E.cy;
            E.cy = E.cy-E.ry;
            E.ry = 0;
            T = generate_tree(directory);
            treelength = treelen(T);
        }
        return;
    }
}

void editorCommand() {
    char *command = editorPrompt(":%s",COMMAND_BOOL,editorCommandCallback);
    free(command);
}

void abAppend(struct abuf *ab, const char *s, int len) {

    char *new = realloc(ab->b,ab->len+len); // -> is equivalent to dereferencing the pointer in the struct and accessing the internal value on the right
    //The above line extends a block of memory the size of the previous string (or free a new block of memory of the right size), plus size of the string we are appending.

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len); //Copies new and adds what s pointers to at the end of it, len is needed to know how large s is, since s is a pointer
    ab->b = new; //We then change the value of ab to the new extended string.
    ab->len += len; //Extending to the above comment this means that we are taking len from the dereferenced ab, and adding the len that was given to the function.
}

void abFree(struct abuf *ab) { //destructor for the string
    free(ab->b); //free up the memory occupied by the string
}

/*** output ***/

void editorScroll() { //we call this at start of refresh
    
    E.rx = 0;
    if (E.cy < E.numrows)   {
        E.rx = editorRowCxToRx(&E.row[E.cy],E.cx); //this allows us to skip to the end of a tab
    }

    if (E.cy < E.rowoff) { //scrolling up
        E.rowoff = E.cy;
        E.ry = 0;
    }
    if (E.cy >= E.rowoff + E.screenrows) { //scrolling down
        E.rowoff = E.cy - E.screenrows + 1;
        E.ry = E.screenrows - 1;
    }
    if (E.rx < E.coloff) { //scroll left
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols - NUM_PADDING - (mode == TREE_MODE ? TREE_PADDING : 0))  { //scroll right
        E.coloff = E.rx - E.screencols + 1 + NUM_PADDING + (mode == TREE_MODE ? TREE_PADDING : 0); //add padding so we don't go back to it.
    }
}

void editorDrawRows(struct abuf *ab) {

    for (int y = 0; y < E.screenrows; y++) { //currently 24 since number of rows is unknown
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) { //any line we are drawing that is over the text file length will have a ~ at the start, or if we are on startup
            
            if (E.numrows == 0 && y == E.screenrows / 3) { //writing welcome message branch
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Pico editor -- version %s", PICO_VERSION); //adds PICO_VERSION to the string.
                if (welcomelen > E.screencols) welcomelen = E.screencols; //trunction to fit terminal width
                int padding = (E.screencols - welcomelen)/2;
                if (padding) { //adding the ~ at the start of the line
                    abAppend(ab,"~",1);
                    padding--;
                }
                while (padding--) abAppend(ab," ",1);
                abAppend(ab,welcome,welcomelen);
            } else {

                if (mode == TREE_MODE)  {
                    char treebuf[50];
                    int fill = 0;
                    switch (paginated)  {
                        case PAGINATED:
                            if (y < P->num_folders) {
                                fill = snprintf(treebuf,sizeof(treebuf)," %s",P->subfolders[y]); 
                                break;
                            }  else if (y >= P->num_folders && y-P->num_folders < P->num_files) {
                                fill = snprintf(treebuf,sizeof(treebuf)," %s",P->files[y-P->num_folders]);
                                break;
                            }   else break;
                        case BRANCHED:
                            if (y < T->num_folders) {
                                fill = snprintf(treebuf,sizeof(treebuf)," %s",T->folders[y]->name);
                                break;
                            } else if (y >= T->num_folders && y-T->num_folders < T->num_files) {
                                fill = snprintf(treebuf,sizeof(treebuf)," %s", T->file_list[y-T->num_folders].name);
                                break;
                            }   else break;
                        default:
                            break;
                    }
                    
                    char space[TREE_PADDING-fill];
                    memset(space,' ',TREE_PADDING-fill-1);
                    
                    if (y == E.ry) abAppend(ab , "\x1b[7m", 4);
                    abAppend(ab,treebuf,fill);
                    abAppend(ab,space,TREE_PADDING-fill-1);
                    abAppend(ab , "\x1b[7m", 4);
                    abAppend(ab , "+", 1);
                    abAppend(ab , "\x1b[m", 3);
                }
                abAppend(ab,"~",1); //draws the tilde at the start of each row
            }
        
        }   else {

            if (mode == TREE_MODE)  {
                char treebuf[50];
                int fill = 0;
                switch (paginated)  {
                    case PAGINATED:
                        if (y < P->num_folders) {
                            fill = snprintf(treebuf,sizeof(treebuf)," %s",P->subfolders[y]); 
                            break;
                        }  else if (y >= P->num_folders && y-P->num_folders < P->num_files) {
                            fill = snprintf(treebuf,sizeof(treebuf)," %s",P->files[y-P->num_folders]);
                            break;
                        }   else break;
                    case BRANCHED:
                        if (y < T->num_folders) {
                            fill = snprintf(treebuf,sizeof(treebuf)," %s",T->folders[y]->name);
                            break;
                        } else if (y >= T->num_folders && y-T->num_folders < T->num_files) {
                            fill = snprintf(treebuf,sizeof(treebuf)," %s", T->file_list[y-T->num_folders].name);
                            break;
                        }   else break;
                    default:
                        break;
                }
                
                char space[TREE_PADDING-fill];
                memset(space,' ',TREE_PADDING-fill-1);
                
                if (y == E.ry) abAppend(ab , "\x1b[7m", 4);
                abAppend(ab,treebuf,fill);
                abAppend(ab,space,TREE_PADDING-fill-1);
                abAppend(ab , "\x1b[7m", 4);
                abAppend(ab , "+", 1);
                abAppend(ab , "\x1b[m", 3);
            }

            char nbuf[10];
            int row = y-E.ry;
            if (row<0) row *= -1;
            int nlen = snprintf(nbuf,sizeof(nbuf)," %s%d  ", row < 10 ? "0" : "", row);
            abAppend(ab,nbuf,nlen);

            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0; //since len can be negative if we have scroll to the right enough
            if (len > E.screencols - NUM_PADDING - (mode == TREE_MODE ? TREE_PADDING : 0) + 1) len = E.screencols - NUM_PADDING - (mode == TREE_MODE ? TREE_PADDING : 0) + 1; //truncate  the text
            
            char *c = &E.row[filerow].render[E.coloff]; //the rendered text of filerow-th row into ab
            //the E.coloff index for chars will make the text start from the coloff-th column.
            //It is as a reference, since adding the [E.coloff] returns the literal value, and we want to pass the pointer to that value, so we can start from there
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1; //default value, when text is normal
            int j;
            for (j = 0; j < len; j++)   {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?'; //we are converting come characters to capital letters, or we are printing a ?
                    abAppend(ab, "\x1b[7m", 4); //inverted colours
                    abAppend(ab , &sym, 1); 
                    abAppend(ab, "\x1b[m", 3); //return to normal colours
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color); //since escape sequence [m also cancels the active highlighting, we have to reactivate it
                        abAppend(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) { //we treat normal separately to the rest of the codes
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5); //reset text colour
                        current_color = -1;
                    }
                    abAppend(ab,&c[j],1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen); //set text color
                    }
                    abAppend(ab, &c[j], 1); //print digit
                }
            }
            abAppend(ab, "\x1b[39m", 5); //reset colour
        }

        abAppend(ab, "\x1b[K",3);//clear each line as we draw it
        abAppend(ab,"\r\n",2);
    }
}

void editorDrawStatusBar(struct abuf *ab)   {
    abAppend(ab , "\x1b[7m", 4); //this command invert the colors for the text after
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), " %.20s - %d lines %s%s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified) " : "", mode == COMMAND_MODE ? "| Command Mode" : "| Edit Mode");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d ", E.syntax ? E.syntax->filetype : "no ft", E.cy+1, E.numrows); //indicator of percentage 
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols)  {
        if (E.screencols - len == rlen ){ //when we only have space for the the rstatus string, we string it.
            abAppend(ab, rstatus, rlen);
            break;
        }   else {
            abAppend(ab, " ",1); //we draw the spaces to make sure the whole space still has a white background.
            len++; //we increase so we know when we only have space left for other stuff.
        }
    }
    abAppend(ab, "\x1b[m",3); //returns text to normal colour scheme
    abAppend(ab,"\r\n",2); //space for status bar
}

void editorDrawMessageBar(struct abuf *ab)   {
    
    abAppend(ab,"\x1b[K",3); //erases all character from position to end of line
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols; //truncate
    if (msglen && time(NULL) - E.statusmsg_time < 5) //if there is a message and less than 5 seconds have passed, ad it to the text to be rendered.
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() { //refresh occurs after all processing of inputs has occured
    
    editorScroll(); //since all inputs have already been processed, we already know the definite position of the mouse
    
    struct abuf ab = ABUF_INIT; //starts with an empty string

    abAppend(&ab,"\x1b[?25l", 6); // hides cursor while drawing, so screen doesn't flicker.
    abAppend(&ab,"\x1b[H", 3); // moves cursor to top of (visible) screen, since above command keeps cursor at bottom of (visible) screen. This is also needed so we can make sure to delete every row currectly.
    //The H escape sequence command, determines cursor position, and can take two positional argument to impose the cursor position, the default positions are 1;1

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH", E.ry + 1, (E.rx - E.coloff) + NUM_PADDING + (mode == TREE_MODE ? TREE_PADDING : 0)); //adds the position of the cursor that we want, to a buf, which we can then add to the end of ab to set the mouse position.
    abAppend(&ab, buf, strlen(buf)); //we add the buffer then to the string to move the cursor during refresh
    //snprintf prints a string in a given format to a specified length

    if (mode != TREE_MODE) abAppend(&ab,"\x1b[?25h", 6); // unhide cursor

    //we have essentially added all the instances of what we want to write on screen into a single string
    //so then we can print everything at once, rather than writing each character one at a time.

    write(STDOUT_FILENO, ab.b , ab.len); //this is why we need len
    abFree(&ab); //we don't need ab anymore
}

void editorSetStatusMessage( const char *fmt, ...) { //The ... indicates a variable list of arguments
    va_list ap; //list of variable augments (the ones passed by ...), which we can then use.
    va_start(ap, fmt); //initializes the list, and points to the first variable in the list. The last argument before the ... (fmt for us), must be passed to va_start, so that it can know the adress of the first variable in ...
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap); // s => writes to a string, v=>needs a va_list
    va_end(ap); //ends the list.
    E.statusmsg_time = time(NULL); //sets status-message to the current time
}

/*** input ***/

char *editorPrompt(char *prompt, char mode_bool, void (*callback)(char *, int, int))    { //callback is a is a variable which in this case will be a function that passes (char*, int), and in this case is also a pointer, and outputs void.

    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while(1)    {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey(); //will block here while waiting for character.
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)   {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c, buflen); //callback can either be a function, or if we are not interested, it can also be a NULL, which will mean this branch doesn't pass. We call this to end the search.
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0)    {
                editorSetStatusMessage("");
                if (callback) callback(buf, c, buflen); //we call this to end the search.
                return buf;
            }
        } else if (!iscntrl(c) && c < 128){ //making sure it's not a special character
            if (buflen == bufsize - 1){
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c; //add text to end of prompt, also increases buflen so last character can be a null character
            buf[buflen] = '\0';
        }

        if (mode_bool && callback) callback(buf, c, buflen); //search
    }

}

void editorMoveCursor(int key) { //int because we have associated each keypress with an enumerated value
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; //pointer to row of interest
    
    switch(key) {
        case ARROW_LEFT:
            if (E.cx != 0)
                E.cx--;
            else if (E.cy > 0) {
                E.cy--; //reduce the row count by one
                E.ry--;
                E.cx = E.row[E.cy].size; //sets length to length of previous line
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size)
                E.cx++;
            else if (row && E.cx == row->size) {
                E.cy++; //move to next row
                E.ry++;
                E.cx = 0; //and start of 
            }
            break;
        case ARROW_UP:
            if ((mode != TREE_MODE && E.cy != 0) || (mode == TREE_MODE && E.ry != 0)) {
                E.cy--;
                E.ry--;
            }
            break;
        case ARROW_DOWN:
            if ((mode == TREE_MODE && E.ry < treelength-1) || (mode != TREE_MODE && E.cy < E.numrows)) {
                E.cy++;
                if (E.ry < E.screenrows - 1)
                    E.ry++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0; //check if row has contents
    if (E.cx > rowlen)  {
        E.cx = rowlen;
    }
}

/** keypress processing **/

void editorProcessKeypress() { // function for mapping keypress to given actions.

    int c = editorReadKey();//waits for keypress, editor ReadKey will send it here
    switch (c) {

        case '\r':
            editorInsertNewLine();
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.row[E.cy].size;
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            { //we create a code block, since variable decleration is not possible inside a switch
                if (c == PAGE_UP) {    
                    E.cy = E.rowoff; //changes position to the top of the page.
                }   else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1; //sets position to bottom of page
                    if (E.cy > E.numrows) E.cy = E.numrows; //limit it if the bottom of the screen is beyond the number of rows.
                }

                int times = E.screenrows; //this means that after we have set the position, we will scroll to the bottom or top of the relative page for the new position.
                while (times--) //we don't need to know how many times since we already have already limited the motion of the cursor.
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); //simulate arrow keys, also takes care of stopping at the edge
            } //this makes the code more compact since we don't need to set E.cy, and can just iterate
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            mode = COMMAND_MODE;
            break;

        default:
            editorInsertChar(c);
            break;
    }

}

void commandProcessKeypress() {
    
    int c = editorReadKey();

    switch (c) {

        case ARROW_DOWN:
        case 'j':
            editorMoveCursor(ARROW_DOWN);
            break;
        case ARROW_LEFT:
        case 'h':
            editorMoveCursor(ARROW_LEFT);
            break;
        case ARROW_RIGHT:
        case 'l':
            editorMoveCursor(ARROW_RIGHT);
            break;
        case ARROW_UP:
        case 'k':
            editorMoveCursor(ARROW_UP);
            break;
            
        case 'i':
            mode = EDIT_MODE;
            break;

        case ':':
            editorCommand();
            break;
        
        default:
            break;
    }

}

void treeProcessKeypress()   {
    int c = editorReadKey();

    switch (c) {

        case ARROW_DOWN:
        case 'j':
            editorMoveCursor(ARROW_DOWN);
            break;
        case ARROW_UP:
        case 'k':
            editorMoveCursor(ARROW_UP);
            break;
            
        case 'i':
            mode = EDIT_MODE;
            break;
        case ':':
            editorCommand();
            break;
        
        case BACKSPACE:
            if (paginated == PAGINATED) {
                char new_directory[50];
                if (directory[strlen(directory)-2] == '.')  {
                    snprintf(new_directory,sizeof(new_directory),".%s",directory);

                }   else    {
                    directory[strlen(directory)-1] = '\0'; //remove last /
                    char *last_folder = rstrstr(directory,"/"); //find second to last
                    memset(new_directory,'\0',sizeof(new_directory));
                    memcpy(new_directory,directory,last_folder-directory+1); //find last
                }
                P = paginate(new_directory);
                treelength = pagelen(P);
                memcpy(directory,new_directory,sizeof(new_directory));
            }
            break;

        case '\r':
            if (paginated == PAGINATED) {
                if (E.ry < P->num_folders)  {
                    char new_directory[MAX_PATH_LEN];
                    snprintf(new_directory,sizeof(new_directory),"%s%s/",directory,P->subfolders[E.ry]);
                    P = paginate(new_directory);
                    treelength = pagelen(P);
                    memcpy(directory,new_directory,sizeof(new_directory));
                    break;
                } else  {
                    editorSave();
                    char new_filepath[MAX_PATH_LEN];
                    snprintf(new_filepath,sizeof(new_filepath),"%s%s",directory,P->files[E.ry-P->num_folders]);
                    initEditor();
                    editorOpen(new_filepath);
                    mode = COMMAND_MODE;
                    editorRefreshScreen();
                    break;
                }
            }

        case '\x1b':
            E.ry = saved_ry; //we have saved the value so we can return the to the line we were on before we accessed the tree
            E.cy = saved_cy;
            mode = COMMAND_MODE;
            break;

        default:
            break;
    }
}

/*** init ***/

void initEditor() {
    
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.ry = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL; //since filetype unknown, we don't highlight
    paginated = PAGINATED;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2; //removing screenline limit to add status bar and message bar
}

int main(int argc, char *argv[]) {
    
    enableRawMode(); //draw tilde at rows
    initEditor();
    if (argc >= 2) editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find"); //initial status-message

    while (1) {
        editorRefreshScreen();
        switch (mode)   {
            case COMMAND_MODE:
                commandProcessKeypress();
                continue;
            case TREE_MODE:
                treeProcessKeypress();
                continue;
            case EDIT_MODE:
                editorProcessKeypress(); 
                continue;
        }
    }
    return 0;
}
