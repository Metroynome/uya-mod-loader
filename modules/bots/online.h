#ifndef ONLINE_BOTS_H
#define ONLINE_BOTS_H

#include <libuya/player.h>

#ifdef ONLINE_BOTS
extern int BotsNetwork;

int botsNetworkActive(void);
int botsOnlinePublish(int index, Player* player);
int botsOnlinePublished(int index);
void botsOnlineRemove(int index);
void botsOnlineRosterChanged(void);
int botsOnlineReset(void);
void botsOnlinePad(struct PAD* pad);
#else
/* Compile away network hooks when online.c is omitted. */
#define BotsNetwork (0)
#define botsNetworkActive() (0)
#define botsOnlinePublish(index, player) (0)
#define botsOnlinePublished(index) (0)
#define botsOnlineRemove(index) ((void)0)
#define botsOnlineRosterChanged() ((void)0)
#define botsOnlineReset() (0)
#define botsOnlinePad(pad) ((void)0)
#endif

#endif
