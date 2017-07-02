PARALLELLA_ID = ${NODE_ID}
NUM_PARALLELLAS = ${NUM_NODES}
CFLAGS := -O3 -DHOST_INTERPRETER -Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes -std=c99 -I ../interpreter -D NID=$(PARALLELLA_ID) -D NN=$(NUM_PARALLELLAS)
OBJECTS := lexer.o parser.o main.o memorymanager.o byteassembler.o stack.o misc.o configuration.o ../interpreter/interpreter.o host-functions.o python_interoperability.o

LIBS=-lm -lpthread

ifeq ($(STANDALONE),1)
CFLAGS+= -DHOST_STANDALONE
else
CFLAGS+= -I../ -I ${EPIPHANY_HOME}/tools/host/include -D__HOST__ -Dasm=__asm__ -Drestrict=
OBJECTS+=device-support.o
CC=mpicc
LDFLAGS=-L ${EPIPHANY_HOME}/tools/host/lib
LIBS+=-le-hal -lrt
ifneq (,$(wildcard ${EPIPHANY_HOME}/tools/host/lib/libe-loader.so))
    LIBS+=-le-loader
endif
endif

YFLAGS := -d
LFLAGS :=

epython: $(OBJECTS)
	$(CC) $(LDFLAGS) -o epython-host $(OBJECTS) $(LIBS)

full: lexer parser epython

.PHONE: check

%.o : %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

lexer:
	$(LEX) $(LFLAGS) -o lexer.c epython.l

parser:
	$(YACC) $(YFLAGS) -o parser.c epython.y

clean:
	$(RM) -f -v $(bins) *.yy.[ch] *.tab.[ch] *.o *.d *.output

-include *.d
