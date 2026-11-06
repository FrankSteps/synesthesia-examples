CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra

BUILD = akko-driver/builds

.PHONY: all run clean

all: $(BUILD)/akko_bridge $(BUILD)/akko_music

$(BUILD)/akko_bridge: akko-driver/src/akko_bridge.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -lcurl

$(BUILD)/akko_music: akko-driver/src/akko_music.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -pthread

run: all
	$(BUILD)/akko_music & \
	sleep 1; \
	$(BUILD)/akko_bridge

clean:
	rm -f $(BUILD)/akko_bridge $(BUILD)/akko_music