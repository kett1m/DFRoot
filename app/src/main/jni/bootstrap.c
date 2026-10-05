#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BLKROSET   0x125d
#define KSUD       "/data/user_de/0/df.root/ksud"
#define PREFS_PATH "/data/user_de/0/df.root/shared_prefs/dfroot.xml"
#define SU_MGR_FILE "/data/user_de/0/df.root/su_manager"
#define SOFT_REBOOT_FILE "/data/user_de/0/df.root/soft_reboot"

/* Read a plain-text file into buf, stripping trailing newline. */
static int read_text_file(const char *path, char *buf, size_t buf_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
    return 0;
}

static int read_prefs(char *su_manager, size_t su_manager_size, int *soft_reboot)
{
    char xml_buf[4096] = {0};
    int have_xml = 0;

    /* Primary: plain-text file (written by ExploitRunner) */
    if (read_text_file(SU_MGR_FILE, su_manager, su_manager_size) != 0 || !su_manager[0]) {
        /* Fallback: legacy XML prefs */
        int fd = open(PREFS_PATH, O_RDONLY);
        if (fd < 0) return -1;
        int n = read(fd, xml_buf, sizeof(xml_buf) - 1);
        close(fd);
        if (n <= 0) return -1;
        xml_buf[n] = '\0';
        have_xml = 1;

        char *p = strstr(xml_buf, "name=\"su_manager\">");
        if (!p) return -1;
        p += strlen("name=\"su_manager\">");
        char *end = strchr(p, '<');
        if (!end) return -1;
        size_t len = end - p;
        if (len == 0 || len >= su_manager_size) return -1;
        memcpy(su_manager, p, len);
        su_manager[len] = '\0';
    }

    /* Soft reboot: plain-text file, fallback to XML */
    if (access(SOFT_REBOOT_FILE, F_OK) == 0) {
        *soft_reboot = 1;
    } else if (have_xml) {
        char *bp = strstr(xml_buf, "name=\"soft_reboot\"");
        if (bp) {
            char *bend = strchr(bp, '>');
            char *v    = strstr(bp, "value=\"true\"");
            *soft_reboot = v && bend && v < bend ? 1 : 0;
        } else {
            *soft_reboot = 0;
        }
    } else {
        *soft_reboot = 0;
    }

    return 0;
}

static void adopt_zygote_env(void)
{
    FILE *f = popen("pidof zygote64 zygote", "r");
    if (!f) return;
    int pid = 0;
    fscanf(f, "%d", &pid);
    pclose(f);
    if (!pid) return;

    char path[32];
    snprintf(path, sizeof(path), "/proc/%d/environ", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    static char buf[16384];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    buf[n] = '\0';
    for (char *p = buf, *end = buf + n; p < end; p += strlen(p) + 1)
        putenv(p);
}

static int should_ro(const char *name)
{
    size_t len = strlen(name);
    if (!strcmp(name, "super"))  return 1;
    if (!strcmp(name, "misc"))   return 1;
    if (!strcmp(name, "steady")) return 1;
    if (len >= 2 && name[len - 2] == '_' &&
        (name[len - 1] == 'a' || name[len - 1] == 'b'))
        return 1;
    return 0;
}

static void set_partitions_ro(void)
{
    DIR *dir = opendir("/dev/block/by-name");
    if (!dir)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (!should_ro(ent->d_name))
            continue;

        char path[128];
        snprintf(path, sizeof(path), "/dev/block/by-name/%s", ent->d_name);

        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISBLK(st.st_mode)) {
            int on = 1;
            ioctl(fd, BLKROSET, &on);
        }
        close(fd);
    }

    closedir(dir);
}

static int run(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void touch(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY, 0666);
    if (fd >= 0)
        close(fd);
}

int main(void)
{
    char su_manager[256];
    int soft_reboot;
    if (read_prefs(su_manager, sizeof(su_manager), &soft_reboot) != 0) {
        touch("/dev/dfm6");
        return 1;
    }
    touch("/dev/dfm1");

    adopt_zygote_env();
    touch("/dev/dfm2");

    set_partitions_ro();
    touch("/dev/dfm3");

    char **late_load;
    if (soft_reboot)
        late_load = (char *[]){ KSUD, "late-load", "--package-name", su_manager, "--soft-reboot", NULL };
    else
        late_load = (char *[]){ KSUD, "late-load", "--package-name", su_manager, NULL };
    if (run(late_load) == 0)
        touch("/dev/dfm4");
    else
        touch("/dev/dfm5");

    return 0;
}
