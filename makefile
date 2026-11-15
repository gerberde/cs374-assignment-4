SOURCE := gerberde_assignment4.c
TARGET := main

$(TARGET): $(SOURCE)
	gcc -std=gnu99 -Wall -o $(TARGET) $(SOURCE) -lm

clean: 
	rm -f $(TARGET)
