#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stropts.h>
#include <termios.h>
#include <unistd.h>
#include <asm/ioctls.h>
#include <sys/select.h>
#include "args.h"
const int BSIZE = 512;
void print_usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options] <tty file>\n", argv0);
    fprintf(stderr, "---\n");
    fprintf(stderr, "Flags: (+|-)\n");
    fprintf(stderr, "inlcr, icrnl, igncr, iuclc, ixon, ixoff, ixany;\n");
    fprintf(stderr, "olcuc, onlcr, ocrnl; isig, icanon, iexten;\n");
    fprintf(stderr, "echo, echok, echoctl, tostop;\n");
    fprintf(stderr, "---\n");
    fprintf(stderr, "Character constants: (+)\n");
    fprintf(stderr, "veof, verase, vintr, vkill, vlnext, vquit, vsusp,\n");
    fprintf(stderr, "vstop, vstart. Prefix with - to disable.\n");
    fprintf(stderr, "example: -vstop -vstart +vlnext ^Q\n");
    fprintf(stderr, "---\n");
    fprintf(stderr, "-attach: make <tty file> controlling tty\n");
    fprintf(stderr, "---\n");
    fprintf(stderr, "Some flags might not be available on your system.\n");
}
void handler(int signum)
{
    fprintf(stderr, "---Signal received: ");
    switch (signum)
    {
        case SIGTERM:
            fprintf(stderr, "SIGTERM");
            break;
        case SIGINT:
            fprintf(stderr, "SIGINT");
            break;
        case SIGTSTP:
            fprintf(stderr, "SIGTSTP");
            break;
        case SIGPIPE:
            fprintf(stderr, "SIGPIPE");
            break;
        case SIGHUP:
            fprintf(stderr, "SIGHUP");
            break;
    }
    fprintf(stderr, "---\n");
}
void install_signal_handlers(void)
{
    // I'm not actually sure whether SIGPIPE ever gets sent. I guess we'll
    // find out.
    const int signals[5] = {SIGTERM, SIGINT, SIGTSTP, SIGPIPE, SIGHUP};
    int i;
    for (i = 0; i < 5; i++)
    {
        struct sigaction act;
        act.sa_handler = handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_RESTART;
        if (sigaction(signals[i], &act, NULL) < 0)
            perror("Warning: sigaction");
    }
}
// Echo input to output.
// This is easy.
void read_write_loop(void)
{
    char c;
    while ((c = getchar()) != EOF)
    {
        putchar(c);
        fflush(stdout);
    }
    exit(EXIT_SUCCESS);
}
void kill_parent(void)
{
    // Use SIGINT so the shell won't report "Terminated" or the like. I'm not
    // sure whether this even matters. bash only reports the exit status of the
    // *last* process in the pipeline. Not sure about other shells.
    kill(getppid(), SIGINT);
}
int main(int argc, char **argv)
{
    // parse arguments
    struct termios T;
    char *tty;
    int attach;
    if (parse_args(argc, argv, &T, &tty, &attach) < 0 || !tty)
    {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    // Attaching is annoying. If we set the opened tty to be our ctty, we will
    // no longer be able to read input. So we need to set up a pipeline where
    // the first process reads input and the second process attaches.
    if (attach)
    {
        int fds[2];
        pipe(fds);
        if (fork())
        {
            // we are the parent; so we write to the pipe
            close(fds[0]);
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
            read_write_loop();
        }
        else
        {
            // we are the child; so we read from the pipe; and attach
            close(fds[1]);
            dup2(fds[0], STDIN_FILENO);
            close(fds[0]);
            setsid();
            // Make sure control returns to the shell if the child dies.
            atexit(kill_parent);
        }
    }
    // This is the child.
    int fd = open(tty, O_RDWR | (attach ? 0 : O_NOCTTY));
    if (fd == -1)
    {
        perror(tty);
        exit(EXIT_FAILURE);
    }
    int attached = 0;
    if (attach)
    {
        int extra_fd;
        if (~(extra_fd = open("/dev/tty", O_RDWR)))
        {
            attached = 1;
            close(extra_fd);
        }
        else
            fprintf(stderr, "Warning: Failed to acquire %s as ctty\n", tty);
    }
    // Are we opening the master end?
    // If not, this will fail, but we don't care.
    grantpt(fd);
    // This is only relevant to /dev/ptmx, but we don't care.
    unlockpt(fd);
    char *pts_name = ptsname(fd);
    if (pts_name)
        fprintf(stderr, "Note: name of slave pty is %s\n", pts_name);
    // Set requested attributes.
    tcgetattr(fd, &T);
    // Hack: parse the args a second time.
    // This is ugly, but necessary as we need to know the filename before we
    // can apply any line settings.
    parse_args(argc, argv, &T, &tty, &attach);
    int ret = tcsetattr(fd, TCSANOW, &T);
    struct termios T_after;
    tcgetattr(fd, &T_after);
    // sizeof(struct termios) won't work properly in the memcmp, as it is too
    // large.
    size_t termios_size = 4*sizeof(tcflag_t) + NCCS*sizeof(cc_t);
    if (ret == -1 || memcmp(&T, &T_after, termios_size))
    {
        fprintf(stderr, "Warning: Some of the requested attributes could ");
        fprintf(stderr, "not be set.\n");
    }
    // If we successfully attached to the tty we opened, install signal
    // handlers so we can see what's going on.
    if (attached)
        install_signal_handlers();
    // Actually do crap
    int obuf_size = 0;
    int offset = 0;
    char ibuf[BSIZE];
    char obuf[BSIZE];
    for (;;)
    {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (obuf_size == offset)
            FD_SET(STDIN_FILENO, &rfds);
        FD_ZERO(&wfds);
        if (obuf_size > offset)
            FD_SET(fd, &wfds);
        if (select(fd + 1, &rfds, &wfds, NULL, NULL) < 0)
            // assume we were interrupted by a signal
            continue;
        if (FD_ISSET(fd, &rfds))
        {
            int x = read(fd, ibuf, BSIZE);
            if (x == 0) // end of file
                exit(EXIT_SUCCESS);
            if (x == -1) // probably a hangup, i.e., other side closed
                exit(EXIT_SUCCESS);
            write(1, ibuf, x);
        }
        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            offset = 0;
            obuf_size = read(STDIN_FILENO, obuf, BSIZE);
            if (!obuf_size) // end of stdin
                exit(EXIT_SUCCESS);
        }
        if (FD_ISSET(fd, &wfds))
        {
            // Write as much as we can from the buffer.
            int x = write(fd, obuf + offset, obuf_size - offset);
            // Adjust offset past amount actually written.
            offset += x;
        }
    }
}
