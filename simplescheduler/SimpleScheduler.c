#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define FIFO_TEMPLATE "/tmp/simplescheduler_fifo_%d"
#define STATS_FILE_TEMPLATE "/tmp/scheduler_stats_%d"
#define MAX_CMD 1024

typedef enum { JOB_READY, JOB_RUNNING, JOB_FINISHED, JOB_STOPPED } job_state_t;

typedef struct job {
    pid_t pid;
    char path[512];
    long arrival_tick;
    long completion_tick;
    long execution_slices;
    job_state_t state;
    struct job *next;
} job_t;

static job_t *ready_head = NULL, *ready_tail = NULL;
static job_t *running_jobs[256];
static int ncpu = 1;
static long tsl_ms = 200;
static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t sigchld_flag = 0;
static long current_tick = 0;
static int fifo_fd = -1;
static char fifo_path[256];
static int uid_cached = 0;

// add job to end of ready queue
static void enqueue_job(job_t *j) {
    j->next = NULL;
    if (!ready_tail) { 
        ready_head = ready_tail = j; 
    } else { 
        ready_tail->next = j; 
        ready_tail = j; 
    }
}

// remove and return next ready job from queue
static job_t* dequeue_ready() {
    if (!ready_head || ready_head->state != JOB_READY) {
        job_t *p = ready_head;
        while (p && p->state != JOB_READY) p = p->next;
        if (!p) return NULL;
        
        job_t *prev = NULL, *cur = ready_head;
        while (cur && cur != p) { prev = cur; cur = cur->next; }
        if (!cur) return NULL;
        
        if (!prev) {
            ready_head = cur->next;
            if (!ready_head) ready_tail = NULL;
        } else {
            prev->next = cur->next;
            if (!prev->next) ready_tail = prev;
        }
        cur->next = NULL;
        return cur;
    }
    
    job_t *j = ready_head;
    ready_head = j->next;
    if (!ready_head) ready_tail = NULL;
    j->next = NULL;
    return j;
}

// search for job by process id
static job_t* find_job(pid_t pid) {
    job_t *p = ready_head;
    while (p) { 
        if (p->pid == pid) return p; 
        p = p->next; 
    }
    return NULL;
}

// signal handler for child process events
static void sigchld_handler(int signo) {
    (void)signo;
    sigchld_flag = 1;
}

// collect status of terminated or stopped children
static void reap_children() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        job_t *j = find_job(pid);
        if (!j) {
            for (int i = 0; i < ncpu; i++) {
                if (running_jobs[i] && running_jobs[i]->pid == pid) {
                    j = running_jobs[i];
                    break;
                }
            }
        }
        
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (j && j->state != JOB_FINISHED) {
                j->state = JOB_FINISHED;
                j->completion_tick = current_tick;
                
                for (int i = 0; i < ncpu; i++) {
                    if (running_jobs[i] && running_jobs[i]->pid == pid) {
                        running_jobs[i] = NULL;
                        break;
                    }
                }
            }
        } else if (WIFSTOPPED(status)) {
            if (j) {
                j->state = JOB_STOPPED;
            }
        }
    }
    sigchld_flag = 0;
}

// fork and execute new job process
static int create_job(const char *path) {
    pid_t pid = fork();
    if (pid < 0) { 
        perror("fork"); 
        return -1; 
    }
    
    if (pid == 0) {
        execl(path, path, (char*)NULL);
        perror("execl");
        _exit(127);
    }
    
    usleep(10000);
    
    job_t *j = calloc(1, sizeof(job_t));
    if (!j) { 
        perror("calloc"); 
        return -1; 
    }
    
    j->pid = pid;
    strncpy(j->path, path, sizeof(j->path)-1);
    j->arrival_tick = current_tick;
    j->execution_slices = 0;
    j->state = JOB_READY;
    j->completion_tick = 0;
    
    enqueue_job(j);
    return 0;
}

// read and process commands from fifo
static void handle_fifo_readable() {
    char buf[MAX_CMD+1];
    ssize_t n = read(fifo_fd, buf, MAX_CMD);
    if (n <= 0) return;
    buf[n] = '\0';
    
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "submit ", 7) == 0) {
            char *path = line + 7;
            while (*path == ' ') path++;
            if (path[0] != '\0') {
                create_job(path);
            }
        } else if (strcmp(line, "shutdown") == 0) {
            shutdown_requested = 1;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

// pause all currently running jobs
static void stop_running_jobs() {
    for (int i = 0; i < ncpu; i++) {
        if (running_jobs[i] && running_jobs[i]->state == JOB_RUNNING) {
            pid_t pid = running_jobs[i]->pid;
            if (kill(pid, 0) == 0) {
                kill(pid, SIGSTOP);
                running_jobs[i]->state = JOB_READY;
                enqueue_job(running_jobs[i]);
            }
            running_jobs[i] = NULL;
        }
    }
}

// schedule and run next batch of ready jobs
static void start_next_batch() {
    for (int i = 0; i < ncpu; i++) {
        if (running_jobs[i] != NULL) continue;
        
        job_t *j = dequeue_ready();
        if (!j) break;
        
        if (kill(j->pid, 0) == 0) {
            kill(j->pid, SIGCONT);
            j->state = JOB_RUNNING;
            j->execution_slices++;
            running_jobs[i] = j;
        } else {
            j->state = JOB_FINISHED;
            j->completion_tick = current_tick;
            enqueue_job(j);
        }
    }
}

// write job statistics to file before exit
static void write_stats_and_exit() {
    char stats_path[256];
    snprintf(stats_path, sizeof(stats_path), STATS_FILE_TEMPLATE, uid_cached);
    FILE *f = fopen(stats_path, "w");
    if (!f) {
        perror("fopen stats");
        return;
    }
    
    fprintf(f, "NAME PID COMPLETION_TIME WAIT_TIME\n");
    job_t *p = ready_head;
    while (p) {
        if (p->state == JOB_FINISHED) {
            long completion_time = p->completion_tick - p->arrival_tick;
            if (completion_time < 1) completion_time = 1;
            long wait_time = completion_time - p->execution_slices;
            fprintf(f, "%s %d %ld %ld\n", p->path, p->pid, completion_time, wait_time);
        }
        p = p->next;
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE_ms>\n", argv[0]);
        return 1;
    }
    
    ncpu = atoi(argv[1]);
    tsl_ms = atol(argv[2]);
    if (ncpu < 1) ncpu = 1;
    if (tsl_ms < 1) tsl_ms = 200;
    
    for (int i = 0; i < 256; i++) {
        running_jobs[i] = NULL;
    }
    
    uid_cached = getuid();
    snprintf(fifo_path, sizeof(fifo_path), FIFO_TEMPLATE, uid_cached);
    
    unlink(fifo_path);
    if (mkfifo(fifo_path, 0600) < 0) {
        perror("mkfifo");
        return 1;
    }
    
    fifo_fd = open(fifo_path, O_RDONLY | O_NONBLOCK);
    int fifo_wdummy = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fifo_fd < 0) { 
        perror("open fifo"); 
        unlink(fifo_path); 
        return 1; 
    }
    
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    
    // main scheduling loop
    while (1) {
        if (shutdown_requested) {
            int unfinished = 0;
            job_t *p = ready_head;
            while (p) { 
                if (p->state != JOB_FINISHED) { 
                    unfinished = 1; 
                    break; 
                } 
                p = p->next; 
            }
            for (int i = 0; i < ncpu; i++) {
                if (running_jobs[i] && running_jobs[i]->state != JOB_FINISHED) {
                    unfinished = 1;
                    break;
                }
            }
            if (!unfinished) break;
        }
        
        start_next_batch();
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fifo_fd, &readfds);
        struct timespec timeout;
        timeout.tv_sec = tsl_ms / 1000;
        timeout.tv_nsec = (tsl_ms % 1000) * 1000000L;
        
        int ret = pselect(fifo_fd + 1, &readfds, NULL, NULL, &timeout, NULL);
        if (ret > 0 && FD_ISSET(fifo_fd, &readfds)) {
            handle_fifo_readable();
        }
        
        current_tick++;
        stop_running_jobs();
        
        if (sigchld_flag) reap_children();
    }
    
    write_stats_and_exit();
    close(fifo_fd);
    close(fifo_wdummy);
    unlink(fifo_path);
    return 0;
}
