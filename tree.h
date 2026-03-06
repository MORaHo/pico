#define NAME_BUF_LEN 48
#define MAX_PATH_LEN 100

#ifndef TREEH
#define TREEH

typedef struct File {
    char name[NAME_BUF_LEN];
} File;

typedef struct Folder {
    int state;
    int num_files, num_folders;
    char name[NAME_BUF_LEN];
    File *file_list;
    struct Folder **folders;
} Folder;

typedef struct Page {
    int num_files, num_folders;
    char name[NAME_BUF_LEN];
    char** files;
    char** subfolders;
} Page;

Folder* generate_tree(const char *dirname);
void clear_tree(Folder* folder);
int treelen(Folder* folder);
Page* paginate(const char* dirname);
int pagelen(Page* page);

#endif