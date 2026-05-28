#include "plugin.hpp"

#include <osdialog.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

Plugin* pluginInstance;

static const std::string FALLBACK_STICKER_PATH = "res/fallback/Sticksy.svg";
static constexpr float DEG_TO_RAD = 0.01745329251994329577f;
static const std::string SVG_EXTENSION_WITH_DOT = ".svg";

static bool hasSvgExtension(const std::string& pathOrFilename) {
	std::string ext = system::getExtension(pathOrFilename);
	ext = string::lowercase(ext);
	return ext == "svg" || ext == ".svg";
}

static std::string stripFinalSvgExtension(const std::string& filename) {
	if(filename.size() < SVG_EXTENSION_WITH_DOT.size()) return filename;
	std::string tail = filename.substr(filename.size() - SVG_EXTENSION_WITH_DOT.size());
	if(string::lowercase(tail) == SVG_EXTENSION_WITH_DOT) return filename.substr(0, filename.size() - SVG_EXTENSION_WITH_DOT.size());
	return filename;
}

struct StickerEntry {
	std::string path;
	std::string displayName;
	float x = 0.f;
	float y = 0.f;
	float rotation = 0.f;
	std::shared_ptr<window::Svg> svg;
};

struct SticksyBlankModule : Module {
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

	int hpWidth = 5;
	std::string panelFolder = "5hp";
	Background background = BACKGROUND_METAL;
	Mode mode = MODE_SINGLE;
	StorageMode storageMode = STORAGE_REFERENCED;
	std::vector<StickerEntry> stickers;
	int panelVersion = 0;
	int stickerVersion = 0;

	SticksyBlankModule(int hp, std::string folder) : hpWidth(hp), panelFolder(std::move(folder)) { config(0,0,0,0); }

	static const std::vector<std::string>& backgroundKeys() { static const std::vector<std::string> k={"neutral","wood","metal","fabric","paper","black","white"}; return k; }
	static const std::vector<std::string>& modeKeys() { static const std::vector<std::string> k={"single","multiple"}; return k; }
	static const std::vector<std::string>& storageModeKeys() { static const std::vector<std::string> k={"referenced","library"}; return k; }

	static Background backgroundFromKey(const std::string& key){ const auto& k=backgroundKeys(); for(int i=0;i<(int)k.size();i++) if(k[i]==key) return (Background)i; return BACKGROUND_METAL; }
	static Mode modeFromKey(const std::string& key){ const auto& k=modeKeys(); for(int i=0;i<(int)k.size();i++) if(k[i]==key) return (Mode)i; return MODE_SINGLE; }
	static StorageMode storageModeFromKey(const std::string& key){ const auto& k=storageModeKeys(); for(int i=0;i<(int)k.size();i++) if(k[i]==key) return (StorageMode)i; return STORAGE_REFERENCED; }

	std::string backgroundKey() const { return backgroundKeys()[background]; }
	std::string modeKey() const { return modeKeys()[mode]; }
	std::string storageModeKey() const { return storageModeKeys()[storageMode]; }

	void setBackground(Background b){ if(b<0||b>=NUM_BACKGROUNDS) b=BACKGROUND_METAL; if(background==b) return; background=b; panelVersion++; }
	void setMode(Mode m){ if(m<0||m>=NUM_MODES) m=MODE_SINGLE; if(!stickers.empty()) return; if(mode==m) return; mode=m; stickerVersion++; }
	void setStorageMode(StorageMode s){ if(s<0||s>=NUM_STORAGE_MODES) s=STORAGE_REFERENCED; if(storageMode==s) return; storageMode=s; stickerVersion++; }
	void replaceSingleSticker(const StickerEntry& e){ stickers.clear(); stickers.push_back(e); stickerVersion++; }
	void addMultipleSticker(const StickerEntry& e){ stickers.push_back(e); stickerVersion++; }
	void clearStickers(){ if(stickers.empty()) return; stickers.clear(); stickerVersion++; }

	std::string ensureLibraryCopyPath(const std::string& sourcePath) {
		std::string filename=system::getFilename(sourcePath);
		if(filename.empty()) return sourcePath;
		std::string stem = stripFinalSvgExtension(filename);
		if(stem.empty()) stem = filename;
		std::string sticksyDir=asset::user("Sticksy");
		if(!system::exists(sticksyDir) && !system::createDirectory(sticksyDir)) return sourcePath;
		std::string libraryDir=system::join(sticksyDir,"stickers");
		if(!system::exists(libraryDir) && !system::createDirectory(libraryDir)) return sourcePath;
		std::string candidate=system::join(libraryDir,filename); int suffix=1;
		while(system::exists(candidate)){ std::ostringstream name; name<<stem<<"_"<<std::setfill('0')<<std::setw(3)<<suffix++<<SVG_EXTENSION_WITH_DOT; candidate=system::join(libraryDir,name.str()); }
		if(!system::copy(sourcePath,candidate)) return sourcePath;
		return candidate;
	}
	std::string resolveStickerPathForLoad(const std::string& selectedPath){ return storageMode==STORAGE_STICKSY_LIBRARY ? ensureLibraryCopyPath(selectedPath) : selectedPath; }

	static bool isValidSvg(const std::shared_ptr<window::Svg>& svg) {
		if(!svg) return false;
		math::Vec size=svg->getSize();
		return size.x>0.f&&size.y>0.f&&std::isfinite(size.x)&&std::isfinite(size.y);
	}
	std::shared_ptr<window::Svg> loadSvgWithFallback(const std::string& path){
		std::shared_ptr<window::Svg> loaded;
		if(!path.empty()){
			try{ loaded=APP->window->loadSvg(path);}catch(...){}
		}
		if(isValidSvg(loaded)) return loaded;
		std::shared_ptr<window::Svg> fallback;
		try{ fallback=APP->window->loadSvg(asset::plugin(pluginInstance,FALLBACK_STICKER_PATH)); }catch(...){}
		if(isValidSvg(fallback)) return fallback;
		return nullptr;
	}
	void loadStickerSvg(StickerEntry& entry){ entry.svg=loadSvgWithFallback(entry.path); }

	math::Vec estimateStickerSize(const StickerEntry& entry) {
		math::Vec size; if(entry.svg) size=entry.svg->getSize();
		auto isBad=[](float v){ return !std::isfinite(v)||v<=0.f||v>100000.f; };
		if(!isBad(size.x)&&!isBad(size.y)) return size;
		std::ifstream file(entry.path);
		if(file.is_open()){
			std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::smatch m; std::regex re(R"(viewBox\s*=\s*["'][^"']*?(-?\d*\.?\d+)[ ,]+(-?\d*\.?\d+)[ ,]+(\d*\.?\d+)[ ,]+(\d*\.?\d+)[^"']*["'])", std::regex::icase);
			if(std::regex_search(text,m,re)&&m.size()>=5){ float vw=std::stof(m[3].str()), vh=std::stof(m[4].str()); if(!isBad(vw)&&!isBad(vh)) return math::Vec(vw,vh); }
		}
		return math::Vec(100.f,100.f);
	}

	void assignPlacementForMultiple(StickerEntry& entry){
		const float panelW=RACK_GRID_WIDTH*(float)hpWidth, panelH=RACK_GRID_HEIGHT; entry.rotation=random::uniform()*36.f-18.f;
		math::Vec size=estimateStickerSize(entry); float absCos=std::fabs(std::cos(entry.rotation*DEG_TO_RAD)), absSin=std::fabs(std::sin(entry.rotation*DEG_TO_RAD));
		float rotW=size.x*absCos+size.y*absSin, rotH=size.x*absSin+size.y*absCos, area=std::max(rotW*rotH,1.f);
		auto visibleRatioAt=[&](float cx,float cy){ float left=cx-rotW*.5f,right=cx+rotW*.5f,top=cy-rotH*.5f,bottom=cy+rotH*.5f; float ix0=std::max(left,0.f),iy0=std::max(top,0.f),ix1=std::min(right,panelW),iy1=std::min(bottom,panelH); float iw=std::max(0.f,ix1-ix0),ih=std::max(0.f,iy1-iy0); return (iw*ih)/area; };
		float bestCx=panelW*.5f,bestCy=panelH*.5f,bestRatio=visibleRatioAt(bestCx,bestCy);
		for(int i=0;i<20;i++){ float cx=random::uniform()*(panelW+rotW)-rotW*.5f, cy=random::uniform()*(panelH+rotH)-rotH*.5f; float ratio=visibleRatioAt(cx,cy); if(ratio>bestRatio){bestRatio=ratio;bestCx=cx;bestCy=cy;} if(ratio>=.5f) break; }
		entry.x=bestCx-panelW*.5f; entry.y=bestCy-panelH*.5f;
	}
	void shakeMultipleStickers(){ if(mode!=MODE_MULTIPLE||stickers.empty()) return; for(StickerEntry& e:stickers) assignPlacementForMultiple(e); stickerVersion++; }

	json_t* dataToJson() override {
		json_t* rootJ=json_object(); json_object_set_new(rootJ,"hpWidth",json_integer(hpWidth)); json_object_set_new(rootJ,"background",json_string(backgroundKey().c_str())); json_object_set_new(rootJ,"mode",json_string(modeKey().c_str())); json_object_set_new(rootJ,"storageMode",json_string(storageModeKey().c_str()));
		json_t* stickersJ=json_array(); for(const StickerEntry& s:stickers){ json_t* j=json_object(); json_object_set_new(j,"path",json_string(s.path.c_str())); json_object_set_new(j,"displayName",json_string(s.displayName.c_str())); json_object_set_new(j,"x",json_real(s.x)); json_object_set_new(j,"y",json_real(s.y)); json_object_set_new(j,"rotation",json_real(s.rotation)); json_array_append_new(stickersJ,j);} json_object_set_new(rootJ,"stickers",stickersJ); return rootJ;
	}
	void dataFromJson(json_t* rootJ) override {
		json_t* backgroundJ=json_object_get(rootJ,"background"); if(backgroundJ&&json_is_string(backgroundJ)) setBackground(backgroundFromKey(json_string_value(backgroundJ))); else setBackground(BACKGROUND_METAL);
		json_t* modeJ=json_object_get(rootJ,"mode"); mode=(modeJ&&json_is_string(modeJ))?modeFromKey(json_string_value(modeJ)):MODE_SINGLE;
		json_t* storageModeJ=json_object_get(rootJ,"storageMode"); storageMode=(storageModeJ&&json_is_string(storageModeJ))?storageModeFromKey(json_string_value(storageModeJ)):STORAGE_REFERENCED;
		clearStickers(); json_t* stickersJ=json_object_get(rootJ,"stickers"); if(stickersJ&&json_is_array(stickersJ)){ size_t n=json_array_size(stickersJ); for(size_t i=0;i<n;i++){ json_t* sj=json_array_get(stickersJ,i); if(!sj||!json_is_object(sj)) continue; json_t* pathJ=json_object_get(sj,"path"); if(!(pathJ&&json_is_string(pathJ))) continue; StickerEntry e; e.path=json_string_value(pathJ); e.displayName=system::getFilename(e.path); json_t* d=json_object_get(sj,"displayName"); if(d&&json_is_string(d)) e.displayName=json_string_value(d); json_t* x=json_object_get(sj,"x"); if(x&&json_is_number(x)) e.x=json_number_value(x); json_t* y=json_object_get(sj,"y"); if(y&&json_is_number(y)) e.y=json_number_value(y); json_t* r=json_object_get(sj,"rotation"); if(r&&json_is_number(r)) e.rotation=json_number_value(r); loadStickerSvg(e); if(mode==MODE_SINGLE){ replaceSingleSticker(e); break; } addMultipleSticker(e);} }
	}
	void process(const ProcessArgs& args) override {}
};

static void pushSticksyModuleChange(SticksyBlankModule* module,const std::string& name,std::function<void()> applyChange){ if(!module) return; auto* h=new history::ModuleChange; h->name=name; h->moduleId=module->id; h->oldModuleJ=module->toJson(); applyChange(); h->newModuleJ=module->toJson(); APP->history->push(h);}

struct StickerCanvas : Widget {
	SticksyBlankModule* module = NULL;
	void draw(const DrawArgs& args) override {
		nvgSave(args.vg); nvgScissor(args.vg,0.f,0.f,box.size.x,box.size.y);
		if(module&&!module->stickers.empty()){
			if(module->mode==SticksyBlankModule::MODE_SINGLE){ if(module->stickers[0].svg){ auto svg=module->stickers[0].svg; math::Vec size=svg->getSize(); nvgSave(args.vg); nvgTranslate(args.vg,std::round((box.size.x-size.x)*.5f),std::round((box.size.y-size.y)*.5f)); svg->draw(args.vg); nvgRestore(args.vg);} }
			else { for(const StickerEntry& s:module->stickers){ if(!s.svg) continue; math::Vec size=s.svg->getSize(); float x=std::round((box.size.x-size.x)*.5f+s.x), y=std::round((box.size.y-size.y)*.5f+s.y); nvgSave(args.vg); nvgTranslate(args.vg,x+size.x*.5f,y+size.y*.5f); nvgRotate(args.vg,s.rotation*DEG_TO_RAD); nvgTranslate(args.vg,-size.x*.5f,-size.y*.5f); s.svg->draw(args.vg); nvgRestore(args.vg);} }
		}
		nvgRestore(args.vg); Widget::draw(args);
	}
};

struct SticksyBlankWidget : ModuleWidget {
	int browserHpWidth = 5;
	int lastPanelVersion=-1,lastStickerVersion=-1; StickerCanvas* stickerCanvas=NULL;
		void applyPanelForBackground(SticksyBlankModule* module){ const auto& key=SticksyBlankModule::backgroundKeys()[module?module->background:SticksyBlankModule::BACKGROUND_METAL]; const std::string folder=module?module->panelFolder:(std::to_string(browserHpWidth)+"hp"); setPanel(createPanel(asset::plugin(pluginInstance,"res/panels/"+folder+"/"+key+".svg"))); }

	SticksyBlankWidget(SticksyBlankModule* module, int defaultHp=5){ setModule(module); browserHpWidth=defaultHp; box.size=math::Vec(RACK_GRID_WIDTH*(module?module->hpWidth:browserHpWidth), RACK_GRID_HEIGHT); applyPanelForBackground(module); stickerCanvas=new StickerCanvas(); stickerCanvas->box=box.zeroPos(); stickerCanvas->module=module; addChild(stickerCanvas);} 
	void step() override { ModuleWidget::step(); auto* module=dynamic_cast<SticksyBlankModule*>(this->module); if(!module) return; if(module->panelVersion!=lastPanelVersion){ applyPanelForBackground(module); lastPanelVersion=module->panelVersion;} if(module->stickerVersion!=lastStickerVersion){ lastStickerVersion=module->stickerVersion; } }

	void appendContextMenu(Menu* menu) override {
		auto* module=dynamic_cast<SticksyBlankModule*>(this->module); assert(menu);
		menu->addChild(new MenuSeparator()); auto* modeLabel=new MenuLabel(); modeLabel->text="Mode"; menu->addChild(modeLabel);
		struct ModeItem:MenuItem{ SticksyBlankModule* module; SticksyBlankModule::Mode mode; void onAction(const event::Action&) override { if(!module||!module->stickers.empty()||module->mode==mode) return; SticksyBlankModule* m=module; SticksyBlankModule::Mode selectedMode=mode; pushSticksyModuleChange(module,"change Sticksy mode",[m,selectedMode](){ m->setMode(selectedMode);}); } void step() override { MenuItem::step(); disabled=module&&!module->stickers.empty()&&module->mode!=mode; rightText=(module&&module->mode==mode)?"✔":""; }};
		auto* s=createMenuItem<ModeItem>("Single"); s->module=module; s->mode=SticksyBlankModule::MODE_SINGLE; menu->addChild(s); auto* m=createMenuItem<ModeItem>("Multiple"); m->module=module; m->mode=SticksyBlankModule::MODE_MULTIPLE; menu->addChild(m);
		if(module&&!module->stickers.empty()){ auto* hint=createMenuItem<MenuItem>("Delete loaded SVGs first"); hint->disabled=true; menu->addChild(hint);} 
		struct LoadSvgItem:MenuItem{ SticksyBlankModule* module; void onAction(const event::Action&) override { if(!module) return; osdialog_filters* filters=osdialog_filters_parse("Scalable Vector Graphic (.svg):svg"); char* pathC=osdialog_file(OSDIALOG_OPEN,NULL,NULL,filters); osdialog_filters_free(filters); if(!pathC) return; std::string selectedPath=pathC; std::free(pathC); if(selectedPath.empty()||!hasSvgExtension(selectedPath)) return; StickerEntry e; std::string storedPath=module->resolveStickerPathForLoad(selectedPath); e.path=storedPath; e.displayName=system::getFilename(storedPath); module->loadStickerSvg(e); if(module->mode==SticksyBlankModule::MODE_MULTIPLE) module->assignPlacementForMultiple(e); SticksyBlankModule* m=module; StickerEntry newEntry=e; pushSticksyModuleChange(module,"load Sticksy SVG",[m,newEntry](){ if(m->mode==SticksyBlankModule::MODE_SINGLE) m->replaceSingleSticker(newEntry); else m->addMultipleSticker(newEntry);}); }};
		auto* load=createMenuItem<LoadSvgItem>("Load SVG..."); load->module=module; menu->addChild(load);
		if(module&&module->mode==SticksyBlankModule::MODE_MULTIPLE){ struct ShakeItem:MenuItem{ SticksyBlankModule* module; void onAction(const event::Action&) override { if(!module||module->mode!=SticksyBlankModule::MODE_MULTIPLE||module->stickers.empty()) return; SticksyBlankModule* m=module; pushSticksyModuleChange(module,"shake Sticksy stickers",[m](){ m->shakeMultipleStickers();}); } void step() override { MenuItem::step(); disabled=!module||module->mode!=SticksyBlankModule::MODE_MULTIPLE||module->stickers.empty(); }}; auto* shake=createMenuItem<ShakeItem>("Shake"); shake->module=module; menu->addChild(shake);} 
		menu->addChild(new MenuSeparator()); auto* stLabel=new MenuLabel(); stLabel->text="Storage"; menu->addChild(stLabel);
		struct StorageItem:MenuItem{ SticksyBlankModule* module; SticksyBlankModule::StorageMode storageMode; void onAction(const event::Action&) override { if(!module||module->storageMode==storageMode) return; SticksyBlankModule* m=module; SticksyBlankModule::StorageMode selectedStorageMode=storageMode; pushSticksyModuleChange(module,"change Sticksy storage mode",[m,selectedStorageMode](){ m->setStorageMode(selectedStorageMode);}); } void step() override { MenuItem::step(); rightText=(module&&module->storageMode==storageMode)?"✔":""; }};
		auto* ref=createMenuItem<StorageItem>("Referenced"); ref->module=module; ref->storageMode=SticksyBlankModule::STORAGE_REFERENCED; menu->addChild(ref); auto* lib=createMenuItem<StorageItem>("Save in Sticksy Library"); lib->module=module; lib->storageMode=SticksyBlankModule::STORAGE_STICKSY_LIBRARY; menu->addChild(lib);
		menu->addChild(new MenuSeparator()); auto* loadedLabel=new MenuLabel(); loadedLabel->text="Loaded SVGs"; menu->addChild(loadedLabel);
		if(module&&module->stickers.empty()){ auto* none=createMenuItem<MenuItem>("(none)"); none->disabled=true; menu->addChild(none);} 
		struct StickerItem:MenuItem{ SticksyBlankModule* module; int index=-1; Menu* createChildMenu() override { Menu* submenu=new Menu; struct DeleteItem:MenuItem{ SticksyBlankModule* module; int index=-1; void onAction(const event::Action&) override { if(!module||index<0||index>=(int)module->stickers.size()) return; SticksyBlankModule* m=module; int deleteIndex=index; pushSticksyModuleChange(module,"delete Sticksy sticker",[m,deleteIndex](){ if(deleteIndex<0||deleteIndex>=(int)m->stickers.size()) return; m->stickers.erase(m->stickers.begin()+deleteIndex); m->stickerVersion++;}); }}; auto* del=createMenuItem<DeleteItem>("Delete"); del->module=module; del->index=index; del->disabled=!module||index<0||index>=(int)module->stickers.size(); submenu->addChild(del); return submenu; }};
		if(module){ for(int i=0;i<(int)module->stickers.size();i++){ auto* item=createMenuItem<StickerItem>(module->stickers[i].displayName,RIGHT_ARROW); item->module=module; item->index=i; menu->addChild(item);} }
		menu->addChild(new MenuSeparator()); auto* bgLabel=new MenuLabel(); bgLabel->text="Background"; menu->addChild(bgLabel);
		struct BgItem:MenuItem{ SticksyBlankModule* module; SticksyBlankModule::Background background; void onAction(const event::Action&) override { if(!module||module->background==background) return; SticksyBlankModule* m=module; SticksyBlankModule::Background selectedBackground=background; pushSticksyModuleChange(module,"change Sticksy background",[m,selectedBackground](){ m->setBackground(selectedBackground);}); } void step() override { MenuItem::step(); rightText=(module&&module->background==background)?"✔":""; }};
		const std::vector<std::string> labels={"Neutral","Wood","Metal","Fabric","Paper","Black","White"}; for(int i=0;i<SticksyBlankModule::NUM_BACKGROUNDS;i++){ auto* item=createMenuItem<BgItem>(labels[i]); item->module=module; item->background=(SticksyBlankModule::Background)i; menu->addChild(item);} 
	}
};

struct SticksyBlank3 : SticksyBlankModule { SticksyBlank3() : SticksyBlankModule(3, "3hp") {} };
struct SticksyBlank5 : SticksyBlankModule { SticksyBlank5() : SticksyBlankModule(5, "5hp") {} };
struct SticksyBlank9 : SticksyBlankModule { SticksyBlank9() : SticksyBlankModule(9, "9hp") {} };
struct SticksyBlank12 : SticksyBlankModule { SticksyBlank12() : SticksyBlankModule(12, "12hp") {} };

struct SticksyBlank3Widget : SticksyBlankWidget { SticksyBlank3Widget(SticksyBlank3* module) : SticksyBlankWidget(module, 3) {} };
struct SticksyBlank5Widget : SticksyBlankWidget { SticksyBlank5Widget(SticksyBlank5* module) : SticksyBlankWidget(module, 5) {} };
struct SticksyBlank9Widget : SticksyBlankWidget { SticksyBlank9Widget(SticksyBlank9* module) : SticksyBlankWidget(module, 9) {} };
struct SticksyBlank12Widget : SticksyBlankWidget { SticksyBlank12Widget(SticksyBlank12* module) : SticksyBlankWidget(module, 12) {} };


struct SticksyFlipbookModule : Module {
	enum InputIds {
		CLK_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	static const int MAX_FRAMES = 128;
	std::vector<std::string> framePaths;
	std::vector<std::shared_ptr<window::Svg> > frameSvgs;
	int currentFrameIndex = 0;
	int frameVersion = 0;

	SticksyFlipbookModule() {
		config(0, NUM_INPUTS, NUM_OUTPUTS, 0);
		configInput(CLK_INPUT, "CLK");
		configOutput(EOC_OUTPUT, "EOC");
	}

	static bool isValidSvg(const std::shared_ptr<window::Svg>& svg) {
		if(!svg) return false;
		math::Vec size = svg->getSize();
		return size.x > 0.f && size.y > 0.f && std::isfinite(size.x) && std::isfinite(size.y);
	}

	std::shared_ptr<window::Svg> loadSvgWithFallback(const std::string& path) {
		std::shared_ptr<window::Svg> loaded;
		if(!path.empty()) {
			try { loaded = APP->window->loadSvg(path); } catch(...) {}
		}
		if(isValidSvg(loaded)) return loaded;
		std::shared_ptr<window::Svg> fallback;
		try { fallback = APP->window->loadSvg(asset::plugin(pluginInstance, FALLBACK_STICKER_PATH)); } catch(...) {}
		if(isValidSvg(fallback)) return fallback;
		return nullptr;
	}

	void setFrames(const std::vector<std::string>& paths, int selectedIndex) {
		framePaths = paths;
		if(framePaths.empty()) {
			frameSvgs.clear();
			currentFrameIndex = 0;
			frameVersion++;
			return;
		}
		frameSvgs.clear();
		frameSvgs.reserve(framePaths.size());
		for(const std::string& path : framePaths) {
			frameSvgs.push_back(loadSvgWithFallback(path));
		}
		if(selectedIndex < 0) selectedIndex = 0;
		if(selectedIndex >= (int)framePaths.size()) selectedIndex = (int)framePaths.size() - 1;
		currentFrameIndex = selectedIndex;
		frameVersion++;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_t* framePathsJ = json_array();
		for(const std::string& path : framePaths) {
			json_array_append_new(framePathsJ, json_string(path.c_str()));
		}
		json_object_set_new(rootJ, "framePaths", framePathsJ);
		json_object_set_new(rootJ, "currentFrameIndex", json_integer(currentFrameIndex));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		std::vector<std::string> loadedPaths;
		json_t* framePathsJ = json_object_get(rootJ, "framePaths");
		if(framePathsJ && json_is_array(framePathsJ)) {
			size_t n = json_array_size(framePathsJ);
			for(size_t i = 0; i < n && i < (size_t)MAX_FRAMES; i++) {
				json_t* pathJ = json_array_get(framePathsJ, i);
				if(pathJ && json_is_string(pathJ)) loadedPaths.push_back(json_string_value(pathJ));
			}
		}
		json_t* legacyFramePathJ = json_object_get(rootJ, "framePath");
		if(loadedPaths.empty() && legacyFramePathJ && json_is_string(legacyFramePathJ)) loadedPaths.push_back(json_string_value(legacyFramePathJ));
		int loadedFrameIndex = 0;
		json_t* currentFrameIndexJ = json_object_get(rootJ, "currentFrameIndex");
		if(currentFrameIndexJ && json_is_integer(currentFrameIndexJ)) loadedFrameIndex = json_integer_value(currentFrameIndexJ);
		setFrames(loadedPaths, loadedFrameIndex);
	}

	void process(const ProcessArgs& args) override {
		outputs[EOC_OUTPUT].setVoltage(0.f);
	}
};

static void pushFlipbookModuleChange(SticksyFlipbookModule* module, const std::string& name, std::function<void()> applyChange) {
	if(!module) return;
	auto* h = new history::ModuleChange;
	h->name = name;
	h->moduleId = module->id;
	h->oldModuleJ = module->toJson();
	applyChange();
	h->newModuleJ = module->toJson();
	APP->history->push(h);
}

struct FlipbookCanvas : Widget {
	SticksyFlipbookModule* module = NULL;
	void draw(const DrawArgs& args) override {
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		if(module && !module->frameSvgs.empty()) {
			int frameIndex = module->currentFrameIndex;
			if(frameIndex < 0 || frameIndex >= (int)module->frameSvgs.size()) frameIndex = 0;
				std::shared_ptr<window::Svg> svg = module->frameSvgs[frameIndex];
				if(svg) {
					math::Vec size = svg->getSize();
					float x = std::round((box.size.x - size.x) * 0.5f);
					float y = std::round((box.size.y - size.y) * 0.5f);
					nvgSave(args.vg);
					nvgTranslate(args.vg, x, y);
					svg->draw(args.vg);
					nvgRestore(args.vg);
				}
			}
		nvgRestore(args.vg);
		Widget::draw(args);
	}
};

struct SticksyFlipbookWidget : ModuleWidget {
	FlipbookCanvas* canvas = NULL;
	SticksyFlipbookWidget(SticksyFlipbookModule* module) {
		setModule(module);
		box.size = math::Vec(RACK_GRID_WIDTH * 12.f, RACK_GRID_HEIGHT);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/flipbook/flipbook_12hp.svg")));
		canvas = new FlipbookCanvas();
		canvas->box = box.zeroPos();
		canvas->module = module;
		addChild(canvas);

		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(15.24f, 116.0f)), module, SticksyFlipbookModule::CLK_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(45.72f, 116.0f)), module, SticksyFlipbookModule::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* module = dynamic_cast<SticksyFlipbookModule*>(this->module);
		assert(menu);
		menu->addChild(new MenuSeparator());
		struct LoadFlipbookImageItem : MenuItem {
			SticksyFlipbookModule* module;
			static bool parseFinalNumberSuffix(const std::string& stem, std::string* prefixOut, int* valueOut) {
				if(stem.empty()) return false;
				int i = (int)stem.size() - 1;
				while(i >= 0 && std::isdigit((unsigned char)stem[i])) i--;
				int digitStart = i + 1;
				if(digitStart >= (int)stem.size()) return false;
				std::string numberPart = stem.substr(digitStart);
				if(numberPart.empty()) return false;
				long value = std::strtol(numberPart.c_str(), NULL, 10);
				if(value < 0) return false;
				*prefixOut = stem.substr(0, digitStart);
				*valueOut = (int)value;
				return true;
			}

			static void detectSequencePaths(const std::string& selectedPath, std::vector<std::string>* pathsOut, int* selectedIndexOut) {
				pathsOut->clear();
				*selectedIndexOut = 0;
				std::string selectedFilename = system::getFilename(selectedPath);
				std::string selectedStem = stripFinalSvgExtension(selectedFilename);
				std::string prefix;
				int selectedValue = 0;
				if(!parseFinalNumberSuffix(selectedStem, &prefix, &selectedValue)) {
					pathsOut->push_back(selectedPath);
					return;
				}
				std::vector<std::string> files;
				try {
					files = system::getEntries(system::getDirectory(selectedPath));
				} catch(...) {
					pathsOut->push_back(selectedPath);
					return;
				}
				std::vector<std::pair<int, std::string> > matches;
				for(const std::string& entry : files) {
					if(!hasSvgExtension(entry)) continue;
					std::string stem = stripFinalSvgExtension(entry);
					std::string entryPrefix;
					int value = 0;
					if(!parseFinalNumberSuffix(stem, &entryPrefix, &value)) continue;
					if(entryPrefix != prefix) continue;
					matches.push_back(std::make_pair(value, system::join(system::getDirectory(selectedPath), entry)));
				}
				if(matches.empty()) {
					pathsOut->push_back(selectedPath);
					return;
				}
				std::sort(matches.begin(), matches.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
					if(a.first != b.first) return a.first < b.first;
					return string::lowercase(a.second) < string::lowercase(b.second);
				});
				for(size_t i = 0; i < matches.size() && (int)i < SticksyFlipbookModule::MAX_FRAMES; i++) {
					pathsOut->push_back(matches[i].second);
				}
				for(size_t i = 0; i < pathsOut->size(); i++) {
					if(system::getFilename((*pathsOut)[i]) == selectedFilename) {
						*selectedIndexOut = (int)i;
						return;
					}
				}
				for(size_t i = 0; i < pathsOut->size(); i++) {
					std::string stem = stripFinalSvgExtension(system::getFilename((*pathsOut)[i]));
					std::string entryPrefix;
					int value = 0;
					if(parseFinalNumberSuffix(stem, &entryPrefix, &value) && value == selectedValue && entryPrefix == prefix) {
						*selectedIndexOut = (int)i;
						return;
					}
				}
			}

			void onAction(const event::Action&) override {
				if(!module) return;
				osdialog_filters* filters = osdialog_filters_parse("Scalable Vector Graphic (.svg):svg");
				char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
				osdialog_filters_free(filters);
				if(!pathC) return;
				std::string selectedPath = pathC;
				std::free(pathC);
				if(selectedPath.empty() || !hasSvgExtension(selectedPath)) return;
				SticksyFlipbookModule* m = module;
				std::vector<std::string> newPaths;
				int selectedFrameIndex = 0;
				detectSequencePaths(selectedPath, &newPaths, &selectedFrameIndex);
				pushFlipbookModuleChange(module, "load Sticksy Flipbook image", [m, newPaths, selectedFrameIndex]() { m->setFrames(newPaths, selectedFrameIndex); });
			}
		};
		auto* load = createMenuItem<LoadFlipbookImageItem>("Load Flipbook Image...");
		load->module = module;
		menu->addChild(load);
	}
};

Model* modelSticksyBlank3 = createModel<SticksyBlank3, SticksyBlank3Widget>("SticksyBlank3");
Model* modelSticksyBlank5 = createModel<SticksyBlank5, SticksyBlank5Widget>("SticksyBlank5");
Model* modelSticksyBlank9 = createModel<SticksyBlank9, SticksyBlank9Widget>("SticksyBlank9");
Model* modelSticksyBlank12 = createModel<SticksyBlank12, SticksyBlank12Widget>("SticksyBlank12");
Model* modelSticksyFlipbook = createModel<SticksyFlipbookModule, SticksyFlipbookWidget>("SticksyFlipbook");

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSticksyBlank3);
	p->addModel(modelSticksyBlank5);
	p->addModel(modelSticksyBlank9);
	p->addModel(modelSticksyBlank12);
	p->addModel(modelSticksyFlipbook);
}
