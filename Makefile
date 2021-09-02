# glook makefile

STD=-std=c99
WFLAGS=-Wall -Wextra
OPT=-O2
IDIR=-Iglee
LIBS=glee
CC=gcc
NAME=glook
SRC=*.c

CFLAGS=$(STD) $(WFLAGS) $(OPT) $(IDIR)
OS=$(shell uname -s)

LDIR=lib
LSTATIC=$(patsubst %,lib%.a,$(LIBS))
LPATHS=$(patsubst %,$(LDIR)/%,$(LSTATIC))
LFLAGS=$(patsubst %,-L%,$(LDIR))
LFLAGS += $(patsubst %,-l%,$(LIBS))
LFLAGS += -lglfw

ifeq ($(OS),Darwin)
	OSFLAGS=-framework OpenGL -mmacos-version-min=10.9
else
	OSFLAGS=-lGL -lGLEW
endif

$(NAME): $(LPATHS) $(SRC)
	$(CC) -o $@ $(SRC) $(CFLAGS) $(LFLAGS) $(OSFLAGS)

$(LPATHS): $(LDIR) $(LSTATIC)
	mv *.a $(LDIR)/

$(LDIR): 
	mkdir $@

$(LDIR)%.a: %
	cd $^ && make && mv $@ ../

clean:
	rm -r $(LDIR)
	
install: $(NAME)
	sudo mv $^ /usr/local/bin/
