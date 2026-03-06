#include <dirent.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"

#define OPEN_ERROR 1

#define CLOSED_FOLDER 0
#define OPEN_FOLDER 1

Folder* generate_tree(const char *dirname)  {

    DIR* dir;
    struct dirent *ent;
    Folder *folder = malloc(sizeof(Folder));
    dir = opendir(dirname);
    folder->state = CLOSED_FOLDER;
    folder->file_list = malloc(sizeof(File));
    folder->folders = malloc(sizeof(Folder*));
    int folders = 0;
    int files = 0;

    while(dir != NULL)  {
        ent = readdir(dir);
        if (!ent) break;
        if (strlen(ent->d_name) < 3 && strcmp(ent->d_name,".") >= 0) continue;
        if (ent->d_type == 8) {
            files++;
            folder->file_list = realloc(folder->file_list, sizeof(File)*files);
            strcpy(folder->file_list[files-1].name,ent->d_name);
        }
        if (ent->d_type == 4) {
            folders++;
            folder->folders = realloc(folder->folders, sizeof(Folder*)*folders);
            char folder_path[MAX_PATH_LEN];
            snprintf(folder_path,MAX_PATH_LEN,"%s/%s",dirname,ent->d_name);
            Folder* int_folder = generate_tree((const char*)folder_path);
            strcpy(int_folder->name,ent->d_name);
            folder->folders[folders-1] = int_folder;
        }
    }

    closedir(dir);
    folder->num_files = files;
    folder->num_folders = folders;
    return folder;
}

void clear_tree(Folder* folder)    {
    free(folder->file_list);
    free(folder->folders);
    free(folder);
}

int treelen(Folder* folder) {
    int len = folder->num_files;
    for(int i = 0; i < folder->num_folders ; i++)  {
        if (folder->folders[i]->state == CLOSED_FOLDER) len++;
        else len += treelen(folder->folders[i]);
    }
    return len;
}

Page* paginate(const char *dirname) {

    DIR *dir;
    struct dirent *ent;
    Page* page = malloc(sizeof(Page));
    dir = opendir(dirname);
    page->num_folders = 0;
    page->subfolders = malloc(page->num_folders*sizeof(char *));
    page->num_files = 0;
    page->files = malloc(page->num_files*sizeof(char*));
    while(dir != NULL)  {
        ent = readdir(dir);
        if(!ent) break;
        if (strlen(ent->d_name) < 3 && strcmp(ent->d_name,".") >= 0) continue;
        if (ent->d_type == 8)   {
            page->num_files++;
            page->files = realloc(page->files,page->num_files*sizeof(char*));
            page->files[page->num_files-1] = malloc(NAME_BUF_LEN);
            strcpy(page->files[page->num_files-1],ent->d_name);
        }
        if (ent->d_type == 4)   {
            page->num_folders++;
            page->subfolders = realloc(page->subfolders,page->num_folders*sizeof(char*));
            page->subfolders[page->num_folders-1] = malloc(NAME_BUF_LEN);
            strcpy(page->subfolders[page->num_folders-1],ent->d_name);
        }
    }

    closedir(dir);
    return page;
}

int pagelen(Page* page){
    return page->num_files + page->num_folders;
}

/*
void main() {
    //Folder* folder_tree = generate_tree("./");
    //printf("%d",treelen(folder_tree));
    //clear_tree(folder_tree);
    Page* page = paginate("../Matlab/");
    for (int i = 0; i < page->num_folders; i++)   {
        printf("%s\n",page->subfolders[i]);
    }
    for (int i = 0; i < page->num_files; i++)   {
        printf("%s\n",page->files[i]);
    }
}
*/