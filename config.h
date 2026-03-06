#include <termios.h>
#include <time.h>

enum editorKey { //enumeration of unique values for different each key so we can pass them between functions without issue.
    BACKSPACE = 127, //ASCII value since no escape sequence associated
    ARROW_LEFT = 1000, //out of range for a char, so no conflict with any possible keypress, means that editorRadKey() outputs int rather than char.
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY, 
    PAGE_UP,
    PAGE_DOWN,
};

enum editorHighlight { //syntax highlighting colour enum
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
};

struct editorSyntax {
    char *filetype;
    char **filematch; //array of strings containing a pattern to match a filename against.
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags; //flag containing language-specific highlighting rules.
};

typedef struct erow {
    int idx; //so erow, knows it's own index, allows us to check prior rows
    int size; //size of the row
    int rsize; //size of render row
    char *chars; //*ab in in abuf, this is a dynamic memory buffer which we will probably use to store the text in each time.
    char *render; //rendered text buffer
    unsigned char *hl; //highlighted text buffer.
    int hl_open_comment; //check whether row has open comment
} erow; //typdef so we don't need to write struct erow

struct editorConfig {
    int cx,cy; //indexes of row, and chars buffer
    int rx,ry; //position in the rendered buffer (we need this because tabs are only unpacked in the rendered buffer)
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row; //editor row, this is a pointer to a dynamic array of erow object, which each represent a row.
    int dirty; //value which we will be treating as a boolean, but could be use to see how much has been modified.
    char *filename;
    char statusmsg[80];
    char mode;
    time_t statusmsg_time; //time the message was written so we can then delete it.
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct abuf { //We make a single character array, so we can then write everything at once rather than writing each character individually
    char *b; //Pointer to our memory buffer
    int len;
};

#define ABUF_INIT {NULL,0}; //empty buffer, acts as constructor for abuf type