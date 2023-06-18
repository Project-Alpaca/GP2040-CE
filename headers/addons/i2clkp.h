#ifndef _I2CLKP_H
#define _I2CLKP_H

#include <BitBang_I2C.h>

#include "gpaddon.h"
#include "GamepadEnums.h"

#define I2CLKPName "I2CLKP"

class I2CLKPInput : public GPAddon {
	public:
	virtual bool available();
	virtual void setup();
	virtual void preprocess() {}
	virtual void process();
	virtual std::string name() { return I2CLKPName; }

	private:
	BBI2C i2c;
};

#endif // _I2CLKP_H
