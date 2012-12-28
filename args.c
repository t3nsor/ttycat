#include "args.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
struct flag
{
    char *name;
    tcflag_t value;
};
struct cc
{
    char *name;
    size_t index;
};
const struct flag iflags[] = {
    {.name = "inlcr", .value = INLCR},
    {.name = "icrnl", .value = ICRNL},
    {.name = "igncr", .value = IGNCR},
#ifdef IUCLC
    {.name = "iuclc", .value = IUCLC},
#endif
    {.name = "ixon",  .value = IXON},
    {.name = "ixoff", .value = IXOFF},
    {.name = "ixoff", .value = IXOFF},
    {.name = NULL, .value = 0}
};
const struct flag oflags[] = {
#ifdef OLCUC
    {.name = "olcuc", .value = OLCUC},
#endif
    {.name = "onlcr", .value = ONLCR},
    {.name = "ocrnl", .value = OCRNL},
    {.name = NULL, .value = 0}
};
const struct flag lflags[] = {
    {.name = "isig",    .value = ISIG},
    {.name = "icanon",  .value = ICANON},
    {.name = "iexten",  .value = IEXTEN},
    {.name = "echo",    .value = ECHO},
    {.name = "echok",   .value = ECHOK},
#ifdef ECHOCTL
    {.name = "echoctl", .value = ECHOCTL},
#endif
    {.name = "tostop",  .value = TOSTOP},
    {.name = NULL, .value = 0}
};
const struct cc ccs[] = {
    {.name = "veof",   .index = VEOF},
    {.name = "verase", .index = VERASE},
    {.name = "vintr",  .index = VINTR},
    {.name = "vkill",  .index = VKILL},
#ifdef VLNEXT
    {.name = "vlnext", .index = VLNEXT},
#endif
    {.name = "vquit",  .index = VQUIT},
    {.name = "vsusp",  .index = VSUSP},
    {.name = "vstop",  .index = VSTOP},
    {.name = "vstart", .index = VSTART},
    {.name = NULL, .index = 0}
};
int parse_char(const char *str)
{
    // Command line arguments can't be empty nor can they contain \0
    int meta = 0;
    int control = 0;
    int chr = 0;
    if (strstr(str, "M-") == str)
    {
        meta = 1;
        str += 2;
        if (!str[0]) return -1; // M- alone is nonsensical
    }
    if (strstr(str, "^") == str)
    {
        str++;
        if (str[0])
            if (str[1])
                return -1; // too many characters
            else
            {
                control = 1;
                chr = str[0];
            }
        else
            chr = '^';
    }
    else if (str[1]) // too many characters
        return -1;
    else
        chr = str[0];
    if (control)
    {
        if (chr == '?')
            chr = '\177';
        else
        {
            chr -= 0x40; //^A -> 0x01, and so on
            if (chr < 0 || chr >= 32) // invalid ^-sequence
                return -1; 
        }
    }
    if (meta)
    {
        if (chr >= 0x80)
            return -1;
        else
            chr += 0x80;
    }
    return chr;
}
int parse_args(int argc, char **argv, struct termios *T, char **tty,
               int *attach)
{
    *attach = 0;
    *tty = NULL;
    int i;
    int end_of_options = 0;
    for (i = 1; i < argc; i++)
        if (end_of_options) // don't process any more flags
            if (*tty) // too many files given
                return -1;
            else
                *tty = argv[i];
        else if (!strcmp(argv[i], "--")) // --: end of options
            end_of_options = 1;
        else if (strspn(argv[i], "+-") > 0) // starts with - or +; flag
        {
            char *flagname = argv[i] + 1;
            int j;
            size_t offset = -1;
            int value = 0;
            for (j = 0; iflags[j].name; j++)
                if (!strcmp(flagname, iflags[j].name))
                {
                    offset = offsetof(struct termios, c_iflag);
                    value = iflags[j].value;
                    break;
                }
            for (j = 0; oflags[j].name; j++)
                if (!strcmp(flagname, oflags[j].name))
                {
                    offset = offsetof(struct termios, c_oflag);
                    value = oflags[j].value;
                    break;
                }
            // control flags go here
            for (j = 0; lflags[j].name; j++)
                if (!strcmp(flagname, lflags[j].name))
                {
                    offset = offsetof(struct termios, c_lflag);
                    value = lflags[j].value;
                    break;
                }
            if (~offset)
            {
                tcflag_t *field = (tcflag_t*)((char*)T + offset);
                if (argv[i][0] == '+')
                    *field |= value;
                else // -
                    *field &= ~value;
                continue;
            }
            // otherwise: must be character constant
            int success = 0;
            for (j = 0; ccs[j].name; j++)
                if (!strcmp(flagname, ccs[j].name))
                {
                    if (argv[i][0] == '-') // disable this character
                        T->c_cc[ccs[j].index] = _POSIX_VDISABLE;
                    else // use next argument
                        if (i + 1 == argc) // no more arguments
                            return -1;
                        else
                        {
                            int c = parse_char(argv[++i]);
                            if (~c)
                                T->c_cc[ccs[j].index] = c;
                            else
                            {
                                fprintf(stderr, "Invalid character `%s'\n",
                                                argv[i]);
                                return -1;
                            }
                        }
                    success = 1;
                }
            if (success) continue; 
            if (!strcmp(argv[i], "-attach"))
                *attach = 1;
            else
            {
                fprintf(stderr, "Unrecognized option %s\n", argv[i]);
                return -1;
            }
        }
        else // filename argument
            if (*tty) // too many files given
                return -1;
            else
                *tty = argv[i];
    return 0;
}
