/* android_uffd_probe.c - TEST (0.5.2): can this app use userfaultfd write-protection?
 *
 * The upload ring copies every vertex range again each frame (13 MB a frame in Cascade Kingdom on the S24) because nothing
 * says whether the game wrote that memory since the last frame (PROGRESS.md, "THE BIGGEST LEAD LEFT"). userfaultfd's
 * write-protect mode (UFFD_FEATURE_PAGEFAULT_FLAG_WP, Linux 5.7+, anonymous memory) would report the first write to a
 * watched page without SIGSEGV, and without making the kernel's own writes into the page (file reads) fail. Android's
 * SELinux policy may refuse the syscall to apps. This only asks, once at start, and logs the answer. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/userfaultfd.h>

void android_uffd_probe(void) {
#ifdef __NR_userfaultfd
    int fd = (int)syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    int e1 = errno;
    if (fd < 0) { fd = (int)syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK); e1 = errno; }
    if (fd < 0) { printf("android_uffd: userfaultfd refused (%s): no write-protect tracking for buffers%c", strerror(e1), 10); fflush(stdout); return; }
    struct uffdio_api api;
    memset(&api, 0, sizeof api);
    api.api = UFFD_API;
    api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
    int r = ioctl(fd, UFFDIO_API, &api);
    int e2 = errno;
    if (r != 0) { printf("android_uffd: userfaultfd opened, UFFDIO_API with write-protect refused (%s)%c", strerror(e2), 10); close(fd); fflush(stdout); return; }
    /* register one anonymous page for write-protect and protect it */
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok_reg = 0, ok_wp = 0, e3 = 0;
    if (p != MAP_FAILED) {
        ((volatile char *)p)[0] = 1;              /* populate it */
        struct uffdio_register reg;
        memset(&reg, 0, sizeof reg);
        reg.range.start = (unsigned long)p; reg.range.len = 4096; reg.mode = UFFDIO_REGISTER_MODE_WP;
        ok_reg = ioctl(fd, UFFDIO_REGISTER, &reg) == 0;
        if (!ok_reg) e3 = errno;
        if (ok_reg) {
            struct uffdio_writeprotect wp;
            memset(&wp, 0, sizeof wp);
            wp.range.start = (unsigned long)p; wp.range.len = 4096; wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
            ok_wp = ioctl(fd, UFFDIO_WRITEPROTECT, &wp) == 0;
            if (!ok_wp) e3 = errno;
            if (ok_wp) { wp.mode = 0; ioctl(fd, UFFDIO_WRITEPROTECT, &wp); }      /* unprotect again before anything writes */
        }
        munmap(p, 4096);
    }
    printf("android_uffd: userfaultfd available, API 0x%llx, features 0x%llx (write-protect %s); register %s, protect %s%s%s%c",
           (unsigned long long)api.api, (unsigned long long)api.features, (api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) ? "yes" : "no",
           ok_reg ? "ok" : "refused", ok_wp ? "ok" : "refused", e3 ? ": " : "", e3 ? strerror(e3) : "", 10);
    fflush(stdout);
    close(fd);
#else
    printf("android_uffd: no userfaultfd syscall number in this build%c", 10);
#endif
}
