#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
const int BSIZE = 512;
int main(int argc, char** argv)
{
    if (argc == 2)
    {
        int fd = open(argv[1], O_RDWR);
        if (fd == -1)
        {
            perror(NULL);
            exit(1);
        }
//        struct termios T;
//        tcgetattr(fd, &T);
//        T.c_lflag &= ~ECHO;
//        tcsetattr(fd, TCSANOW, &T);
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
                FD_SET(0, &rfds);
            FD_ZERO(&wfds);
            if (obuf_size > offset)
                FD_SET(fd, &wfds);
            select(fd + 1, &rfds, &wfds, NULL, NULL);
            if (FD_ISSET(fd, &rfds))
            {
                int x = read(fd, ibuf, BSIZE);
                if (x == 0) // end of file
                    exit(0);
                if (x == -1) // slave end hung up
                    exit(0);
                write(1, ibuf, x);
            }
            if (FD_ISSET(0, &rfds))
            {
                offset = 0;
                obuf_size = read(0, obuf, BSIZE);
                if (!obuf_size) // end of stdin
                    exit(0);
            }
            if (FD_ISSET(fd, &wfds))
            {
                int x = write(fd, obuf + offset, obuf_size - offset);
                offset += x;
            }
        }
    }
    else
    {
        fprintf(stderr, "usage: %s <tty file>\n", argv[0]);
        exit(1);
    }
}
