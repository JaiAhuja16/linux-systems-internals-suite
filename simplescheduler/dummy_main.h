int dummy_main(int argc, char **argv);
int main(int argc, char **argv) {
    // Extra code: ensure the process stops itself until scheduler resumes it
    #include <signal.h>
    raise(SIGSTOP);
    int ret = dummy_main(argc, argv);
    return ret;
}
#define main dummy_main