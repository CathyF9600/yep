#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>


int main() {
    // read the /proc directory and its contents
    DIR *dirp = opendir("/proc");
    struct dirent *dp;
    printf("%5s %s\n", "PID" , "CMD");
    if (dirp) {
        while ((dp = readdir(dirp)) != NULL) {
            // strcat(full_path, dp->d_name);
            // printf("%d\n", dp->d_name[0]); //%c gets char, %d gets ascii
            int ascii = dp->d_name[0];
            if (47 < ascii && ascii < 58) {
                // open /proc/<pid>/status file to get proc’s name
                char *directory;
                directory = malloc(strlen(dp->d_name) + strlen("/proc/") + strlen("/status") + 2);
                directory[0] = '\0';
                strcat(directory, "/proc/");
                strcat(directory, dp->d_name);
                strcat(directory, "/status");
                int descripter = open((const char *)directory, O_RDONLY);
                // printf("the directory is %s\n", directory);
                // printf("the descripter is %d\n", descripter);
                if (descripter) {
                    // get Name in status file
                    char proc_name[30];
                    proc_name[0] = '\0';
                    char buf[30];
                    buf[0] = '\0';
                    int i=0, j = 0;
                    read(descripter, buf, 30); // read the first 10 chars
                    int boo = 0; // signal for "start to write"
                    while (buf[i] != '\n') {
                        if (boo) {
                            // printf("%c", buf[i]);
                            proc_name[j] = buf[i];
                            j++;
                        }
                        if (buf[i] == '\t') {boo = 1;}
                        if (buf[i] == '\n') {boo = 0;}
                        i++;
                    }
                    proc_name[j] = '\0';
                    printf("%5s %s\n", dp->d_name , proc_name);
                    // free(proc_name);
                    // free(buf);
                }
                
                close(descripter);
                free(directory);
            }
        }
    } else {
        perror("Directory doesn't exist.");
        exit(0);
    }
    closedir(dirp);
    
    
    return 0;
}
