# Makefile2
CC = gcc
FLAGS =
CFLAGS = -fPIC -Wall -O2
LDFLAGS= -shared -llibicapapi
TARGET  = c-icap-java.so
SOURCE = src/modules/java/c-icap-java.c
ifndef $(JAVA_HOME)
	JAVA_HOME = /usr/lib/jvm/java-6-openjdk-amd64
endif
INCLUDES = -I$(JAVA_HOME)/include/

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(FLAGS) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $< $(INCLUDES)

.PHONY: clean
clean:
	$(RM) $(TARGET)
