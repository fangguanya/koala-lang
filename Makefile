#####################################

HOSTOS := $(shell uname -s | tr '[:upper:]' '[:lower:]')

TOPDIR = $(shell pwd)

export	TOPDIR

include $(TOPDIR)/config.mk

######################################

KOALA_LIB_FILES = hash_table.c hash.c \
object.c nameobject.c tupleobject.c tableobject.c methodobject.c \
stringobject.c moduleobject.c structobject.c \
globalstate.c coroutine.c koala.c

KOALA_LIB = koala

KOALAC = koalac

KOALAC_FILES =

KOALA_FILES =

KOALA  = koala

######################################

all:

lib:
	@$(RM) lib*.a
	@$(CC) -c $(CFLAGS) $(KOALA_LIB_FILES)
	@$(AR) -r lib$(KOALA_LIB).a $(patsubst %.c, %.o, $(KOALA_LIB_FILES))
	@$(RM) *.o

test: lib
	@$(CC) $(CFLAGS) object_test.c -l$(KOALA_LIB) -L.
