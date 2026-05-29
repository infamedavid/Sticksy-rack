RACK_DIR ?= ../Rack-SDK

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard res/*)
DISTRIBUTABLES += $(wildcard res/panels/*/*.svg)
DISTRIBUTABLES += $(wildcard res/fallback/*.svg)
DISTRIBUTABLES += $(wildcard res/default/Stickers/*.svg)
DISTRIBUTABLES += $(wildcard res/default/Animation/vc_cat/*.svg)
DISTRIBUTABLES += plugin.json

include $(RACK_DIR)/plugin.mk
