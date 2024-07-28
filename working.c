#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#define CHUNK_SIZE 1024


int checkFilePermissions(char *filename){
    if (access(filename, R_OK) != 0){
        printf("No read permission for file %s.\n", filename);
        return 0;
    }

    return 1;
}


// Zip function to pack files into one archive
void zip(char *arc_file_name, int argc, char *argv[], size_t chunk_size){
    int arc_file = open(arc_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR); // create an archive
    if (arc_file == -1) {
        perror("Error opening archive file");
        exit(EXIT_FAILURE);
    }

    int cur_file;
    int size[128]; // The program can archive up to 128 files
    dprintf(arc_file, "%d\n", argc - 3); // Write a number of files

    for (int i = 0; i < argc - 3; i++){
        struct stat st;
        if (stat(argv[i+2], &st) == -1) {
            perror("Error getting file info");
            close(arc_file);
            exit(EXIT_FAILURE);
        }
        size[i] = st.st_size; // Count the file's size
        dprintf(arc_file, "%s - %d\n", argv[i+2], size[i]); // Write the file's name and size
    }

    for (int i = 0; i < argc - 3; i++){
        cur_file = open(argv[i+2], O_RDONLY);
        if (cur_file == -1) {
            perror("Error opening file");
            close(arc_file);
            exit(EXIT_FAILURE);
        }
        
        char* temp;
        for (size_t offset = 0; offset < size[i]; offset += chunk_size){
            size_t length = (size[i] - offset > chunk_size) ? chunk_size : size[i] - offset;
            size_t page_offset = offset - (offset % getpagesize());
            temp = mmap(NULL, length, PROT_READ, MAP_PRIVATE, cur_file, page_offset);
            if (temp == MAP_FAILED){
                perror("Error mapping file");
                close(cur_file);
                close(arc_file);
                exit(EXIT_FAILURE);
            }
            
            ssize_t written = write(arc_file, temp + offset - page_offset, length); // Write the file's content into the archive
            if (written == -1){
                perror("Error writing to archive file");
                munmap(temp, length);
                close(cur_file);
                close(arc_file);
                exit(EXIT_FAILURE);
            } 
            else if (written != length){
                fprintf(stderr, "Warning: Not all data was written to file.\n");
            }

            if (munmap(temp, length + offset - page_offset) == -1){
                perror("Error unmapping file");
                close(cur_file);
                close(arc_file);
                exit(EXIT_FAILURE);
            }
        }

        printf("File %s added\n", argv[i+2]);

        if (close(cur_file) == -1){
            perror("Error closing file");
            close(arc_file);
            exit(EXIT_FAILURE);
        }
    }

    if (close(arc_file) == -1) {
        perror("Error closing archive file");
        exit(EXIT_FAILURE);
    }
}


// Unzip function to unpack files from archive
void unzip(char *arc_file_name, size_t chunk_size){
    FILE *arc_file = fopen(arc_file_name, "rb");
    if (arc_file == NULL) {
        perror("Error opening archive file");
        exit(EXIT_FAILURE);
    }

    char count[10], *end;
    if (fscanf(arc_file, "%s", count) != 1){
        perror("Error reading number of files");
        fclose(arc_file);
        exit(EXIT_FAILURE);
    }
    long long cnt = strtol(count, &end, 10); // Scan the number of files

    int names_start = ftell(arc_file);
    char cur_file_name[128] = {0};
    char dump[128] = {0};
    int cur_pos = 0;
    char temp[128];

    // Print file names
    for (int i = 0; i < cnt; i++){
        if (fscanf(arc_file, "%s", temp) != 1){
            perror("Error reading file name");
            fclose(arc_file);
            exit(EXIT_FAILURE);
        }
        printf("%s\n", temp);

        if (fscanf(arc_file, "%s", temp) != 1 || fscanf(arc_file, "%s", temp) != 1){
            perror("Error reading file size");
            fclose(arc_file);
            exit(EXIT_FAILURE);
        }
    }

    long long unsigned int cur_file_size, content_start = ftell(arc_file), end_ = 0;

    fseek(arc_file, names_start, SEEK_SET);

    for (int i = 0; i < cnt; i++){
        if (fscanf(arc_file, "%s%s%llu", cur_file_name, dump, &cur_file_size) != 3){ //scan the file's name and size
            perror("Error reading file name and size");
            fclose(arc_file);
            exit(EXIT_FAILURE);
        }

        end_ = ftell(arc_file);

        FILE *cur_file = fopen(cur_file_name, "wb"); // Create current file
        if (cur_file == NULL) {
            perror("Error opening file for writing");
            fclose(arc_file);
            exit(EXIT_FAILURE);
        }

        fseek(arc_file, content_start + 1, SEEK_SET); //go to file's content

        char* temp = malloc(chunk_size * sizeof(char*));
        if (temp == NULL) {
            perror("Error allocating memory");
            fclose(cur_file);
            fclose(arc_file);
            exit(EXIT_FAILURE);
        }
        // Read data from file chunk by chunk
        for (size_t offset = 0; offset < cur_file_size; offset += chunk_size){
            size_t length = (cur_file_size - offset > chunk_size) ? chunk_size : cur_file_size - offset;
            if (fread(temp, 1, length, arc_file) != length){
                perror("Error reading file content");
                free(temp);
                fclose(cur_file);
                fclose(arc_file);
                exit(EXIT_FAILURE);
            }
            if (fwrite(temp, 1, length, cur_file) != length){ // Copy the content into file
                perror("Error writing file content");
                free(temp);
                fclose(cur_file);
                fclose(arc_file);
                exit(EXIT_FAILURE);
            }
        }

        content_start += cur_file_size; // Move the content pointer to another file
        fseek(arc_file, end_, SEEK_SET); // Go to the next file
        fclose(cur_file);
        free(temp);
    }

    printf("DONE!\n");
    fclose(arc_file);
}


int main(int argc, char *argv[]) {

    char *arc_file_name;

    // Check if parsed command is not 'zip' or 'unzip'
    if ((argc < 2) || (strcmp("zip", argv[1]) && strcmp("unzip", argv[1]))){
        printf("Unknown operation!\nUse 'zip file1 [file2 ...] archive_name.zip'\n");
        printf("or\n");
        printf("'unzip archive_name.zip'\n");

        return 1;
    }

    // Performing zip operation
    if (!strcmp("zip", argv[1])){
        if (argc < 4){
            printf("Not enough arguments!\n");

            return 1;
        }
        // Check if parsed files exist
        for (int i = 2; i < argc - 1; i++){
            FILE *file = fopen(argv[i], "r");
            if (file == NULL){
                printf("File does not exist! No file named '%s'.\n", argv[i]);

                return 1;
            }
            else{
                fclose(file);
            }
        }

        // Check if parsed archive name can be used for resulting file
        FILE *tmp = fopen(argv[argc-1], "r");
        if (tmp != NULL){
            printf("Can not create archive! File named '%s' already exists.\n", argv[argc-1]);
            
            return 1;

        }
        else{
            tmp = fopen(argv[argc-1], "w");
            if (tmp == NULL){
                printf("Can not create file named '%s'!\n", argv[argc-1]);

                return 1;
            }
            else{
                fclose(tmp);
                remove(argv[argc-1]);
            }
        }

        // Check if user has permission to read files he wants to archive
        for (int i = 2; i < argc - 1; ++i){
            if (!checkFilePermissions(argv[i])) {
                printf("Unable to proceed due to insufficient file permissions.\n");
                exit(EXIT_FAILURE);
            }
        }

        arc_file_name = argv[argc-1];
        zip(arc_file_name, argc, argv, CHUNK_SIZE);


    }
    // Performing unzip operation
    else{
        if (argc != 3){
            printf("Invalid arguments!\n");

            return 1;
        }

        // Check if parsed archive exists
        FILE *file = fopen(argv[argc-1], "r");
        if (file == NULL){
            printf("File does not exist! No file named '%s'.\n", argv[argc-1]);

            return 1;
        }
        else{
            fclose(file);
        }

        // Check if user has permission to read archive file
        if (!checkFilePermissions(argv[2])) {
            printf("Unable to proceed due to insufficient file permissions.\n");
            exit(EXIT_FAILURE);
        }

        arc_file_name = argv[argc-1];
        unzip(arc_file_name, CHUNK_SIZE);
    }

}