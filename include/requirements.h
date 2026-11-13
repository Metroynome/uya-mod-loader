#ifndef LOADER_REQUIREMENTS_H
#define LOADER_REQUIREMENTS_H

#include "module.h"

void moduleRequirementsReset(int extraRam);
u32 moduleRequirementsMissing(u32 required);
void moduleRequirementsNotice(const char* name, u32 required);
void moduleRequirementsTick(void);

#endif
