#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "enums.h"
#include "gpconfig.h"
#include "storagemanager.h"

#include "config_legacy.h"

class ConfigManager {
public:
	ConfigManager(ConfigManager const&) = delete;
	void operator=(ConfigManager const&)  = delete;
    static ConfigManager& getInstance() {// Thread-safe storage ensures cross-thread talk
		static ConfigManager instance; // Guaranteed to be destroyed. // Instantiated on first use.
		return instance;
	}
    void setup(ConfigType);
    void loop(); // If anything needs to update in the gpconfig driver
    void setGamepadOptions(Gamepad*);
    void setBoardOptions(ConfigLegacy::BoardOptions);
    void setPreviewBoardOptions(ConfigLegacy::BoardOptions);
    void setLedOptions(ConfigLegacy::LEDOptions);
    void setSplashImage(const ConfigLegacy::SplashImage&);
private:
    ConfigManager() {}
    void setupConfig(GPConfig*);
    ConfigType cType;
    GPConfig * config;
};

#endif