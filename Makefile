#
# Makefile to build against "emscripten"
#
# Note: to configurate emsdk, do:
# >$ <path>/emsdk activate latest
# >$ source <path>/emsdk_env.sh

## Definitions

ROOTDIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
OBJDIR  := ./obj

CORE_SRCDIR := $(ROOTDIR)/src
CORE_SRC    := $(wildcard $(CORE_SRCDIR)/*.cpp)
CORE_OBJ    := $(patsubst $(CORE_SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(CORE_SRC))

## Dependencies

# Note: On a fresh install of emsdk, you might temporary add "-sUSE_SDL=2" to
#       the variable "DEFINE" so that emsdk will fetch the SDL2 lib.

## Platform

CXX = emcc
CXXFLAGS = -O2 -g
DEFINE = -DTRE_EMSCRIPTEN -DTRE_OPENGL_ES -DTRE_DEBUG -DTRE_PRINTS
INC = -I$(ROOTDIR)/include -I$(ROOTDIR)/glm
LDFLAGS = -sUSE_SDL=2 -sFULL_ES3=1 -sINITIAL_MEMORY=128Mb -sWASM=0

## Rules

default : all

all : $(OBJDIR) testBasic.html testAudioMixer.html

clean :
	@rm -f $(OBJDIR)/*.o testBasic.* testAudioMixer.*

#-

$(OBJDIR) :
	mkdir $(OBJDIR)

$(OBJDIR)/%.o : $(CORE_SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(DEFINE) $(INC) -c -o $@ $<

$(OBJDIR)/%.o : $(ROOTDIR)/test/%.cpp
	$(CXX) $(CXXFLAGS) $(DEFINE) $(INC) -c -o $@ $< -DTESTIMPORTPATH=\"resources-dir/\"

%.html : $(CORE_OBJ) $(OBJDIR)/%.o
	$(CXX) -o $@ $^ $(LDFLAGS) --preload-file ../test/resources@resources-dir/resources/

#-

.SECONDARY: $(CORE_OBJ)
