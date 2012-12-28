CFLAGS = -Wall

all: ttycat

clean:
	rm -f *.o ttycat

debug: CFLAGS += -DDEBUG -g
debug: ttycat

ttycat: ttycat.o args.o
