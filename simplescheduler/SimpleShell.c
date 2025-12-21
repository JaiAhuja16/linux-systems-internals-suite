#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define FIFO_TEMPLATE "/tmp/simplescheduler_fifo_%d"
#define STATS_FILE_TEMPLATE "/tmp/scheduler_stats_%d"

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE_ms>\n", argv[0]);
        return 1;
    }
    int ncpu = atoi(argv[1]);
    char *tslice = argv[2];
    int uid = getuid();
    char fifo_path[256];
    snprintf(fifo_path, sizeof(fifo_path), FIFO_TEMPLATE, uid);

    // fork and launch scheduler process
    pid_t sched = fork();
    if (sched < 0) { perror("fork"); return 1; }
    if (sched == 0) {
        execl("./SimpleScheduler", "./SimpleScheduler", argv[1], argv[2], (char*)NULL);
        perror("execl scheduler"); _exit(127);
    }
    
    // wait for scheduler to create fifo
    int tries = 0;
    while (access(fifo_path, F_OK) != 0 && tries < 50) { usleep(100000); tries++; }
    
    int fifo_fd = -1;
    if (access(fifo_path, F_OK) == 0) {
        fifo_fd = open(fifo_path, O_WRONLY);
        if (fifo_fd < 0) {
            perror("open fifo for writing");
        }
    } else {
        fprintf(stderr, "Scheduler FIFO not found.\n");
    }

    // main shell loop
    char line[1024];
    while (1) {
        printf("SimpleShell$ "); fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t L = strlen(line); if (L>0 && line[L-1]=='\n') line[L-1]='\0';
        
        if (strncmp(line, "submit ", 7) == 0) {
            char *path = line + 7;
            if (path[0] == '\0') { printf("Usage: submit ./a.out\n"); continue; }
            if (fifo_fd >= 0) {
                char cmd[1024]; snprintf(cmd, sizeof(cmd), "submit %s\n", path);
                write(fifo_fd, cmd, strlen(cmd));
            } else {
                printf("Scheduler not available.\n");
            }
        } else if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            // send shutdown and print stats
            if (fifo_fd >= 0) {
                write(fifo_fd, "shutdown\n", strlen("shutdown\n"));
            }
            waitpid(sched, NULL, 0);
            char stats_path[256]; snprintf(stats_path, sizeof(stats_path), STATS_FILE_TEMPLATE, uid);
            FILE *f = fopen(stats_path, "r");
            if (!f) { perror("fopen stats"); break; }
            char buf[1024];
            while (fgets(buf, sizeof(buf), f)) {
                printf("%s", buf);
            }
            fclose(f);
            unlink(stats_path);
            break;
        } else {
            printf("Unknown command.\n");
        }
    }
    if (fifo_fd >= 0) close(fifo_fd);
    return 0;
}
