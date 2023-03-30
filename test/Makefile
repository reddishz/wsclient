MODNAME = demo
 
objects := $(patsubst %.c,%.o,$(wildcard *.c))

MODOBJ = $(objects)

XMODCFLAGS = -Wall -Werror --std=gnu99 
MODCFLAGS = -Wall -Wextra -pedantic --std=gnu99

MODLDFLAGS = -L ../wwsocket/lib -lpthread -luuid -lwwsocket   -lm  -Wl,-R -Wl,/usr/local/lib64/aliyun -lssl -lcrypto 

INCLUDE= -I. -I./include  -I../wwsocket/include

 
CC = gcc

CFLAGS = -fPIC -g -ggdb  $(MODCFLAGS) $(INCLUDE) 
LDFLAGS =  $(MODLDFLAGS) 

	
.PHONY: all Debug Release
all: $(MODNAME)
  
$(MODNAME): $(MODOBJ)
#	@$(CC) -shared -o $@ $(MODOBJ) $(LDFLAGS)
	@$(CC) -o $@ $(MODOBJ) $(LDFLAGS)

 
.c.o: $<
	@$(CC) $(CFLAGS) -o $@ -c $<
 
.PHONY: clean

clean: 
	rm -f $(MODNAME) $(MODOBJ)
