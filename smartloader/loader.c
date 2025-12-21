#include "loader.h"
#include <signal.h>
#include <ucontext.h>

Elf32_Ehdr *ehdr; 
Elf32_Phdr *phdr; 
int fd;  
ssize_t bytes_read;         

#define pg_sz 4096

int pg_faults = 0;
int pg_allocs = 0;
float frag = 0;

/*
 * release memory and other cleanups
 */
void loader_cleanup() {
    if (phdr) {
        free(phdr);
        phdr = NULL;
    }
    if (ehdr) {
        free(ehdr);
        ehdr = NULL;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void segv_handler(int sig, siginfo_t *info, void *context){
    void *flt_addr = info->si_addr;
    pg_faults++;

    for (int i = 0 ; i < ehdr->e_phnum ; i++){
        if (phdr[i].p_type == PT_LOAD){
            uint32_t start = phdr[i].p_vaddr;
            uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if ((uint32_t) flt_addr < end && (uint32_t) flt_addr >= start){
                uintptr_t page_start = ((uintptr_t)flt_addr / pg_sz) * pg_sz;
                size_t offset = page_start - phdr[i].p_vaddr;
                size_t bytes_to_copy = pg_sz;
                if (offset + bytes_to_copy > phdr[i].p_filesz)
                    bytes_to_copy = (phdr[i].p_filesz > offset) ? (phdr[i].p_filesz - offset) : 0;

                void *map_addr = mmap((void *)page_start, pg_sz, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
                if (map_addr == MAP_FAILED) {
                    perror("Error mapping memory\n");
                    exit(1);
                }

                if (bytes_to_copy > 0) {
                    if (lseek(fd, phdr[i].p_offset + offset, SEEK_SET) < 0) {
                    perror("lseek failes\n");
                    exit(1);
                    }
                    read(fd, map_addr, bytes_to_copy);
                }
                uint32_t pg_ind = (page_start - phdr[i].p_vaddr) / pg_sz;
                uint32_t filled_bit = phdr[i].p_memsz - pg_sz * pg_ind;
                filled_bit = (pg_sz * (pg_ind + 1) <= phdr[i].p_memsz) ? pg_sz : filled_bit;
                pg_allocs++;
                frag += pg_sz - filled_bit;
                return;
            }
        }
    }
    fprintf(stderr, "No memory access at %p\n", flt_addr);
    exit(1);
}


void load_and_run_elf(char **exe) {
    fd = open(exe[1], O_RDONLY);
    if (fd < 0) {
        perror("Open Failed");
        exit(1);
    }

    ehdr = malloc(sizeof(Elf32_Ehdr));

    bytes_read = read(fd, ehdr, sizeof(Elf32_Ehdr));
    if (bytes_read < 0) {
        perror("Couldn't read the file");
        loader_cleanup();
        exit(1);
    }
    
    phdr = malloc(sizeof(Elf32_Phdr) * ehdr->e_phnum);

    if (lseek(fd, ehdr->e_phoff, SEEK_SET) < 0) {
        perror("lseek failed");
        loader_cleanup();
        exit(1);
    }

    bytes_read = read(fd, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum);
    if (bytes_read < 0) {
        perror("Couldn't read the file");
        loader_cleanup();
        exit(1);
    }

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

    // directly jump to entrypoint (no pre loading of segments)
    int (*_start)(void) = (int (*)(void))(ehdr->e_entry);
    int result = _start();

    printf("User _start return value = %d\n", result);
    printf("Total number of page faults = %d\n", pg_faults);
    printf("Total number of page allocations carried out = %d\n", pg_allocs);
    printf("Total amount of internal fragmentation = %.2f KB\n", frag / 1024.0);

}

int main(int argc, char** argv) {
    if(argc != 2) {
        printf("Usage: %s <ELF Executable> \n",argv[0]);
        exit(1);
    }
    // 1. carry out necessary checks on the input ELF file
    // 2. passing it to the loader for carrying out the loading/execution
    load_and_run_elf(argv);
    // 3. invoke the cleanup routine inside the loader  
    loader_cleanup();
    return 0;
}