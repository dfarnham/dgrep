RM           = rm -f
CC           = gcc
CFLAGS       = -I.
EXTRA_CFLAGS = -O2 -Wall
SRCS         = dgrep.c
OBJS         = $(SRCS:.c=.o)
EXE          = dgrep

all: $(EXE)

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(OBJS) $(LIBS) -o $(EXE)

$(OBJS): $(SRCS)
	$(CC) -c $(CFLAGS) $(EXTRA_CFLAGS) $(SRCS)

test: spotless all
	@ECHO "-~-~-~-~-~-~-~-~TEST~-~-~-~-~-~-~-~-~-~-"

clean:
	$(RM) $(OBJS)

spotless: clean
	$(RM) $(EXE)

