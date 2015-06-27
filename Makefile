# Makefile2
CC = gcc
FLAGS =
CFLAGS = -fPIC -Wall -O2

#c-icap-java.c
LDFLAGS= -shared -licapapi
SOURCE = src/modules/java/c-icap-java.c
TARGET  = c-icap-java.so
ifndef $(JAVA_HOME)
	JAVA_HOME = /usr/lib/jvm/java-6-openjdk-amd64
endif
INCLUDES = -I$(JAVA_HOME)/include/

#doc need doxygen
DOXYGEN = doxygen
DOXYFILE = c-icap-java.Doxyfile
DOXYGEN_TARGET_SRC = src
DOXYGEN_TARGET = doc

#flow need graphviz
DOT = dot
DOT_TARGET = flow.dot
DOT_OPTIONS = -Tjpg

install:$(TARGET)
	$(info not-impremented-yet)

all: $(TARGET)

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
