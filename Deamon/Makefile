CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I. -I/home/xu/GetWay/Public
LIBS     := -lpthread

TARGET   := /home/xu/GetWay/bin/daemon
OBJS     := Deamon.o deamonmain.o

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LIBS)

Deamon.o: Deamon.cpp Deamon.h
	$(CXX) $(CXXFLAGS) -c Deamon.cpp -o Deamon.o

main.o: main.cpp Deamon.h
	$(CXX) $(CXXFLAGS) -c deamonmain.cpp -o deamonmain.o

clean:
	rm -f $(OBJS) $(TARGET)

rebuild: clean all