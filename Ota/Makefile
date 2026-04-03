CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I. -I/home/xu/GetWay/Public
LIBS     := -lcurl -lssl -lcrypto -lpthread

TARGET   := ota
OBJS     := OTA.o ota_main.o

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LIBS)

OTA.o: OTA.cpp OTA.h
	$(CXX) $(CXXFLAGS) -c OTA.cpp -o OTA.o

ota_main.o: ota_main.cpp OTA.h
	$(CXX) $(CXXFLAGS) -c ota_main.cpp -o ota_main.o

clean:
	rm -f $(OBJS) $(TARGET)

rebuild: clean all