#include "plugin.hpp"

#include <osdialog.h>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

Plugin* pluginInstance;

static const std::string FALLBACK_STICKER_PATH = "res/fallback/Sticksy.svg";
static constexpr float DEG_TO_RAD = 0.01745329251994329577f;

struct StickerEntry {
	std::string path;
	std::string displayName;
	float x = 0.f;
	float y = 0.f;
	float rotation = 0.f;
	std::shared_ptr<window::Svg> svg;
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

	enum StorageMode {
		STORAGE_REFERENCED,
		STORAGE_STICKSY_LIBRARY,
		NUM_STORAGE_MODES
	};

	Background background = BACKGROUND_METAL;
	Mode mode = MODE_SINGLE;
	StorageMode storageMode = STORAGE_REFERENCED;
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
		if (!stickers.empty())
			return;
		if (mode == newMode)
			return;
		mode = newMode;
		stickerVersion++;
	}

	static const std::vector<std::string>& modeKeys() {
		static const std::vector<std::string> keys = {"single", "multiple"};
		return keys;
	}

	static Mode modeFromKey(const std::string& key) {
		const auto& keys = modeKeys();
		for (int i = 0; i < (int) keys.size(); i++) {
			if (keys[i] == key)
				return (Mode) i;
		}
		return MODE_SINGLE;
	}

	std::string modeKey() const {
		return modeKeys()[mode];
	}

	static const std::vector<std::string>& storageModeKeys() {
		static const std::vector<std::string> keys = {"referenced", "library"};
		return keys;
	}

	static StorageMode storageModeFromKey(const std::string& key) {
		const auto& keys = storageModeKeys();
		for (int i = 0; i < (int) keys.size(); i++) {
			if (keys[i] == key)
				return (StorageMode) i;
		}
		return STORAGE_REFERENCED;
	}

	std::string storageModeKey() const {
		return storageModeKeys()[storageMode];
	}

	void setStorageMode(StorageMode newStorageMode) {
		if (newStorageMode < 0 || newStorageMode >= NUM_STORAGE_MODES)
			newStorageMode = STORAGE_REFERENCED;
		if (storageMode == newStorageMode)
			return;
		storageMode = newStorageMode;
		stickerVersion++;
	}

	std::string ensureLibraryCopyPath(const std::string& sourcePath) {
		std::filesystem::path source = std::filesystem::u8path(sourcePath);
		std::string filename = source.filename().u8string();
		std::string stem = source.stem().u8string();
		std::string ext = source.extension().u8string();
		std::filesystem::path libraryDir = std::filesystem::u8path(asset::user("Sticksy/stickers"));
		std::error_code ec;
		std::filesystem::create_directories(libraryDir, ec);
		if (ec)
			return sourcePath;

		std::filesystem::path candidate = libraryDir / filename;
		int suffix = 1;
		while (std::filesystem::exists(candidate, ec)) {
			if (ec)
				return sourcePath;
			std::ostringstream name;
			name << stem << "_" << std::setfill('0') << std::setw(3) << suffix++ << ext;
			candidate = libraryDir / name.str();
		}

		std::filesystem::copy_file(source, candidate, std::filesystem::copy_options::none, ec);
		if (ec)
			return sourcePath;
		return candidate.u8string();
	}

	std::string resolveStickerPathForLoad(const std::string& selectedPath) {
		if (storageMode == STORAGE_STICKSY_LIBRARY)
			return ensureLibraryCopyPath(selectedPath);
		return selectedPath;
	}

	void replaceSingleSticker(const StickerEntry& entry) {
		stickers.clear();
		stickers.push_back(entry);
		stickerVersion++;
	}

	void addMultipleSticker(const StickerEntry& entry) {
		stickers.push_back(entry);
		stickerVersion++;
	}

	void clearStickers() {
		if (stickers.empty())
			return;
		stickers.clear();
		stickerVersion++;
	}

	std::shared_ptr<window::Svg> loadSvgWithFallback(const std::string& path) {
		std::shared_ptr<window::Svg> loaded;
		if (!path.empty()) {
			try {
				loaded = APP->window->loadSvg(path);
			}
			catch (...) {
			}
		}
		if (!loaded) {
			try {
				loaded = APP->window->loadSvg(asset::plugin(pluginInstance, FALLBACK_STICKER_PATH));
			}
			catch (...) {
			}
		}
		return loaded;
	}

	void loadStickerSvg(StickerEntry& entry) {
		entry.svg = loadSvgWithFallback(entry.path);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "background", json_string(backgroundKey().c_str()));
		json_object_set_new(rootJ, "mode", json_string(modeKey().c_str()));
		json_object_set_new(rootJ, "storageMode", json_string(storageModeKey().c_str()));

		json_t* stickersJ = json_array();
		for (const StickerEntry& sticker : stickers) {
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
			mode = modeFromKey(modeKey);
		}
		else {
			mode = MODE_SINGLE;
		}
		json_t* storageModeJ = json_object_get(rootJ, "storageMode");
		if (storageModeJ && json_is_string(storageModeJ))
			storageMode = storageModeFromKey(json_string_value(storageModeJ));
		else
			storageMode = STORAGE_REFERENCED;

		clearStickers();
		json_t* stickersJ = json_object_get(rootJ, "stickers");
		if (stickersJ && json_is_array(stickersJ)) {
			size_t stickerCount = json_array_size(stickersJ);
			for (size_t i = 0; i < stickerCount; i++) {
				json_t* stickerJ = json_array_get(stickersJ, i);
				if (!stickerJ || !json_is_object(stickerJ))
					continue;
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
					if (mode == MODE_SINGLE) {
						replaceSingleSticker(entry);
						break;
					}
					addMultipleSticker(entry);
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
	}
};

struct StickerCanvas : Widget {
	SticksyBlank5* module = NULL;

	void draw(const DrawArgs& args) override {
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		if (module && !module->stickers.empty()) {
			if (module->mode == SticksyBlank5::MODE_SINGLE) {
				if (module->stickers[0].svg) {
					auto svg = module->stickers[0].svg;
					math::Vec size = svg->getSize();
					nvgSave(args.vg);
					nvgTranslate(args.vg, std::round((box.size.x - size.x) * 0.5f), std::round((box.size.y - size.y) * 0.5f));
					svg->draw(args.vg);
					nvgRestore(args.vg);
				}
			}
			else {
				for (const StickerEntry& sticker : module->stickers) {
					if (!sticker.svg)
						continue;
					math::Vec size = sticker.svg->getSize();
					float x = std::round((box.size.x - size.x) * 0.5f + sticker.x);
					float y = std::round((box.size.y - size.y) * 0.5f + sticker.y);
					nvgSave(args.vg);
					nvgTranslate(args.vg, x + size.x * 0.5f, y + size.y * 0.5f);
					nvgRotate(args.vg, sticker.rotation * DEG_TO_RAD);
					nvgTranslate(args.vg, -size.x * 0.5f, -size.y * 0.5f);
					sticker.svg->draw(args.vg);
					nvgRestore(args.vg);
				}
			}
		}
		nvgRestore(args.vg);
		Widget::draw(args);
	}
};

struct SticksyBlank5Widget : ModuleWidget {
	int lastPanelVersion = -1;
	int lastStickerVersion = -1;
	StickerCanvas* stickerCanvas = NULL;

	void pushModuleChange(SticksyBlank5* module, const std::string& name, std::function<void()> applyChange) {
		if (!module)
			return;
		history::ModuleChange* h = new history::ModuleChange;
		h->name = name;
		h->moduleId = module->id;
		h->oldModuleJ = module->toJson();
		applyChange();
		h->newModuleJ = module->toJson();
		APP->history->push(h);
	}

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
				if (!module->stickers.empty() || module->mode == mode)
					return;
				moduleWidget->pushModuleChange(module, "change Sticksy mode", [&]() {
					module->setMode(mode);
				});
			}
			void step() override {
				MenuItem::step();
				disabled = module && !module->stickers.empty() && module->mode != mode;
				rightText = (module && module->mode == mode) ? "✔" : "";
			}
		};

		auto* singleItem = createMenuItem<ModeItem>("Single");
		singleItem->module = module;
		singleItem->mode = SticksyBlank5::MODE_SINGLE;
		menu->addChild(singleItem);

		auto* multipleItem = createMenuItem<ModeItem>("Multiple");
		multipleItem->module = module;
		multipleItem->mode = SticksyBlank5::MODE_MULTIPLE;
		menu->addChild(multipleItem);

		if (module && !module->stickers.empty()) {
			auto* modeHint = createMenuItem<MenuItem>("Delete loaded SVGs first");
			modeHint->disabled = true;
			menu->addChild(modeHint);
		}

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
					StickerEntry entry;
					entry.path = module->resolveStickerPathForLoad(path);
					entry.displayName = system::getFilename(path);
					if (module->mode == SticksyBlank5::MODE_MULTIPLE) {
						entry.x = random::uniform() * 30.f - 15.f;
						entry.y = random::uniform() * 40.f - 20.f;
						entry.rotation = random::uniform() * 36.f - 18.f;
					}
						module->loadStickerSvg(entry);
					moduleWidget->pushModuleChange(module, "load Sticksy SVG", [&]() {
						if (module->mode == SticksyBlank5::MODE_SINGLE)
							module->replaceSingleSticker(entry);
						else
							module->addMultipleSticker(entry);
					});
				}
			};

		auto* loadItem = createMenuItem<LoadSvgItem>("Load SVG...");
		loadItem->module = module;
		menu->addChild(loadItem);

		menu->addChild(new MenuSeparator());
		MenuLabel* storageLabel = new MenuLabel();
		storageLabel->text = "Storage";
		menu->addChild(storageLabel);

		struct StorageModeItem : MenuItem {
			SticksyBlank5* module;
			SticksyBlank5::StorageMode storageMode;
			void onAction(const event::Action& e) override {
				if (!module || module->storageMode == storageMode)
					return;
				moduleWidget->pushModuleChange(module, "change Sticksy storage mode", [&]() {
					module->setStorageMode(storageMode);
				});
			}
			void step() override {
				MenuItem::step();
				rightText = (module && module->storageMode == storageMode) ? "✔" : "";
			}
		};

		auto* referencedItem = createMenuItem<StorageModeItem>("Referenced");
		referencedItem->module = module;
		referencedItem->storageMode = SticksyBlank5::STORAGE_REFERENCED;
		menu->addChild(referencedItem);

		auto* libraryItem = createMenuItem<StorageModeItem>("Save in Sticksy Library");
		libraryItem->module = module;
		libraryItem->storageMode = SticksyBlank5::STORAGE_STICKSY_LIBRARY;
		menu->addChild(libraryItem);

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
						moduleWidget->pushModuleChange(module, "delete Sticksy sticker", [&]() {
							module->stickers.erase(module->stickers.begin() + index);
							module->stickerVersion++;
						});
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
				moduleWidget->pushModuleChange(module, "change Sticksy background", [&]() {
					module->setBackground(background);
				});
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
