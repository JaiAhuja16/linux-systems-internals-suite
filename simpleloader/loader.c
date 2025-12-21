#include "loader.h"

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;
ssize_t bytes_read;

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

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            void *virtual_mem = mmap((void *)phdr[i].p_vaddr, phdr[i].p_memsz, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (virtual_mem == MAP_FAILED) {
                printf("Error mapping memory\n");
                exit(1);
            }
            if (lseek(fd, phdr[i].p_offset, SEEK_SET) < 0) {
                perror("lseek failed");
                loader_cleanup();
                exit(1);
            }
            bytes_read = read(fd, virtual_mem, phdr[i].p_filesz);
            if (bytes_read < 0) {
                printf("Couldn't read the memory\n");
                munmap(virtual_mem, phdr[i].p_memsz);
                exit(1);
            }
        }
    }

    int (*_start)(void) = (int (*)(void))(ehdr->e_entry);
    int result = _start();
    printf("User _start return value = %d\n", result);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <ELF Executable> \n", argv[0]);
        exit(1);
    }

    // 1. carry out necessary checks on the input ELF file
    // 2. passing it to the loader for carrying out the loading/execution
    load_and_run_elf(argv);

    // 3. invoke the cleanup routine inside the loader
    loader_cleanup();

    return 0;
}
