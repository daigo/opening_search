OSL_HOME = ../..
-include makefile.local
-include $(OSL_HOME)/makefile.local
include $(OSL_HOME)/makefile.conf

ifdef PROFILE
RELEASE = true
endif

#
all:
	$(MAKE) programs
programs: master client

ifdef PROFILE
PROF = $(PROFILE_FLAGS)
LDFLAGS += $(PROF)
endif

LOADLIBES += -lhiredis -lglog \
	     -lboost_thread$(BOOST_POSTFIX_MT) -lboost_program_options$(BOOST_POSTFIX) -lpthread

## #gcc
OTHERFLAGS = -pipe $(CPUOPTION)
ifdef DEBUG
CXXOPTFLAGS = -O
OTHERFLAGS += -g -DDEBUG 
else
CXXOPTFLAGS = $(RELEASE_CXX_OPTFLAGS)
WARNING_FLAGS += $(WARN_INLINE)
ifdef RELEASE
OTHERFLAGS +=  -DNDEBUG
endif
ifndef PROF
CXXOPTFLAGS += -fomit-frame-pointer
endif
endif

CXXFLAGS = $(PROF) $(OTHERFLAGS) $(CXXOPTFLAGS) $(WARNING_FLAGS) $(INCLUDES)

PROGRAM_SRCS = master.cc client.cc
SRCS = $(PROGRAM_SRCS) 
OBJS = $(patsubst %.cc,%.o,$(SRCS))

CC = $(CXX)
PROGRAMS = $(PROGRAM_SRCS:.cc=)
OSL_HOME_FLAGS = -DOSL_HOME=\"$(shell dirname `dirname \`pwd\``)/osl\"

master: $(FILE_OSL_ALL) redis.o

client: $(FILE_OSL_ALL) redis.o

clean: light-clean
	-rm *.o $(PROGRAMS)
	-rm -f core

light-clean:
	-rm -rf .deps .objs .gch 

-include $(patsubst %.cc,.deps/%.cc.d,$(SRCS))

.PHONY: all clean light-clean
