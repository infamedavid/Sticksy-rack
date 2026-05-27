RACK_DIR ?= ../Rack-SDK

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard res/*)
DISTRIBUTABLES += $(wildcard res/panels/*/*.svg)
DISTRIBUTABLES += $(wildcard res/fallback/*.svg)
DISTRIBUTABLES += plugin.json

include $(RACK_DIR)/plugin.mk
