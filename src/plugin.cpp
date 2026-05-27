#include "plugin.hpp"

Plugin* pluginInstance;

struct SticksyBlank5 : Module {
	SticksyBlank5() {
		config(0, 0, 0, 0);
	}

	void process(const ProcessArgs& args) override {
		// Decorative module: no DSP behavior in Phase 0.
	}
};

struct SticksyBlank5Widget : ModuleWidget {
	SticksyBlank5Widget(SticksyBlank5* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/5hp/metal.svg")));
	}
};

Model* modelSticksyBlank5 = createModel<SticksyBlank5, SticksyBlank5Widget>("SticksyBlank5");

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSticksyBlank5);
}
