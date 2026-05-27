#include "plugin.hpp"

#include <osdialog.h>

#include <string>
#include <vector>

Plugin* pluginInstance;

static const std::string FALLBACK_STICKER_PATH = "res/fallback/Sticksy.svg";

struct StickerEntry {
	std::string path;
	std::string displayName;
	float x = 0.f;
	float y = 0.f;
	float rotation = 0.f;
	std::shared_ptr<Svg> svg;
};

struct SticksyBlank5 : Module {
	enum Background {
		BACKGROUND_NEUTRAL,
		BACKGROUND_WOOD,
		BACKGROUND_METAL,
		BACKGROUND_FABRIC,
		BACKGROUND_PAPER,
		BACKGROUND_BLACK,
		BACKGROUND_WHITE,
		NUM_BACKGROUNDS
	};

	enum Mode {
		MODE_SINGLE,
		MODE_MULTIPLE,
		NUM_MODES
	};

	Background background = BACKGROUND_METAL;
	Mode mode = MODE_SINGLE;
	std::vector<StickerEntry> stickers;
	int panelVersion = 0;
	int stickerVersion = 0;

	SticksyBlank5() {
		config(0, 0, 0, 0);
	}

	static const std::vector<std::string>& backgroundKeys() {
		static const std::vector<std::string> keys = {
			"neutral", "wood", "metal", "fabric", "paper", "black", "white"
		};
		return keys;
	}

	static Background backgroundFromKey(const std::string& key) {
		const auto& keys = backgroundKeys();
		for (int i = 0; i < (int) keys.size(); i++) {
			if (keys[i] == key)
				return (Background) i;
		}
		return BACKGROUND_METAL;
	}

	std::string backgroundKey() const {
		return backgroundKeys()[background];
	}

	void setBackground(Background newBackground) {
		if (newBackground < 0 || newBackground >= NUM_BACKGROUNDS)
			newBackground = BACKGROUND_METAL;
		if (background == newBackground)
			return;
		background = newBackground;
		panelVersion++;
	}

	void setMode(Mode newMode) {
		if (newMode < 0 || newMode >= NUM_MODES)
			newMode = MODE_SINGLE;
		if (newMode == MODE_MULTIPLE)
			newMode = MODE_SINGLE;
		if (mode == newMode)
			return;
		mode = newMode;
		stickerVersion++;
	}

	void replaceSingleSticker(const StickerEntry& entry) {
		stickers.clear();
		stickers.push_back(entry);
		stickerVersion++;
	}

	void clearStickers() {
		if (stickers.empty())
			return;
		stickers.clear();
		stickerVersion++;
	}

	std::shared_ptr<Svg> loadSvgWithFallback(const std::string& path) {
		std::shared_ptr<Svg> loaded;
		if (!path.empty()) {
			try {
				loaded = APP->window->loadSvg(path);
			}
			catch (...) {
			}
		}
		if (!loaded) {
			loaded = APP->window->loadSvg(asset::plugin(pluginInstance, FALLBACK_STICKER_PATH));
		}
		return loaded;
	}

	void loadStickerSvg(StickerEntry& entry) {
		entry.svg = loadSvgWithFallback(entry.path);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "background", json_string(backgroundKey().c_str()));
		json_object_set_new(rootJ, "mode", json_string("single"));

		json_t* stickersJ = json_array();
		if (!stickers.empty()) {
			const StickerEntry& sticker = stickers[0];
			json_t* stickerJ = json_object();
			json_object_set_new(stickerJ, "path", json_string(sticker.path.c_str()));
			json_object_set_new(stickerJ, "displayName", json_string(sticker.displayName.c_str()));
			json_object_set_new(stickerJ, "x", json_real(sticker.x));
			json_object_set_new(stickerJ, "y", json_real(sticker.y));
			json_object_set_new(stickerJ, "rotation", json_real(sticker.rotation));
			json_array_append_new(stickersJ, stickerJ);
		}
		json_object_set_new(rootJ, "stickers", stickersJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* backgroundJ = json_object_get(rootJ, "background");
		if (backgroundJ && json_is_string(backgroundJ))
			setBackground(backgroundFromKey(json_string_value(backgroundJ)));
		else
			setBackground(BACKGROUND_METAL);

		json_t* modeJ = json_object_get(rootJ, "mode");
		if (modeJ && json_is_string(modeJ)) {
			std::string modeKey = json_string_value(modeJ);
			setMode(modeKey == "single" ? MODE_SINGLE : MODE_SINGLE);
		}
		else {
			setMode(MODE_SINGLE);
		}

		clearStickers();
		json_t* stickersJ = json_object_get(rootJ, "stickers");
		if (stickersJ && json_is_array(stickersJ) && json_array_size(stickersJ) > 0) {
			json_t* stickerJ = json_array_get(stickersJ, 0);
			if (stickerJ && json_is_object(stickerJ)) {
				json_t* pathJ = json_object_get(stickerJ, "path");
				if (pathJ && json_is_string(pathJ)) {
					StickerEntry entry;
					entry.path = json_string_value(pathJ);
					entry.displayName = system::getFilename(entry.path);
					if (json_t* displayNameJ = json_object_get(stickerJ, "displayName"); displayNameJ && json_is_string(displayNameJ))
						entry.displayName = json_string_value(displayNameJ);
					if (json_t* xJ = json_object_get(stickerJ, "x"); xJ && json_is_number(xJ))
						entry.x = json_number_value(xJ);
					if (json_t* yJ = json_object_get(stickerJ, "y"); yJ && json_is_number(yJ))
						entry.y = json_number_value(yJ);
					if (json_t* rotationJ = json_object_get(stickerJ, "rotation"); rotationJ && json_is_number(rotationJ))
						entry.rotation = json_number_value(rotationJ);

					loadStickerSvg(entry);
					replaceSingleSticker(entry);
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
	}
};

struct SetBackgroundAction : history::ModuleAction {
	SticksyBlank5::Background oldBackground;
	SticksyBlank5::Background newBackground;
	void undo() override { if (auto* m = dynamic_cast<SticksyBlank5*>(APP->engine->getModule(moduleId))) m->setBackground(oldBackground); }
	void redo() override { if (auto* m = dynamic_cast<SticksyBlank5*>(APP->engine->getModule(moduleId))) m->setBackground(newBackground); }
};

struct DeleteStickerAction : history::ModuleAction {
	int stickerIndex = -1;
	StickerEntry deletedSticker;

	void undo() override {
		Module* baseModule = APP->engine->getModule(moduleId);
		auto* module = dynamic_cast<SticksyBlank5*>(baseModule);
		if (!module)
			return;
		if (stickerIndex < 0 || stickerIndex > (int) module->stickers.size())
			return;
		module->stickers.insert(module->stickers.begin() + stickerIndex, deletedSticker);
		module->stickerVersion++;
	}

	void redo() override {
		Module* baseModule = APP->engine->getModule(moduleId);
		auto* module = dynamic_cast<SticksyBlank5*>(baseModule);
		if (!module)
			return;
		if (stickerIndex < 0 || stickerIndex >= (int) module->stickers.size())
			return;
		module->stickers.erase(module->stickers.begin() + stickerIndex);
		module->stickerVersion++;
	}
};

struct StickerCanvas : Widget {
	SticksyBlank5* module = NULL;

	void draw(const DrawArgs& args) override {
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		if (module && !module->stickers.empty() && module->stickers[0].svg) {
			auto svg = module->stickers[0].svg;
			math::Vec size = svg->getSize();
			nvgSave(args.vg);
			nvgTranslate(args.vg, std::round((box.size.x - size.x) * 0.5f), std::round((box.size.y - size.y) * 0.5f));
			svg->draw(args.vg);
			nvgRestore(args.vg);
		}
		nvgRestore(args.vg);
		Widget::draw(args);
	}
};

struct SticksyBlank5Widget : ModuleWidget {
	int lastPanelVersion = -1;
	int lastStickerVersion = -1;
	StickerCanvas* stickerCanvas = NULL;

	void applyPanelForBackground(SticksyBlank5::Background background) {
		const auto& key = SticksyBlank5::backgroundKeys()[background];
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/5hp/" + key + ".svg")));
	}

	SticksyBlank5Widget(SticksyBlank5* module) {
		setModule(module);
		applyPanelForBackground(module ? module->background : SticksyBlank5::BACKGROUND_METAL);

		stickerCanvas = new StickerCanvas();
		stickerCanvas->box = box.zeroPos();
		stickerCanvas->module = module;
		addChild(stickerCanvas);
	}

	void step() override {
		ModuleWidget::step();
		auto* module = dynamic_cast<SticksyBlank5*>(this->module);
		if (!module)
			return;
		if (module->panelVersion != lastPanelVersion) {
			applyPanelForBackground(module->background);
			lastPanelVersion = module->panelVersion;
		}
		if (module->stickerVersion != lastStickerVersion) {
			lastStickerVersion = module->stickerVersion;
		}
	}

	void appendContextMenu(Menu* menu) override {
		auto* module = dynamic_cast<SticksyBlank5*>(this->module);
		assert(menu);

		menu->addChild(new MenuSeparator());
		MenuLabel* modeLabel = new MenuLabel();
		modeLabel->text = "Mode";
		menu->addChild(modeLabel);

		struct ModeItem : MenuItem {
			SticksyBlank5* module;
			SticksyBlank5::Mode mode;
			void onAction(const event::Action& e) override {
				if (!module)
					return;
				module->setMode(mode);
			}
			void step() override {
				MenuItem::step();
				rightText = (module && module->mode == mode) ? "✔" : "";
			}
		};

		auto* singleItem = createMenuItem<ModeItem>("Single");
		singleItem->module = module;
		singleItem->mode = SticksyBlank5::MODE_SINGLE;
		menu->addChild(singleItem);

		auto* multipleItem = createMenuItem<MenuItem>("Multiple");
		multipleItem->disabled = true;
		menu->addChild(multipleItem);

		struct LoadSvgItem : MenuItem {
			SticksyBlank5* module;
			void onAction(const event::Action& e) override {
				if (!module)
					return;
				char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, "Scalable Vector Graphic (.svg):svg");
				if (!pathC)
					return;
				std::string path = pathC;
				std::free(pathC);
				if (path.empty())
					return;
				if (string::lowercase(system::getExtension(path)) != ".svg")
					return;
				try {
					StickerEntry entry;
					entry.path = path;
					entry.displayName = system::getFilename(path);
					entry.svg = APP->window->loadSvg(path);
					module->replaceSingleSticker(entry);
				}
				catch (...) {
				}
			}
		};

		auto* loadItem = createMenuItem<LoadSvgItem>("Load SVG...");
		loadItem->module = module;
		menu->addChild(loadItem);

		menu->addChild(new MenuSeparator());
		MenuLabel* loadedLabel = new MenuLabel();
		loadedLabel->text = "Loaded SVGs";
		menu->addChild(loadedLabel);

		if (module && module->stickers.empty()) {
			auto* noneItem = createMenuItem<MenuItem>("(none)");
			noneItem->disabled = true;
			menu->addChild(noneItem);
		}

		struct StickerItem : MenuItem {
			SticksyBlank5* module;
			int index = -1;

			Menu* createChildMenu() override {
				Menu* submenu = new Menu;
				struct DeleteItem : MenuItem {
					SticksyBlank5* module;
					int index = -1;
					void onAction(const event::Action& e) override {
						if (!module || index < 0 || index >= (int) module->stickers.size())
							return;
						auto* action = new DeleteStickerAction();
						action->name = "delete Sticksy sticker";
						action->moduleId = module->id;
						action->stickerIndex = index;
						action->deletedSticker = module->stickers[index];
						APP->history->push(action);
					}
				};
				auto* deleteItem = createMenuItem<DeleteItem>("Delete");
				deleteItem->module = module;
				deleteItem->index = index;
				deleteItem->disabled = !module || index < 0 || index >= (int) module->stickers.size();
				submenu->addChild(deleteItem);
				return submenu;
			}
		};

		if (module) {
			for (int i = 0; i < (int) module->stickers.size(); i++) {
				auto* item = createMenuItem<StickerItem>(module->stickers[i].displayName, RIGHT_ARROW);
				item->module = module;
				item->index = i;
				menu->addChild(item);
			}
		}

		menu->addChild(new MenuSeparator());
		MenuLabel* label = new MenuLabel();
		label->text = "Background";
		menu->addChild(label);

		struct BackgroundItem : MenuItem {
			SticksyBlank5* module;
			SticksyBlank5::Background background;
			void onAction(const event::Action& e) override {
				if (!module || module->background == background)
					return;
				auto* action = new SetBackgroundAction();
				action->name = "change Sticksy background";
				action->moduleId = module->id;
				action->oldBackground = module->background;
				action->newBackground = background;
				APP->history->push(action);
			}
			void step() override {
				MenuItem::step();
				rightText = (module && module->background == background) ? "✔" : "";
			}
		};

		const std::vector<std::string> labels = {"Neutral", "Wood", "Metal", "Fabric", "Paper", "Black", "White"};
		for (int i = 0; i < SticksyBlank5::NUM_BACKGROUNDS; i++) {
			auto* item = createMenuItem<BackgroundItem>(labels[i]);
			item->module = module;
			item->background = (SticksyBlank5::Background) i;
			menu->addChild(item);
		}
	}
};

Model* modelSticksyBlank5 = createModel<SticksyBlank5, SticksyBlank5Widget>("SticksyBlank5");

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSticksyBlank5);
}
