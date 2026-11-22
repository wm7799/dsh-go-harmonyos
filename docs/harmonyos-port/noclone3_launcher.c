/* Install a clone3(2)->ENOSYS seccomp rule in a disposable helper process,
 * then exec PRoot. The filter survives exec and is inherited by Node, so
 * glibc's pthread_create falls back to clone(2) without any LD_PRELOAD. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define ENOSYS 38

static int install_filter(void)
{
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 435, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };
    int rc;

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return -errno;

    rc = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
    if (rc == 0)
        return 1;

    rc = (int)syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
    if (rc == 0)
        return 2;

    return -errno;
}

int main(int argc, char **argv)
{
    int rc;
    if (argc < 2) {
        fprintf(stderr, "noclone3: missing command\n");
        return 127;
    }

    rc = install_filter();
    if (rc < 0) {
        fprintf(stderr, "noclone3: filter install failed: %d\n", rc);
        return 126;
    }
    fprintf(stderr, "noclone3: clone3 filter armed via %s\n", rc == 2 ? "seccomp" : "prctl");

    execv(argv[1], &argv[1]);
    fprintf(stderr, "noclone3: execv %s failed: %d\n", argv[1], errno);
    return 127;
}
