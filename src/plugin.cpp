#include "plugin.hpp"

#include <string>
#include <vector>

Plugin* pluginInstance;

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

	Background background = BACKGROUND_METAL;
	int panelVersion = 0;

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

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "background", json_string(backgroundKey().c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* backgroundJ = json_object_get(rootJ, "background");
		if (backgroundJ && json_is_string(backgroundJ)) {
			setBackground(backgroundFromKey(json_string_value(backgroundJ)));
		}
		else {
			setBackground(BACKGROUND_METAL);
		}
	}

	void process(const ProcessArgs& args) override {
		// Decorative module: no DSP behavior in Phase 1.
	}
};

struct SticksyBlank5Widget;

struct SetBackgroundAction : history::ModuleAction {
	SticksyBlank5::Background oldBackground;
	SticksyBlank5::Background newBackground;

	void undo() override {
		Module* baseModule = APP->engine->getModule(moduleId);
		if (auto* module = dynamic_cast<SticksyBlank5*>(baseModule)) {
			module->setBackground(oldBackground);
		}
	}

	void redo() override {
		Module* baseModule = APP->engine->getModule(moduleId);
		if (auto* module = dynamic_cast<SticksyBlank5*>(baseModule)) {
			module->setBackground(newBackground);
		}
	}
};

struct SticksyBlank5Widget : ModuleWidget {
	int lastPanelVersion = -1;

	void applyPanelForBackground(SticksyBlank5::Background background) {
		const auto& key = SticksyBlank5::backgroundKeys()[background];
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/5hp/" + key + ".svg")));
	}

	SticksyBlank5Widget(SticksyBlank5* module) {
		setModule(module);
		applyPanelForBackground(module ? module->background : SticksyBlank5::BACKGROUND_METAL);
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
	}

	void appendContextMenu(Menu* menu) override {
		auto* module = dynamic_cast<SticksyBlank5*>(this->module);
		assert(menu);

		menu->addChild(new MenuSeparator());
		MenuLabel* label = new MenuLabel();
		label->text = "Background";
		menu->addChild(label);

		struct BackgroundItem : MenuItem {
			SticksyBlank5* module;
			SticksyBlank5::Background background;

			void onAction(const event::Action& e) override {
				if (!module)
					return;
				if (module->background == background)
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

		const std::vector<std::string> labels = {
			"Neutral", "Wood", "Metal", "Fabric", "Paper", "Black", "White"
		};

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
