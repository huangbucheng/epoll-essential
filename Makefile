#
# Server Makefile
#
#eg. make DEBUG=1
#eg. make 
# Compiler settings
CC      = g++

CFLAGS  += -Wall 
ifndef DEBUG
ADDITIONAL_CFLAGS  ?= -O2
else
ADDITIONAL_CFLAGS  ?= -g
endif

CFLAGS += ${ADDITIONAL_CFLAGS}

INCFLAGS ?= -I ./iniparser/src -I ./zlog/src/
LINKFLAGS = -lpthread ./iniparser/libiniparser.a -lzlog

SHLD = ${CC} ${CFLAGS} 

TARGET ?= tcp_server

RM      ?= rm -f


# Implicit rules

SUFFIXES = .o .c .h .a .so .sl

COMPILE	?= $(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) $(INCFLAGS) -c 

ifndef V
QUIET_CC	= @echo "CC	$@";
QUIET_LINK	= @echo "LINK	$@";
endif

.c.o:
	$(QUIET_CC)$(COMPILE) $(OUTPUT_OPTION) $<


SRCS = src/server.c \
	   src/conn.c \
	   src/net.c \
	   src/f_epoll.c \
	   src/common.c

OBJS = $(SRCS:.c=.o)


default:	$(TARGET)

$(TARGET):	$(OBJS)
	$(QUIET_LINK)$(SHLD) -o $(TARGET) $(OBJS) $(LINKFLAGS) 

clean:
	$(RM) $(OBJS) $(TARGET)

