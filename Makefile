# Makefile2
CC ?= gcc
FLAGS +=
CFLAGS ?= -fPIC -Wall -O2
OS = $(shell uname -s | tr '[A-Z]' '[a-z]')

ifeq ("$(OS)", "darwin")
JAVA_HOME ?= $(shell /usr/libexec/java_home)
JAVA_HEADERS ?= /System/Library/Frameworks/JavaVM.framework/Versions/A/Headers
#JAVA_HEADERS?=/Developer/SDKs/MacOSX10.6.sdk/System/Library/Frameworks/JavaVM.framework/Versions/1.6.0/Headers/
endif

ifeq ("$(OS)", "linux")
JAVA_HOME ?= $(shell readlink -f /usr/bin/javac | sed "s:bin/javac::")
JAVA_HEADERS ?= $(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux
endif

#c-icap-java.c
LDFLAGS += -shared -licapapi
SOURCE := src/modules/java/c-icap-java.c
TARGET := c-icap-java.so
INCLUDES += -I$(JAVA_HEADERS)

#doc need doxygen
DOXYGEN := doxygen
DOXYFILE := c-icap-java.Doxyfile
DOXYGEN_TARGET_SRC := src
DOXYGEN_TARGET := doc

#flow need graphviz
DOT := dot
DOT_TARGET := flow.dot
DOT_OPTIONS ?= -Tjpg

all: $(TARGET)

install:$(TARGET)
	$(info not-impremented-yet)

$(TARGET): $(SOURCE)
	$(CC) $(FLAGS) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $< $(INCLUDES)

doc: $(DOXYGEN_TARGET_SRC) $(DOXYFILE)
	$(DOXYGEN) $(DOXYFILE)

flow: $(DOT_TARGET)
	$(DOT) $(DOT_OPTIONS) $(DOT_TARGET) -o $(DOT_TARGET:.dot=.jpg)

.PHONY: uninstall 
uninstall:
	$(info not-impremented-yet)

.PHONY: clean
clean:
	$(RM) $(TARGET)

.PHONY: cleandoc
cleandoc:
	$(RM) -r $(DOXYGEN_TARGET)

.PHONY: cleanflow
cleanflow:
	$(RM) $(DOT_TARGET:.dot=.jpg)

CLEAN_TARGETS = clean cleandoc cleanflow

.PHONY: cleanall
cleanall:
	$(MAKE) $(CLEAN_TARGETS)
