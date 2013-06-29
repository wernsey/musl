CC=gcc

ifeq ($(BUILD),debug)
# Debug mode: Unoptimized and with debuging symbols
 CFLAGS = -Wall -O0 -g -DTEST
#CFLAGS = -Wall -O0 -g
LFLAGS =
else
# Release mode: Optimized and stripped of debugging info
CFLAGS = -Wall -Os -DNDEBUG
LFLAGS = -s -fno-exceptions
endif

SOURCES=main.c musl.c
OBJECTS=$(SOURCES:.c=.o)

# If your system does not have POSIX regular expressions
# (regcomp(), regexec(), etc.) you can remove this line
# to disable the REGEX() built-in function
#CFLAGS += -DWITH_REGEX

all: musl manual.txt

debug:
	make "BUILD=debug"

musl: $(OBJECTS)
	$(CC) -o $@ $(LFLAGS) $(OBJECTS) 
		
.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

musl.o : musl.h

main.o : musl.h

manual.txt: doc.awk musl.c main.c
	awk -f $^ > $@

.PHONY : clean

clean:
	-rm -rf musl musl.exe
	-rm -rf *.o
	-rm -rf *~ *.tmp
	-rm -rf manual.txt
