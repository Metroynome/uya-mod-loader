#ifndef UYA_CONFIG_H
#define UYA_CONFIG_H

#include "module.h"

const moduleApi_t* moduleConfigApi(void);
int* moduleConfigBusy(void);
int moduleConfigLoad(const char* mod, const moduleConfigField_t* fields, int count);
int moduleConfigSave(const char* mod, const moduleConfigField_t* fields, int count);
#endif
