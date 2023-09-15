# glook Makefile

NAME=glook
SRC=$(NAME).c

CC=gcc
STD=-std=c89
OPT=-O2
LIBS=-lglfw
WFLAGS=-Wall -Wextra -pedantic

OS=$(shell uname -s)
ifeq ($(OS),Darwin)
	LIBS+=-framework OpenGL
else
	LIBS=-lGL -lGLEW
endif

CFLAGS=$(STD) $(OPT) $(WFLAGS)

$(NAME): $(LPATHS) $(SRC)
	$(CC) $(SRC) -o $@ $(CFLAGS) $(LIBS)

clean:
	rm -r $(NAME)

