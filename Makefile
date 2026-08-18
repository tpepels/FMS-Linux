CXX ?= g++
PKG_CONFIG ?= pkg-config

CPPFLAGS += $(shell $(PKG_CONFIG) --cflags sdl2) -Isrc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -pthread
LDLIBS += $(shell $(PKG_CONFIG) --libs sdl2) -pthread

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
TARGET := fms-linux
TEST_TARGET := fms-tests
PERSISTENCE_TEST_TARGET := fms-persistence-tests
AUDIO_TEST_TARGET := fms-audio-tests
UI_TEST_TARGET := fms-ui-tests
PREFIX ?= /usr/local

.PHONY: all clean run screenshot test audio-smoke install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

run: $(TARGET)
	./$(TARGET)

screenshot: $(TARGET)
	tmp_dir="$$(mktemp -d)"; \
	trap 'rm -r -- "$$tmp_dir"' EXIT; \
	SDL_VIDEODRIVER=dummy ./$(TARGET) --no-audio --screenshot fms-ui.bmp --save-path "$$tmp_dir/state.bin"

audio-smoke: $(TARGET)
	tmp_dir="$$(mktemp -d)"; \
	trap 'rm -r -- "$$tmp_dir"' EXIT; \
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(TARGET) --audio-smoke --save-path "$$tmp_dir/state.bin"

$(TEST_TARGET): tests/test_model.cpp src/model.cpp src/persistence.cpp src/model.hpp src/persistence.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/test_model.cpp src/model.cpp src/persistence.cpp -o $@ -pthread

$(PERSISTENCE_TEST_TARGET): tests/test_persistence.cpp src/model.cpp src/persistence.cpp src/model.hpp src/persistence.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/test_persistence.cpp src/model.cpp src/persistence.cpp -o $@ -pthread

$(AUDIO_TEST_TARGET): tests/test_audio.cpp src/model.cpp src/audio.cpp src/model.hpp src/audio.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/test_audio.cpp src/model.cpp src/audio.cpp -o $@ $(LDLIBS)

$(UI_TEST_TARGET): tests/test_ui.cpp src/model.cpp src/audio.cpp src/ui.cpp src/model.hpp src/audio.hpp src/ui.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/test_ui.cpp src/model.cpp src/audio.cpp src/ui.cpp -o $@ $(LDLIBS)

test: $(TEST_TARGET) $(PERSISTENCE_TEST_TARGET) $(AUDIO_TEST_TARGET) $(UI_TEST_TARGET) $(TARGET)
	./$(TEST_TARGET)
	./$(PERSISTENCE_TEST_TARGET)
	SDL_AUDIODRIVER=dummy ./$(AUDIO_TEST_TARGET)
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(UI_TEST_TARGET)
	./$(TARGET) --help >/dev/null

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/fms-linux
	install -Dm644 packaging/io.fmslinux.FMS.desktop $(DESTDIR)$(PREFIX)/share/applications/io.fmslinux.FMS.desktop
	install -Dm644 packaging/io.fmslinux.FMS.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/io.fmslinux.FMS.svg

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/fms-linux
	$(RM) $(DESTDIR)$(PREFIX)/share/applications/io.fmslinux.FMS.desktop
	$(RM) $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/io.fmslinux.FMS.svg

clean:
	$(RM) $(TARGET) $(TEST_TARGET) $(PERSISTENCE_TEST_TARGET) $(AUDIO_TEST_TARGET) $(UI_TEST_TARGET) $(OBJECTS) $(OBJECTS:.o=.d) fms-ui.bmp
