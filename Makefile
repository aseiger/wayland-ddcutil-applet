# bar_applet – wf-panel-pi plugin for Raspberry Pi OS (Wayland)
# Builds a shared library that wf-panel-pi loads at runtime.

PLUGIN_NAME = bar_applet

# Paths -------------------------------------------------------------------
PKG_CFG    = pkg-config
PLUGIN_DIR = $(shell $(PKG_CFG) --variable=plugindir wf-panel-pi)

# Toolchain ---------------------------------------------------------------
CXX     ?= g++
CC      ?= gcc

WFP_CFLAGS  := $(shell $(PKG_CFG) --cflags wf-panel-pi)
GTK_CFLAGS  := $(shell $(PKG_CFG) --cflags gtkmm-3.0)
GTK_LIBS    := $(shell $(PKG_CFG) --libs   gtkmm-3.0)

COMMON_FLAGS = -Wall -Wextra -fPIC -O2 $(GTK_CFLAGS) $(WFP_CFLAGS)
CXXFLAGS    += $(COMMON_FLAGS) -std=c++17
CFLAGS      += -Wall -Wextra -fPIC -O2
LDFLAGS     += -shared $(GTK_LIBS)

# Sources -----------------------------------------------------------------
CXX_SRC = src/bar_applet.cpp
C_SRC   = src/brightness.c src/volume.c src/lcdstats.c
OBJS    = $(CXX_SRC:.cpp=.o) $(C_SRC:.c=.o)
TARGET  = lib$(PLUGIN_NAME).so

# Rules -------------------------------------------------------------------
.PHONY: all clean install uninstall

all: $(TARGET)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	install -d $(DESTDIR)$(PLUGIN_DIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)
	@echo ""
	@echo "Installed to $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)"
	@echo "Add 'bar_applet' to your panel config, then restart:"
	@echo "  killall wf-panel-pi; wf-panel-pi &"

uninstall:
	rm -f $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)
	@echo "Uninstalled. Restart wf-panel-pi to take effect."
