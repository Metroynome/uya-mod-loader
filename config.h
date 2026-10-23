#ifndef UYA_CONFIG_H
#define UYA_CONFIG_H

#include "module.h"

const ModuleApi* moduleConfigApi(void);
int* moduleConfigBusy(void);
int moduleConfigLoad(const char* mod, const ModuleConfigField* fields, int count);
int moduleConfigSave(const char* mod, const ModuleConfigField* fields, int count);
#endif
