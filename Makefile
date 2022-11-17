MODNAME = lib/libwwsocket.a
 
objects := $(patsubst %.c,%.o,$(wildcard *.c))

MODOBJ = $(objects) 

MODCFLAGS = -Wall -Wextra -pedantic --std=gnu99


INCLUDE= -I. -I./include 

 
CC = gcc

CFLAGS = -fPIC -g -ggdb  $(MODCFLAGS) $(INCLUDE) 

ifdef debug
	CFLAGS += -g -ggdb  -DDEBUG 
endif

.PHONY: all Debug Release
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	ar rcs $(MODNAME) $(MODOBJ)
	ranlib $@

.c.o: $<
	@$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean

clean: 
	rm -f $(MODNAME) $(MODOBJ)

dist: clean all