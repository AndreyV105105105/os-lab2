CC     = gcc
CFLAGS = -Wall -Wextra -O2

ifeq ($(OS),Windows_NT)
    RM = del /Q /F
else
    RM = rm -f
endif

all: parent.exe child.exe

parent.exe: parent.o common.o
	$(CC) $(CFLAGS) -o $@ $^

child.exe: child.o common.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-$(RM) *.o *.exe

.PHONY: all clean
