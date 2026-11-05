#include "parse_lfs.h"
#include "../lib/lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>


//free cr struct
void freeCr(CR* cr) {
    if(cr != NULL) {
        if (cr->entries != NULL) {
            free(cr->entries);
            cr->entries = NULL;
        }
        free(cr);
        cr = NULL;
    }
}

//free inode struct
void freeInode(Inode* inode) {
    if (inode != NULL) {
        if (inode->direct_blocks != NULL) {
            free(inode->direct_blocks);
            inode->direct_blocks = NULL;
        }
        free(inode);
        inode = NULL;
    }
}

//free imap struct
void freeImap(Imap* imap) {
    if(imap != NULL) {
        if (imap->entries != NULL) {
            free(imap->entries);
            imap->entries = NULL;
        }
        free(imap);
        imap = NULL;
    }
}

//we need to read the checkpoint in order to find the location of all imaps
CR* read_checkpoint_region(FILE* img_file) {
    //beginning of the file
    fseek(img_file, 0, SEEK_SET);
    //allocate memory for file
    CR* cr = malloc(sizeof(CR));

    if(!cr) {
        return NULL;
    }

    if(fread(&cr->image_offset, sizeof(uint), 1, img_file) != 1) {
        free(cr);
        return NULL;
    }
    if(fread(&cr->num_entries, sizeof(uint), 1, img_file) != 1) {
        free(cr);
        return NULL;
    }

    //allocate memory for each CR entry
    cr->entries = malloc(cr->num_entries * sizeof(CR_Entry));
    if(!cr->entries) {
        free(cr);
        return NULL;
    }

    //read each CR entry
    for (uint i = 0; i < cr->num_entries; i++) {
        int needToFree = 0;
        if(fread(&cr->entries[i].inumber_start, sizeof(uint), 1, img_file) != 1) {
            needToFree = 1;
        }
        else if(fread(&cr->entries[i].inumber_end, sizeof(uint), 1, img_file) != 1) {
            needToFree = 1;
        }
        else if(fread(&cr->entries[i].imap_disk_offset, sizeof(uint), 1, img_file) != 1) {
            needToFree = 1;
        }

        if(needToFree == 1) {
            freeCr(cr);
            return NULL;
        }
    }
    return cr;
}

//read imap with offset
Imap* read_imap(FILE* img_file, uint offset) {
    fseek(img_file, offset, SEEK_SET);
    Imap* imap = malloc(sizeof(Imap));
    if(imap == NULL) {
        return NULL;
    }

    //read number of entries
    if(fread(&imap->num_entries, sizeof(uint), 1, img_file) != 1) {
        free(imap);
        return NULL;
    }

    //allocate memory for imap entries
    imap->entries = malloc(imap->num_entries * sizeof(Imap_Entry));
    if(imap->entries == NULL) {
        free(imap);
        return NULL;
    }

    //reach each imap entry
    for(uint i = 0; i < imap->num_entries; i++) {
        if(fread(&imap->entries[i].inumber, sizeof(uint), 1, img_file) != 1) {
            freeImap(imap);
            return NULL;
        }
        else if(fread(&imap->entries[i].inode_disk_offset, sizeof(uint), 1, img_file) != 1) {
            freeImap(imap);
            return NULL;
        }
    }

    return imap;
}

//read inode with offset
Inode* read_inode(FILE* img_file, uint offset) {
    fseek(img_file, offset, SEEK_SET);
    Inode* inode = malloc(sizeof(Inode));
    if (!inode) {
        return NULL;
    }

    //check inode info
    if (fread(&inode->file_cursor, sizeof(uint), 1, img_file) != 1) {
        free(inode);
        return NULL;
    }
    else if (fread(&inode->size, sizeof(uint), 1, img_file) != 1) {
        free(inode);
        return NULL;
    }
    else if (fread(&inode->permissions, sizeof(mode_t), 1, img_file) != 1) {
        free(inode);
        return NULL;
    }
    else if (fread(&inode->mtime, sizeof(time_t), 1, img_file) != 1) {
        free(inode);
        return NULL;
    }
    else if (fread(&inode->num_direct_blocks, sizeof(uint), 1, img_file) != 1) {
        free(inode);
        return NULL;
    }

    //allocate memory for direct block entry
    inode->direct_blocks = malloc(inode->num_direct_blocks * sizeof(uint));
    if (!inode->direct_blocks) {
        free(inode);
        return NULL;
    }

    //read direct block entry
    for (uint i = 0; i < inode->num_direct_blocks; i++) {
        if (fread(&inode->direct_blocks[i], sizeof(uint), 1, img_file) != 1) {
            freeInode(inode);
            return NULL;
        }
    }

    return inode;
}

//calculate the offset of an inode
uint get_inode_offset(CR* cr, uint inumber, FILE* img_file) {
    //get checkpoint regions with inumber
    for (uint i = 0; i < cr->num_entries; i++) {
        if (inumber >= cr->entries[i].inumber_start && inumber <= cr->entries[i].inumber_end) {
            //need to read Imap
            Imap* imap = read_imap(img_file, cr->entries[i].imap_disk_offset);
            if (!imap) {
                return 0;
            }

            //get the inode entry in Imap
            for (uint j = 0; j < imap->num_entries; j++) {
                if (imap->entries[j].inumber == inumber) {
                    uint offset = imap->entries[j].inode_disk_offset;
                    freeImap(imap);
                    return offset;
                }
            }


            freeImap(imap);
            break;
        }
    }
    //returns 0 if we cannot find
    return 0;
}

//helper function for qsort later on
//0 if equal
//postive if a comes after b
//negative if a comes before b
int compare_dir_entries(const void* a, const void* b) {
    const DirEntry* entry_a = (const DirEntry*)a;
    const DirEntry* entry_b = (const DirEntry*)b;

    if (entry_a->is_directory != entry_b->is_directory) {
        return entry_a->is_directory - entry_b->is_directory;
    }

    return strcmp(entry_a->name, entry_b->name);
}

//called for ls command
void print_dir_tree(FILE* img_file, CR* cr, uint inumber, int depth) {
    uint inode_offset = get_inode_offset(cr, inumber, img_file);
    if (inode_offset == 0) {
        return;
    }

    Inode* inode = read_inode(img_file, inode_offset);
    if (inode == NULL) {
        return;
    }

    if (!S_ISDIR(inode->permissions)) {
        freeInode(inode);
        return;
    }

    //read directory
    DirEntry* entries = NULL;
    int num_entries = 0;
    int max_entries = 0;

    //buffer
    char spanning_buffer[512] = {0};
    size_t spanning_len = 0;
    uint dir_bytes_seen = 0;

    //loop through each block
    for (uint i = 0; i < inode->num_direct_blocks; i++) {
        fseek(img_file, inode->direct_blocks[i], SEEK_SET);

        //"Data blocks are allocated as 4 KB regions"
        //this means that it is 4096 because 1 KB = 1024 bytes and there are 4 KB.
        char buffer[4096] = {0};
        size_t bytes_read = fread(buffer, 1, 4096, img_file);
        //ignore leftover bytes in the block past the end of the directory
        if (bytes_read > inode->size - dir_bytes_seen) {
            bytes_read = inode->size - dir_bytes_seen;
        }
        dir_bytes_seen += bytes_read;
        if (bytes_read == 0) {
            continue;
        }

        char* processing_buffer;
        size_t processing_size;        
        if (spanning_len > 0) {
            char* combined = malloc(spanning_len + bytes_read);
            if (combined == NULL) {
                processing_buffer = buffer;
                processing_size = bytes_read;
            } else {
                memcpy(combined, spanning_buffer, spanning_len);
                memcpy(combined + spanning_len, buffer, bytes_read);
                processing_buffer = combined;
                processing_size = spanning_len + bytes_read;
                //reset
                spanning_len = 0;
            }
        } else {
            processing_buffer = buffer;
            processing_size = bytes_read;
        }

        char* ptr = processing_buffer;
        while (ptr < processing_buffer + processing_size) {
            char* comma = memchr(ptr, ',', processing_buffer + processing_size - ptr);
            if (!comma) {
                if (ptr < processing_buffer + processing_size) {
                    size_t remaining = processing_buffer + processing_size - ptr;
                    if (remaining < sizeof(spanning_buffer)) {
                        memcpy(spanning_buffer, ptr, remaining);
                        spanning_len = remaining;
                    }
                }
                break;
            }
            
            if (comma >= processing_buffer + processing_size - sizeof(uint)) {
                size_t save_len = processing_buffer + processing_size - ptr;
                if (save_len < sizeof(spanning_buffer)) {
                    memcpy(spanning_buffer, ptr, save_len);
                    spanning_len = save_len;
                }
                break;
            }

            size_t name_len = comma - ptr;
            if (name_len == 0 || name_len >= 256) {
                ptr++;
                continue;
            }

            char name[256];
            memcpy(name, ptr, name_len);
            name[name_len] = '\0';

            uint entry_inumber;
            memcpy(&entry_inumber, comma + 1, sizeof(uint));

            uint entry_inode_offset = get_inode_offset(cr, entry_inumber, img_file);
            if (entry_inode_offset == 0) {
                ptr = comma + 1 + sizeof(uint);
                continue;
            }

            Inode* entry_inode = read_inode(img_file, entry_inode_offset);
            if (!entry_inode) {
                ptr = comma + 1 + sizeof(uint);
                continue;
            }

            if (num_entries >= max_entries) {
                max_entries = max_entries == 0 ? 16 : max_entries * 2;
                DirEntry* new_entries = realloc(entries, max_entries * sizeof(DirEntry));
                if (!new_entries) {
                    freeInode(entry_inode);
                    break;
                }
                entries = new_entries;
            }

            int already_exists = 0;
            for (int j = 0; j < num_entries; j++) {
                if (strcmp(entries[j].name, name) == 0) {
                    already_exists = 1;
                    break;
                }
            }

            if (!already_exists) {
                memcpy(entries[num_entries].name, name, name_len + 1);
                entries[num_entries].inumber = entry_inumber;
                entries[num_entries].is_directory = S_ISDIR(entry_inode->permissions);
                num_entries++;
            }
            
            freeInode(entry_inode);


            ptr = comma + 1 + sizeof(uint);
        }

        if (processing_buffer != buffer) {
            free(processing_buffer);
        }
    }

    //use qsort in order to make all the directory entries ordered
    //we have a helper method compare_dir_entries that is specifically for our qsort in order to
    // make a list that is ordered
    if (entries && num_entries > 0) {
        qsort(entries, num_entries, sizeof(DirEntry), compare_dir_entries);
    }

    //print out
    for (int i = 0; i < num_entries; i++) {
        uint entry_inode_offset = get_inode_offset(cr, entries[i].inumber, img_file);
        if (entry_inode_offset == 0) {
            continue;
        }

        Inode* entry_inode = read_inode(img_file, entry_inode_offset);
        if (!entry_inode) {
            continue;
        }

        ls_print_file(entries[i].name, entry_inode->size, entry_inode->permissions, entry_inode->mtime, depth);

        if (S_ISDIR(entry_inode->permissions)) {
            print_dir_tree(img_file, cr, entries[i].inumber, depth + 1);
        }

        freeInode(entry_inode);
    }

    if (entries) {
        free(entries);
    }

    freeInode(inode);
}

//helper when we need to read the path
// we use this so that we can get the user's arguments
// and use it as data we can read
void normalize_path(char* path) {
    char temp[1024] = {0};
    char* result = temp;

    if (path[0] == '/') {
        *result++ = '/';
    }

    char* saveptr = NULL;
    char* path_copy = strdup(path);
    char* token = strtok_r(path_copy, "/", &saveptr);

    while (token) {
        if (strcmp(token, ".") == 0) {
            token = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        if (strcmp(token, "..") == 0) {
            char* last_slash = strrchr(temp, '/');
            if (last_slash && last_slash != temp) {
                *last_slash = '\0';
                result = last_slash;
            }
            token = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        *result++ = '/';
        strcpy(result, token);
        result += strlen(token);
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);

    //empty path 
    if (result == temp) {
        *result++ = '/';
    }

    *result = '\0';

    strcpy(path, temp);
}

//find inode by path
uint find_inode_by_path(FILE* img_file, CR* cr, char* path) {
    //copy for normalization later on
    char path_copy[1024];
    strcpy(path_copy, path);

    //make the path normalized
    normalize_path(path_copy);

    //find root path
    if (strcmp(path_copy, "/") == 0) {
        return 0;
    }

    uint current_inumber = 0;

    char* rest = path_copy;
    
    if (*rest == '/') {
        rest++;
    }

    char* token;
    char* saveptr = NULL;
    char* path_to_tokenize = strdup(rest);
    token = strtok_r(path_to_tokenize, "/", &saveptr);

    while (token) {
        uint inode_offset = get_inode_offset(cr, current_inumber, img_file);
        if (inode_offset == 0) {
            free(path_to_tokenize);
            return 0;
        }

        Inode* inode = read_inode(img_file, inode_offset);
        if (!inode) {
            free(path_to_tokenize);
            return 0;
        }

        if (!S_ISDIR(inode->permissions)) {
            freeInode(inode);
            free(path_to_tokenize);
            return 0;
        }

        int found = 0;
        uint next_inumber = 0;

        //read the whole directory first so entries spanning two blocks stay intact
        char* buffer = malloc(inode->size + 1);
        if (!buffer) {
            freeInode(inode);
            free(path_to_tokenize);
            return 0;
        }
        size_t bytes_read = 0;
        for (uint i = 0; i < inode->num_direct_blocks && bytes_read < inode->size; i++) {
            fseek(img_file, inode->direct_blocks[i], SEEK_SET);

            size_t to_read = inode->size - bytes_read;
            if (to_read > 4096) {
                to_read = 4096;
            }
            bytes_read += fread(buffer + bytes_read, 1, to_read, img_file);
        }

        //go through each directory entry
        char* ptr = buffer;
        while (ptr < buffer + bytes_read) {
            //get the end of filename to process later on
            char* comma = NULL;
            for (char* c = ptr; c < buffer + bytes_read; c++) {
                if (*c == ',') {
                    comma = c;
                    break;
                }
            }
            if (!comma || comma >= buffer + bytes_read - sizeof(uint)) {
                break;
            }

            size_t name_len = comma - ptr;
            if (name_len == 0 || name_len >= 256) {
                ptr++;
                continue;
            }

            char name[256];
            memcpy(name, ptr, name_len);
            name[name_len] = '\0';

            uint entry_inumber;
            memcpy(&entry_inumber, comma + 1, sizeof(uint));

            if (strcmp(name, token) == 0) {
                next_inumber = entry_inumber;
                found = 1;
                break;
            }

            ptr = comma + 1 + sizeof(uint);
        }
        free(buffer);
        freeInode(inode);
        if (!found) {
            free(path_to_tokenize);
            return 0;
        }

        current_inumber = next_inumber;
        token = strtok_r(NULL, "/", &saveptr);
    }
    free(path_to_tokenize);
    return current_inumber;
}

//get and print file contents
int cat_file(FILE* img_file, CR* cr, char* filepath) {
    //find inode
    uint inumber = find_inode_by_path(img_file, cr, filepath);
    if (inumber == 0) {
        fprintf(stderr, "parse_lfs error: Failed to find file.\n");
        return 1;
    }
    //find inode offset
    uint inode_offset = get_inode_offset(cr, inumber, img_file);
    if (inode_offset == 0) {
        fprintf(stderr, "parse_lfs error: Failed to find file.\n");
        return 1;
    }
    //read inode
    Inode* inode = read_inode(img_file, inode_offset);
    if (!inode) {
        fprintf(stderr, "parse_lfs error: Failed to find file.\n");
        return 1;
    }
    //ensure it is a regular file
    if (S_ISDIR(inode->permissions)) {
        fprintf(stderr, "parse_lfs error: Failed to find file.\n");
        freeInode(inode);
        return 1;
    }

    //print file contents after reading it all the way
    uint total_bytes_read = 0;
    for (uint i = 0; i < inode->num_direct_blocks && total_bytes_read < inode->size; i++) {
        fseek(img_file, inode->direct_blocks[i], SEEK_SET);

        uint bytes_to_read = 4096;
        if (total_bytes_read + bytes_to_read > inode->size) {
            bytes_to_read = inode->size - total_bytes_read;
        }

        char buffer[4096];
        if (fread(buffer, 1, bytes_to_read, img_file) != bytes_to_read) {
            fprintf(stderr, "parse_lfs error: Failed to read file data.\n");
            freeInode(inode);
            return 1;
        }
        fwrite(buffer, 1, bytes_to_read, stdout);
        total_bytes_read += bytes_to_read;
    }

    freeInode(inode);
    return 0;
}

int main(int argc, char** argv) {
    //arguments has to be less than 3
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [ls|cat] <image_file> [file_path]\n", argv[0]);
        return 1;
    }

   char* command = argv[1];
   int result = 0;
    //checks if the command is ls or cat. otherwise error
    if (strcmp(command, "ls") == 0) {
        char* image_file = argv[2];
        FILE* img_file = fopen(image_file, "rb");
        if (!img_file) {
            fprintf(stderr, "Failed to open image file: %s\n", image_file);
            return 1;
        }
        CR* cr = read_checkpoint_region(img_file);
        if (!cr) {
            fprintf(stderr, "Failed to read checkpoint region\n");
            fclose(img_file);
            return 1;
        }
        printf("/\n");
        print_dir_tree(img_file, cr, 0, 1);
        freeCr(cr);
        fclose(img_file);
    }
    else if (strcmp(command, "cat") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s cat <file_path> <image_file>\n", argv[0]);
            return 1;
        }
        char* filepath = argv[2];
        char* image_file = argv[3];
        FILE* img_file = fopen(image_file, "rb");
        if (img_file == NULL) {
            fprintf(stderr, "parse_lfs error: Failed to find file.\n");
            return 1;
        }
        
        CR* cr = read_checkpoint_region(img_file);
        if (cr == NULL) {
            fprintf(stderr, "Failed to read checkpoint region\n");
            fclose(img_file);
            return 1;
        }
        
        result = cat_file(img_file, cr, filepath);
        freeCr(cr);
        fclose(img_file);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }

   return result;
}
