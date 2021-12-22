CC=gcc
CFLAGS=-I.
RM = rm -f
RANLIB = ranlib
AR = ar rcul

SLP_SRCS = slptclfe.c slpgetln.o
SLP_OBJS = slptclfe.o slpgetln.o

.c.o:
		$(RM) $@
		$(CC) -c $(CFLAGS) $^

all:: libslp.a

libslp.a:	$(SLP_OBJS)
			$(RM) $@
			$(AR) $@ $(SLP_OBJS)
			$(RANLIB) $@

clean:
			rm -f *.a *.o core
