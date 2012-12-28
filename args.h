#ifndef ARGS_H
#define ARGS_H
#include <termios.h>
int parse_args(int argc, char **argv, struct termios *T, char **tty,
               int *attach);
#endif
