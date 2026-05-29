#include "plugin.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

Plugin* pluginInstance;

static const std::string FALLBACK_STICKER_PATH = "res/fallback/Sticksy.svg";
static constexpr float DEG_TO_RAD = 0.01745329251994329577f;
static const std::string SVG_EXTENSION_WITH_DOT = ".svg";

static const std::string STICKSY_USER_LIBRARY_FOLDER = "Sticksy";
static const std::string STICKSY_STICKERS_FOLDER = "Stickers";
static const std::string STICKSY_ANIMATION_FOLDER = "Animation";
static const std::string STICKSY_VC_CAT_FOLDER = "vc_cat";
static const std::string DEFAULT_STICKERS_TEMPLATE_PATH = "res/default/Stickers";
static const std::string DEFAULT_VC_CAT_TEMPLATE_PATH = "res/default/Animation/vc_cat";

static bool hasSvgExtension(const std::string& pathOrFilename);

static bool createDirectoryIfMissing(const std::string& path) {
	if(system::exists(path)) return true;
	if(system::createDirectory(path)) return true;
	WARN("Sticksy: Could not create user library folder '%s'", path.c_str());
	return false;
}

static std::string sticksyUserLibraryPath() {
	return asset::user(STICKSY_USER_LIBRARY_FOLDER);
}

static std::string sticksyUserStickerLibraryPath() {
	return system::join(sticksyUserLibraryPath(), STICKSY_STICKERS_FOLDER);
}

static std::string sticksyUserAnimationLibraryPath() {
	return system::join(sticksyUserLibraryPath(), STICKSY_ANIMATION_FOLDER);
}

static std::string prepareSticksyBrowserInitialPath(const std::string& libraryPath) {
	std::string sticksyDir = sticksyUserLibraryPath();
	if(!createDirectoryIfMissing(sticksyDir)) return "";
	if(!createDirectoryIfMissing(libraryPath)) return "";
	if(!system::exists(libraryPath)) return "";
	return libraryPath;
}

static bool firstBlankSvgBrowserOpen = true;
static bool firstFlipbookImageBrowserOpen = true;
static bool firstFlipbookBackgroundBrowserOpen = true;

static void copySvgFilesIfMissing(const std::string& sourceDir, const std::string& destDir) {
	if(!system::exists(sourceDir)) {
		WARN("Sticksy: Default asset source folder '%s' does not exist", sourceDir.c_str());
		return;
	}
	if(!createDirectoryIfMissing(destDir)) return;

	std::vector<std::string> entries;
	try {
		entries = system::getEntries(sourceDir);
	}
	catch(...) {
		WARN("Sticksy: Could not read default asset source folder '%s'", sourceDir.c_str());
		return;
	}

	for(const std::string& entry : entries) {
		std::string filename = system::getFilename(entry);
		if(filename.empty() || !hasSvgExtension(filename)) continue;
		std::string sourcePath = entry;
		if(system::getDirectory(entry).empty()) sourcePath = system::join(sourceDir, filename);
		std::string destPath = system::join(destDir, filename);
		if(system::exists(destPath)) continue;
		if(!system::copy(sourcePath, destPath)) {
			WARN("Sticksy: Could not copy default asset '%s' to '%s'", sourcePath.c_str(), destPath.c_str());
		}
	}
}

static void ensureDefaultSticksyLibraryInstalled() {
	std::string sticksyDir = sticksyUserLibraryPath();
	std::string stickersDir = system::join(sticksyDir, STICKSY_STICKERS_FOLDER);
	std::string animationDir = system::join(sticksyDir, STICKSY_ANIMATION_FOLDER);
	std::string vcCatDir = system::join(animationDir, STICKSY_VC_CAT_FOLDER);

	createDirectoryIfMissing(sticksyDir);
	createDirectoryIfMissing(stickersDir);
	createDirectoryIfMissing(animationDir);
	createDirectoryIfMissing(vcCatDir);

	copySvgFilesIfMissing(asset::plugin(pluginInstance, DEFAULT_STICKERS_TEMPLATE_PATH), stickersDir);
	copySvgFilesIfMissing(asset::plugin(pluginInstance, DEFAULT_VC_CAT_TEMPLATE_PATH), vcCatDir);
}

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

	static const std::vector<std::string>& backgroundKeys() { static const std::vector<std::string> k={"neutral","wood","metal","paper","black","white"}; return k; }
	static const std::vector<std::string>& modeKeys() { static const std::vector<std::string> k={"single","multiple"}; return k; }
	static const std::vector<std::string>& storageModeKeys() { static const std::vector<std::string> k={"referenced","library"}; return k; }

	static Background backgroundFromKey(const std::string& key){ if(key=="fabric") return BACKGROUND_METAL; const auto& k=backgroundKeys(); for(int i=0;i<(int)k.size();i++) if(k[i]==key) return (Background)i; return BACKGROUND_METAL; }
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
		std::string sticksyDir=sticksyUserLibraryPath();
		if(!createDirectoryIfMissing(sticksyDir)) return sourcePath;
		std::string libraryDir=sticksyUserStickerLibraryPath();
		if(!createDirectoryIfMissing(libraryDir)) return sourcePath;
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
		float margin=RACK_GRID_WIDTH;
		float minX=margin, maxX=panelW-margin;
		float minY=margin, maxY=panelH-margin;
		float cx=(maxX<=minX)?(panelW*.5f):(minX+random::uniform()*(maxX-minX));
		float cy=(maxY<=minY)?(panelH*.5f):(minY+random::uniform()*(maxY-minY));
		entry.x=cx-panelW*.5f; entry.y=cy-panelH*.5f;
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
		struct LoadSvgItem:MenuItem{ SticksyBlankModule* module; void onAction(const event::Action&) override { if(!module) return; osdialog_filters* filters=osdialog_filters_parse("Scalable Vector Graphic (.svg):svg"); std::string initialPath; if(firstBlankSvgBrowserOpen) initialPath=prepareSticksyBrowserInitialPath(sticksyUserStickerLibraryPath()); firstBlankSvgBrowserOpen=false; char* pathC=osdialog_file(OSDIALOG_OPEN,initialPath.empty()?NULL:initialPath.c_str(),NULL,filters); osdialog_filters_free(filters); if(!pathC) return; std::string selectedPath=pathC; std::free(pathC); if(selectedPath.empty()||!hasSvgExtension(selectedPath)) return; StickerEntry e; std::string storedPath=module->resolveStickerPathForLoad(selectedPath); e.path=storedPath; e.displayName=system::getFilename(storedPath); module->loadStickerSvg(e); if(module->mode==SticksyBlankModule::MODE_MULTIPLE) module->assignPlacementForMultiple(e); SticksyBlankModule* m=module; StickerEntry newEntry=e; pushSticksyModuleChange(module,"load Sticksy SVG",[m,newEntry](){ if(m->mode==SticksyBlankModule::MODE_SINGLE) m->replaceSingleSticker(newEntry); else m->addMultipleSticker(newEntry);}); }};
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
		const std::vector<std::string> labels={"Neutral","Wood","Metal","Paper","Black","White"}; for(int i=0;i<SticksyBlankModule::NUM_BACKGROUNDS;i++){ auto* item=createMenuItem<BgItem>(labels[i]); item->module=module; item->background=(SticksyBlankModule::Background)i; menu->addChild(item);} 
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
		RST_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	static const int MAX_FRAMES = 128;
	enum PlayMode {
		PLAY_FORWARD,
		PLAY_REVERSE,
		PLAY_PING_PONG,
		PLAY_RANDOM,
		NUM_PLAY_MODES
	};
	std::vector<std::string> framePaths;
	std::vector<std::shared_ptr<window::Svg> > frameSvgs;
	std::string selectedFramePath;
	std::string backgroundPath;
	std::shared_ptr<window::Svg> backgroundSvg;
	int currentFrameIndex = 0;
	PlayMode playMode = PLAY_FORWARD;
	int pingDirection = 1;
	int randomCycleCounter = 0;
	dsp::SchmittTrigger clkTrigger;
	dsp::SchmittTrigger rstTrigger;
	dsp::PulseGenerator eocPulse;
	int frameVersion = 0;

	SticksyFlipbookModule() {
		config(0, NUM_INPUTS, NUM_OUTPUTS, 0);
		configInput(CLK_INPUT, "CLK");
		configInput(RST_INPUT, "RST");
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

	void setFrames(const std::vector<std::string>& paths, int selectedIndex, const std::string& selectedPath = "") {
		framePaths = paths;
		selectedFramePath = selectedPath;
		randomCycleCounter = 0;
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
		if(selectedFramePath.empty() && currentFrameIndex >= 0 && currentFrameIndex < (int)framePaths.size()) selectedFramePath = framePaths[currentFrameIndex];
		frameVersion++;
	}

	void setPlayMode(PlayMode mode) {
		if(mode < 0 || mode >= NUM_PLAY_MODES) mode = PLAY_FORWARD;
		playMode = mode;
		if(playMode == PLAY_PING_PONG) {
			int last = (int)framePaths.size() - 1;
			pingDirection = (currentFrameIndex == last && last > 0) ? -1 : 1;
		}
	}

	std::shared_ptr<window::Svg> loadSvgWithoutFallback(const std::string& path) {
		if(path.empty()) return nullptr;
		std::shared_ptr<window::Svg> loaded;
		try { loaded = APP->window->loadSvg(path); } catch(...) {}
		if(isValidSvg(loaded)) return loaded;
		return nullptr;
	}

	void setBackgroundPath(const std::string& path) {
		backgroundPath = path;
		backgroundSvg = loadSvgWithoutFallback(backgroundPath);
		frameVersion++;
	}

	static const std::vector<std::string>& playModeKeys() {
		static const std::vector<std::string> k = {"forward", "reverse", "pingPong", "random"};
		return k;
	}
	static PlayMode playModeFromKey(const std::string& key) {
		const std::vector<std::string>& keys = playModeKeys();
		for(int i = 0; i < (int)keys.size(); i++) if(keys[i] == key) return (PlayMode)i;
		return PLAY_FORWARD;
	}
	std::string playModeKey() const { return playModeKeys()[playMode]; }

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_t* framePathsJ = json_array();
		for(const std::string& path : framePaths) {
			json_array_append_new(framePathsJ, json_string(path.c_str()));
		}
		json_object_set_new(rootJ, "framePaths", framePathsJ);
		json_object_set_new(rootJ, "currentFrameIndex", json_integer(currentFrameIndex));
		json_object_set_new(rootJ, "playMode", json_string(playModeKey().c_str()));
		json_object_set_new(rootJ, "pingDirection", json_integer(pingDirection));
		json_object_set_new(rootJ, "randomCycleCounter", json_integer(randomCycleCounter));
		json_object_set_new(rootJ, "selectedFramePath", json_string(selectedFramePath.c_str()));
		json_object_set_new(rootJ, "backgroundPath", json_string(backgroundPath.c_str()));
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
		std::string loadedSelectedFramePath;
		json_t* selectedFramePathJ = json_object_get(rootJ, "selectedFramePath");
		if(selectedFramePathJ && json_is_string(selectedFramePathJ)) loadedSelectedFramePath = json_string_value(selectedFramePathJ);
		setFrames(loadedPaths, loadedFrameIndex, loadedSelectedFramePath);
		json_t* playModeJ = json_object_get(rootJ, "playMode");
		if(playModeJ && json_is_string(playModeJ)) playMode = playModeFromKey(json_string_value(playModeJ));
		else playMode = PLAY_FORWARD;
		json_t* pingDirectionJ = json_object_get(rootJ, "pingDirection");
		if(pingDirectionJ && json_is_integer(pingDirectionJ)) pingDirection = json_integer_value(pingDirectionJ);
		if(pingDirection >= 0) pingDirection = 1;
		else pingDirection = -1;
		json_t* randomCycleCounterJ = json_object_get(rootJ, "randomCycleCounter");
		if(randomCycleCounterJ && json_is_integer(randomCycleCounterJ)) randomCycleCounter = json_integer_value(randomCycleCounterJ);
		if(randomCycleCounter < 0) randomCycleCounter = 0;
		int n = (int)framePaths.size();
		if(n > 1 && randomCycleCounter >= n) randomCycleCounter %= n;
		if(n <= 1) randomCycleCounter = 0;
		json_t* backgroundPathJ = json_object_get(rootJ, "backgroundPath");
		if(backgroundPathJ && json_is_string(backgroundPathJ)) backgroundPath = json_string_value(backgroundPathJ);
		backgroundSvg = loadSvgWithoutFallback(backgroundPath);
	}

	void resetPlaybackToFirstFrame() {
		if(framePaths.empty()) return;
		currentFrameIndex = 0;
		pingDirection = 1;
		randomCycleCounter = 0;
		frameVersion++;
	}

	void process(const ProcessArgs& args) override {
		bool fireEoc = false;
		bool didReset = false;
		if(rstTrigger.process(inputs[RST_INPUT].getVoltage())) {
			resetPlaybackToFirstFrame();
			didReset = true;
		}
		if(!didReset && clkTrigger.process(inputs[CLK_INPUT].getVoltage())) {
			int n = (int)framePaths.size();
			if(n > 0) {
				if(currentFrameIndex < 0) currentFrameIndex = 0;
				if(currentFrameIndex >= n) currentFrameIndex = n - 1;
			}
			if(n <= 1) {
				fireEoc = true;
			}
			else {
				int previousIndex = currentFrameIndex;
				if(playMode == PLAY_FORWARD) {
					currentFrameIndex++;
					if(currentFrameIndex >= n) currentFrameIndex = 0;
					if(previousIndex == n - 1 && currentFrameIndex == 0) fireEoc = true;
				}
				else if(playMode == PLAY_REVERSE) {
					currentFrameIndex--;
					if(currentFrameIndex < 0) currentFrameIndex = n - 1;
					if(previousIndex == 0 && currentFrameIndex == n - 1) fireEoc = true;
				}
				else if(playMode == PLAY_PING_PONG) {
					currentFrameIndex += pingDirection;
					if(currentFrameIndex >= n) {
						currentFrameIndex = n - 2;
						pingDirection = -1;
					}
					else if(currentFrameIndex < 0) {
						currentFrameIndex = 1;
						pingDirection = 1;
					}
					if(previousIndex == 1 && currentFrameIndex == 0) fireEoc = true;
				}
				else if(playMode == PLAY_RANDOM) {
					currentFrameIndex = (int)std::floor(random::uniform() * n);
					if(currentFrameIndex >= n) currentFrameIndex = n - 1;
					randomCycleCounter++;
					if(randomCycleCounter >= n) {
						randomCycleCounter = 0;
						fireEoc = true;
					}
				}
				frameVersion++;
			}
		}
		if(fireEoc) eocPulse.trigger(1e-3f);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);
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
		if(module && module->backgroundSvg) {
			math::Vec size = module->backgroundSvg->getSize();
			float x = std::round((box.size.x - size.x) * 0.5f);
			float y = std::round((box.size.y - size.y) * 0.5f);
			nvgSave(args.vg);
			nvgTranslate(args.vg, x, y);
			module->backgroundSvg->draw(args.vg);
			nvgRestore(args.vg);
		}
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
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(30.48f, 116.0f)), module, SticksyFlipbookModule::RST_INPUT));
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
					std::string entryFilename = system::getFilename(entry);
					if(!hasSvgExtension(entryFilename)) continue;
					std::string stem = stripFinalSvgExtension(entryFilename);
					std::string entryPrefix;
					int value = 0;
					if(!parseFinalNumberSuffix(stem, &entryPrefix, &value)) continue;
					if(entryPrefix != prefix) continue;
					std::string framePath = entry;
					if(system::getDirectory(entry).empty()) framePath = system::join(system::getDirectory(selectedPath), entryFilename);
					matches.push_back(std::make_pair(value, framePath));
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
				std::string initialPath;
				if(firstFlipbookImageBrowserOpen) initialPath = prepareSticksyBrowserInitialPath(sticksyUserAnimationLibraryPath());
				firstFlipbookImageBrowserOpen = false;
				char* pathC = osdialog_file(OSDIALOG_OPEN, initialPath.empty() ? NULL : initialPath.c_str(), NULL, filters);
				osdialog_filters_free(filters);
				if(!pathC) return;
				std::string selectedPath = pathC;
				std::free(pathC);
				if(selectedPath.empty() || !hasSvgExtension(selectedPath)) return;
				SticksyFlipbookModule* m = module;
				std::vector<std::string> newPaths;
				int selectedFrameIndex = 0;
				detectSequencePaths(selectedPath, &newPaths, &selectedFrameIndex);
				pushFlipbookModuleChange(module, "load Sticksy Flipbook image", [m, newPaths, selectedFrameIndex, selectedPath]() { m->setFrames(newPaths, selectedFrameIndex, selectedPath); });
			}
		};
		auto* load = createMenuItem<LoadFlipbookImageItem>("Load Flipbook Image...");
		load->module = module;
		menu->addChild(load);
		struct LoadBackgroundImageItem : MenuItem {
			SticksyFlipbookModule* module;
			void onAction(const event::Action&) override {
				if(!module) return;
				osdialog_filters* filters = osdialog_filters_parse("Scalable Vector Graphic (.svg):svg");
				std::string initialPath;
				if(firstFlipbookBackgroundBrowserOpen) initialPath = prepareSticksyBrowserInitialPath(sticksyUserStickerLibraryPath());
				firstFlipbookBackgroundBrowserOpen = false;
				char* pathC = osdialog_file(OSDIALOG_OPEN, initialPath.empty() ? NULL : initialPath.c_str(), NULL, filters);
				osdialog_filters_free(filters);
				if(!pathC) return;
				std::string selectedPath = pathC;
				std::free(pathC);
				if(selectedPath.empty() || !hasSvgExtension(selectedPath)) return;
				SticksyFlipbookModule* m = module;
				std::string newPath = selectedPath;
				pushFlipbookModuleChange(module, "load Sticksy Flipbook background", [m, newPath]() { m->setBackgroundPath(newPath); });
			}
		};
		auto* loadBackground = createMenuItem<LoadBackgroundImageItem>("Load Background Image...");
		loadBackground->module = module;
		menu->addChild(loadBackground);
		struct ClearBackgroundItem : MenuItem {
			SticksyFlipbookModule* module;
			void onAction(const event::Action&) override {
				if(!module || module->backgroundPath.empty()) return;
				SticksyFlipbookModule* m = module;
				pushFlipbookModuleChange(module, "clear Sticksy Flipbook background", [m]() { m->setBackgroundPath(""); });
			}
			void step() override {
				MenuItem::step();
				disabled = !module || module->backgroundPath.empty();
			}
		};
		auto* clearBackground = createMenuItem<ClearBackgroundItem>("Clear Background");
		clearBackground->module = module;
		menu->addChild(clearBackground);
		menu->addChild(new MenuSeparator());
		auto* pmLabel = new MenuLabel();
		pmLabel->text = "Play Mode";
		menu->addChild(pmLabel);
		struct PlayModeItem : MenuItem {
			SticksyFlipbookModule* module;
			SticksyFlipbookModule::PlayMode mode;
			void onAction(const event::Action&) override {
				if(!module || module->playMode == mode) return;
				SticksyFlipbookModule* m = module;
				SticksyFlipbookModule::PlayMode nextMode = mode;
				pushFlipbookModuleChange(module, "change Sticksy Flipbook play mode", [m, nextMode]() { m->setPlayMode(nextMode); });
			}
			void step() override {
				MenuItem::step();
				rightText = (module && module->playMode == mode) ? "✔" : "";
			}
		};
		auto* pf = createMenuItem<PlayModeItem>("Forward");
		pf->module = module;
		pf->mode = SticksyFlipbookModule::PLAY_FORWARD;
		menu->addChild(pf);
		auto* pr = createMenuItem<PlayModeItem>("Reverse");
		pr->module = module;
		pr->mode = SticksyFlipbookModule::PLAY_REVERSE;
		menu->addChild(pr);
		auto* pp = createMenuItem<PlayModeItem>("Ping Pong");
		pp->module = module;
		pp->mode = SticksyFlipbookModule::PLAY_PING_PONG;
		menu->addChild(pp);
		auto* px = createMenuItem<PlayModeItem>("Random");
		px->module = module;
		px->mode = SticksyFlipbookModule::PLAY_RANDOM;
		menu->addChild(px);
	}
};

Model* modelSticksyBlank3 = createModel<SticksyBlank3, SticksyBlank3Widget>("SticksyBlank3");
Model* modelSticksyBlank5 = createModel<SticksyBlank5, SticksyBlank5Widget>("SticksyBlank5");
Model* modelSticksyBlank9 = createModel<SticksyBlank9, SticksyBlank9Widget>("SticksyBlank9");
Model* modelSticksyBlank12 = createModel<SticksyBlank12, SticksyBlank12Widget>("SticksyBlank12");
Model* modelSticksyFlipbook = createModel<SticksyFlipbookModule, SticksyFlipbookWidget>("SticksyFlipbook");

void init(Plugin* p) {
	pluginInstance = p;
	ensureDefaultSticksyLibraryInstalled();
	p->addModel(modelSticksyBlank3);
	p->addModel(modelSticksyBlank5);
	p->addModel(modelSticksyBlank9);
	p->addModel(modelSticksyBlank12);
	p->addModel(modelSticksyFlipbook);
}
