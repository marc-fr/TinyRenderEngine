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

# Opus (Optionnal)

OPUS_SRCDIR  := $(ROOTDIR)/../opus
OPUS_DEFINE  := -DTRE_WITH_OPUS
OPUS_INC     := -I$(OPUS_SRCDIR)/include
OPUS_LDFLAGS := $(OBJDIR)/libopus.a
OPUS_RULE    := opus

## Platform

CXX = emcc
CXXFLAGS = -O2
DEFINE = -DTRE_EMSCRIPTEN -DTRE_OPENGL_ES $(OPUS_DEFINE) -DTRE_PROFILE -DTRE_DEBUG -DTRE_PRINTS
INC = -I$(ROOTDIR)/include -I$(ROOTDIR)/glm $(OPUS_INC)
LDFLAGS = -sUSE_SDL=2 -sFULL_ES3=1 -sINITIAL_MEMORY=256Mb -sSTACK_SIZE=1Mb -sWASM=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 $(OPUS_LDFLAGS)

## Rules

default : all

all : $(OBJDIR) $(OPUS_RULE) testBasic.html testAudioMixer.html testTextureCompression.html

clean :
	@rm -f $(OBJDIR)/*.o testBasic.* testAudioMixer.* testTextureCompression.*

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

## Rule for Opus (get sources from https://github.com/xiph/opus)

opus : $(OBJDIR)/libopus.a

$(OBJDIR)/libopus.a :
	cd $(OPUS_SRCDIR); emconfigure ./configure --disable-extra-programs --disable-doc --disable-intrinsics --disable-stack-protector
	cd $(OPUS_SRCDIR); emmake make;
	rm $(OPUS_SRCDIR)/*.wasm
	cp $(OPUS_SRCDIR)/.libs/libopus.a $(OBJDIR)/libopus.a
