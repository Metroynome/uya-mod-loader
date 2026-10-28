#ifndef TRAINING_GAME_H
#define TRAINING_GAME_H

#include <tamtypes.h>
#include <libuya/player.h>
#include <libuya/math3d.h>
#include <libuya/game.h>

/* Struct offsets describe the EE ABI, including 16-byte VECTOR alignment. */
#define TPS                      (GAME_FPS)
#define TARGET_MOBY_ID           (0x251c)
#define TRAINING_MAX_SPAWNPOINTS (25)
#define MIN_FLOAT_MAGNITUDE      (0.0001)

typedef enum trainingType {
	TRAINING_TYPE_TEST,
	TRAINING_TYPE_FUSION,
	TRAINING_TYPE_MAX
} trainingType_e;

typedef enum trainingAggression {
	TRAINING_AGGRESSION_AGGRO,
	TRAINING_AGGRESSION_AGGRO_NO_DAMAGE,
	TRAINING_AGGRESSION_PASSIVE,
	TRAINING_AGGRESSION_IDLE,
	TRAINING_AGGRESSION_MAX
} trainingAggression_e;

typedef enum targetState {
	TARGET_STATE_IDLE = 0,
	TARGET_STATE_JUMPING = 1 << 0,
	TARGET_STATE_STRAFING = 1 << 1,
} targetState_e;

typedef enum targetEvent {
	TARGET_EVENT_SPAWN,
	TARGET_EVENT_DESTROY,
} targetEvent_e;

typedef struct trainingMapConfig {
	/* 0x00 */ int SpawnPointCount;
	/* 0x10 */ VECTOR SpawnPoints[TRAINING_MAX_SPAWNPOINTS];
	/* 0x1a0 */ VECTOR PlayerSpawnPoint;
} trainingMapConfig_t;

typedef struct reactVars {
	/* 0x00 */ int flags;
	/* 0x04 */ int lastReactFrame;
	/* 0x08 */ Moby* pInfectionMoby;
	/* 0x0c */ float acidDamage;
	/* 0x10 */ char eternalDeathCount;
	/* 0x11 */ char doHotSpotChecks;
	/* 0x12 */ char unchainable;
	/* 0x13 */ char isShielded;
	/* 0x14 */ char state;
	/* 0x15 */ char deathState;
	/* 0x16 */ char deathEffectState;
	/* 0x17 */ char deathStateType;
	/* 0x18 */ signed char knockbackRes;
	/* 0x19 */ char padC;
	/* 0x1a */ char padD;
	/* 0x1b */ char padE;
	/* 0x1c */ char padF;
	/* 0x1d */ char padG;
	/* 0x1e */ char padH;
	/* 0x1f */ char padI;
	/* 0x20 */ float minorReactPercentage;
	/* 0x24 */ float majorReactPercentage;
	/* 0x28 */ float deathHeight;
	/* 0x2c */ float bounceDamp;
	/* 0x30 */ int deadlyHotSpots;
	/* 0x34 */ float curUpGravity;
	/* 0x38 */ float curDownGravity;
	/* 0x3c */ float shieldDamageReduction;
	/* 0x40 */ float damageReductionSameOClass;
	/* 0x44 */ int deathCorn;
	/* 0x48 */ int deathType;
	/* 0x4c */ short int deathSound;
	/* 0x4e */ short int deathSound2;
	/* 0x50 */ float peakFrame;
	/* 0x54 */ float landFrame;
	/* 0x58 */ float drag;
	/* 0x5c */ u16 effectStates;
	/* 0x5e */ u16 effectPrimMask;
	/* 0x60 */ u16 effectTimers[16];
} reactVars_t;

typedef struct trainingTargetMobyPVar {
	/* 0x00 */ struct TargetVars* TargetVarsPtr;
	/* 0x04 */ char _pad0[0x0c];
	/* 0x10 */ struct reactVars* ReactVarsPtr;
	/* 0x14 */ char _pad1[0x08];
	/* 0x1c */ struct MoveVars_V2* MoveVarsPtr;
	/* 0x20 */ char _pad2[0x14];
	/* 0x34 */ struct FlashVars* FlashVarsPtr;
	/* 0x38 */ char _pad3[0x18];
	/* 0x50 */ struct TargetVars TargetVars;
	/* 0xe0 */ struct reactVars reactVars;
	/* 0x160 */ struct MoveVars_V2 MoveVars;
	/* 0x310 */ struct FlashVars FlashVars;
	/* 0x320 */ VECTOR SpawnPos;
	/* 0x330 */ VECTOR Velocity;
	/* 0x340 */ int State;
	/* 0x344 */ u32 LifeTicks;
	/* 0x348 */ int TimeCreated;
	/* 0x34c */ float Jitter;
	/* 0x350 */ float StrafeSpeed;
	/* 0x354 */ float JumpSpeed;
} trainingTargetMobyPVar_t;

typedef struct targetDamageEventArgs {
	/* 0x00 */ int SourceUID;
	/* 0x04 */ u16 SourceOClass;
} targetDamageEventArgs_t;

typedef struct trainingState {
	/* 0x00 */ int InitializedTime;
	/* 0x04 */ int Points;
	/* 0x08 */ int Kills;
	/* 0x0c */ int Hits;
	/* 0x10 */ int ShotsFired;
	/* 0x14 */ int BestCombo;
	/* 0x18 */ int TargetsDestroyed;
	/* 0x1c */ int GameOver;
	/* 0x20 */ int WinningTeam;
	/* 0x24 */ int IsHost;
	/* 0x28 */ enum trainingAggression AggroMode;
	/* 0x2c */ enum trainingType TrainingType;
	/* 0x30 */ int TargetLastSpawnIdx;
	/* 0x34 */ int ComboCounter;
	/* 0x38 */ long TimeLastSpawn;
	/* 0x40 */ long TimeLastKill;
} trainingState_t;

typedef struct simulatedPlayer {
	/* 0x00 */ struct PAD Pad;
	/* 0x5c0 */ struct trainingTargetMobyPVar Vars;
	/* 0x920 */ Player* Player;
	/* 0x924 */ PlayerVTable* vtable;
	/* 0x928 */ int state;
	/* 0x92c */ float currTargetDist;
	/* 0x930 */ float prevTargetDist;
	/* 0x934 */ u32 TicksToRespawn;
	/* 0x938 */ u32 TicksToJump;
	/* 0x93c */ u32 TicksToJumpFor;
	/* 0x940 */ u32 TicksToStrafeSwitch;
	/* 0x944 */ u32 TicksToStrafeQuickSwitchFor;
	/* 0x948 */ u32 TicksToStrafeStop;
	/* 0x94c */ u32 TicksToStrafeStopFor;
	/* 0x950 */ u32 TicksToStrafeStoppedFor;
	/* 0x954 */ u32 TicksToFire;
	/* 0x958 */ u32 TicksFireDelay;
	/* 0x95c */ u32 TicksToCycle;
	/* 0x960 */ u32 TicksToWrench;
	/* 0x964 */ u32 TicksToThrowWrench;
	/* 0x968 */ u32 UsingWrench;
	/* 0x96c */ u32 HasWrenched;
	/* 0x970 */ float LastDistanceToPlayer;
	/* 0x974 */ u32 TicksToAimYaw;
	/* 0x978 */ u32 TicksToAimPitch;
	/* 0x97c */ u32 TicksToTbag;
	/* 0x980 */ u32 TicksAimNearPlayer;
	/* 0x984 */ u32 TicksSinceSeenPlayer;
	/* 0x988 */ u32 TicksToWeaponSwitch;
	/* 0x98c */ int StrafeDir;
	/* 0x990 */ int Idx;
	/* 0x994 */ int CycleIdx;
	/* 0x998 */ int Points;
	/* 0x99c */ float YawOff;
	/* 0x9a0 */ float Yaw;
	/* 0x9a4 */ float YawVel;
	/* 0x9a8 */ float YawAcc;
	/* 0x9ac */ float PitchOff;
	/* 0x9b0 */ float Pitch;
	/* 0x9b4 */ float PitchVel;
	/* 0x9b8 */ float PitchAcc;
	/* 0x9bc */ char Created;
	/* 0x9bd */ char Active;

	// Swingshot fields

	/* 0x9be */ char IsSwinging;
	/* 0x9c0 */ int SwingReleaseTimer;
	/* 0x9c4 */ Moby* SwingshotOrb;
	/* 0x9c8 */ u32 jumpPadSearchCooldown;
	/* 0x9cc */ Moby* targetJumpPad;
	/* 0x9d0 */ float WanderAngle;
	/* 0x9d4 */ u32 TicksToWanderSwitch;
} simulatedPlayer_t;

typedef struct trainingGameData {
	/* 0x00 */ u32 Version;
	/* 0x04 */ int Points;
	/* 0x08 */ int Time;
	/* 0x0c */ int Kills;
	/* 0x10 */ int Hits;
	/* 0x14 */ int Misses;
	/* 0x18 */ int BestCombo;
} trainingGameData_t;

extern trainingState_t State;
extern int BotsOnline;

extern const float TARGET_MIN_SPAWN_DISTANCE[TRAINING_TYPE_MAX];
extern const float TARGET_IDEAL_SPAWN_DISTANCE[TRAINING_TYPE_MAX];
extern const float TARGET_BUFFER_SPAWN_DISTANCE[TRAINING_TYPE_MAX];

extern const char* NAMES[];
extern const int NAMES_COUNT;

extern const char* TRAINING_AGGRO_NAMES[];

/* Implemented by libuya; older installed headers omit these declarations. */
Player** playerGetAll(void);
PlayerVTable* playerGetVTable(Player* player);
void startBots(void);
void botsReset(int multiplayer);
int botsLevelChanged(void);

#endif // TRAINING_GAME_H
