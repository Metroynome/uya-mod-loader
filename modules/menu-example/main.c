#include "include/modmenu.h"
static const ModuleApi *loader;
static int registered, enabled, amount=5;
static void action(void *context)
{ (void)context; loader->print("menu-example: action, enabled=%d amount=%d mode=%u\n",enabled,amount,loader->mode); }
int moduleInit(const ModuleApi *api) { loader=api; return 0; }
void moduleUpdate(void)
{
    const ModMenuApi *menu; int tab;
    ModMenuItem toggle={"Example toggle",MODMENU_TOGGLE,&enabled,0,0,0,action,0};
    ModMenuItem range={"Example value",MODMENU_RANGE,&amount,0,10,1,action,0};
    ModMenuItem button={"Print a message",MODMENU_ACTION,0,0,0,0,action,0};
    if(registered) return;
    menu=modmenuFind(loader); if(!menu) return; /* Load-order independent. */
    tab=menu->addTab("menu-example","Example Mod",UYA_MODE_BOTH); if(tab<0) return;
    if(menu->addItem(tab,&toggle)<0 || menu->addItem(tab,&range)<0 || menu->addItem(tab,&button)<0) {
        menu->removeTab(tab); return;
    }
    registered=1;
}
