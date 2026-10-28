#include <tamtypes.h>
#include <string.h>

#include <libuya/time.h>
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/player.h>
#include <libuya/camera.h>
#include <libuya/weapon.h>
#include <libuya/sha1.h>
#include <libuya/collision.h>
#include <libuya/stdio.h>
#include <libuya/graphics.h>
#include <libuya/spawnpoint.h>
#include <libuya/random.h>
#include <libuya/net.h>
#include <libuya/sound.h>
#include <libuya/color.h>
#include <libuya/pad.h>
#include <libuya/uya.h>
#include <libuya/utils.h>
#include <libuya/interop.h>
#include "training.h"

#define MAX_SPAWNED_TARGETS    (TRAINING_MAX_BOTS)
#define TARGET_IDLE            (State.AggroMode == TRAINING_AGGRESSION_IDLE)
#define TARGET_SPAWN_DISTANCE  (6.0f)
#define TARGET_HEALTH          (15)
#define TARGET_RESPAWN_DELAY   (TIME_SECOND * 1)
#define TARGET_POINTS_LIFETIME (TIME_SECOND * 20)
#define TARGET_LIFETIME_TICKS  (TPS * 60)
#define TICKS_TO_RESPAWN       (TPS * 0.5)
#define TIMELIMIT_MINUTES      (5)
#define SIZEOF_PLAYER_OBJECT   (0x4500)
#define TARGET_TEAM            (TEAM_RED)

// Distance-based behavior
// based off of bot to player distance
#define STRAFE_DISTANCE_MIN (4.0f)
#define STRAFE_DISTANCE_MAX (15.0f)
#define WANDER_DISTANCE_MIN (30.0f)
#define WANDER_DISTANCE_MAX (35.0f)

#define JUMP_PAD_SEARCH_DISTANCE (20.0f)
#define HEIGHT_DIFF_FOR_JUMP_PAD (1.0f)

void createSimPlayer(simulatedPlayer_t* sPlayer, int idx);

extern int Initialized;
extern struct trainingMapConfig Config;

int TargetTeam = TARGET_TEAM;

int last_names_idx = 0;
int waiting_for_sniper_shot = 0;

int SimPlayerCount;
simulatedPlayer_t SimPlayers[MAX_SPAWNED_TARGETS];

void modeInitialize(void)
{
	// cheatsApplyNoPacks();
	// cheatsDisableHealthboxes();
}

void modeOnGadgetFired(int gadgetId)
{
	if (gadgetId == GADGET_ID_FLUX) {
		State.ShotsFired += 1;
		waiting_for_sniper_shot = 10;
	}
}

int modeGetJumpTicks(void)
{
	if (rand(2))
		return randRangeInt(TPS * 2, TPS * 5);

	return randRangeInt(5, TPS * 1);
}

//--------------------------------------------------------------------------
void modeOnTargetHit(simulatedPlayer_t* target, MobyColDamage* colDamage)
{
}

//--------------------------------------------------------------------------
void modeOnTargetKilled(simulatedPlayer_t* target, MobyColDamage* colDamage)
{
	target->TicksToRespawn = TICKS_TO_RESPAWN;
	State.TargetsDestroyed += 1;
	target->Active = 0;
}

// Helper function for wrench behavior
int modeUpdateTarget_Wrench(
    simulatedPlayer_t* sPlayer, Player* player, struct padButtonStatus* pad, float approachSpeed, float yaw)
{
	// Check if currently wrenching
	int shouldWrench = (sPlayer->UsingWrench && sPlayer->TicksToThrowWrench > 0);

	if (shouldWrench) {
		// Stop all movement while wrenching
		pad->ljoy_h = 0x7f;
		pad->ljoy_v = 0x7f;

		// Press square to wrench
		pad->btns &= ~PAD_SQUARE;

		return 1; // Return 1 to signal we're wrenching (caller should return early)
	} else if (sPlayer->UsingWrench) {
		// Done wrenching
		sPlayer->UsingWrench = 0;
	}

	// Check if we should switch back to gun after wrenching
	if (sPlayer->HasWrenched && !sPlayer->UsingWrench && sPlayer->currTargetDist >= STRAFE_DISTANCE_MIN) {
		pad->btns &= ~PAD_TRIANGLE;
		sPlayer->HasWrenched = 0;
	}

	// Trigger wrench if player rushing in
	if (sPlayer->currTargetDist <= 2 && approachSpeed > 0.1 && sPlayer->TicksToThrowWrench == 0 &&
	    !sPlayer->HasWrenched) {
		sPlayer->TicksToThrowWrench = 30;
		sPlayer->UsingWrench = 1;
		sPlayer->HasWrenched = 1;
	}

	return 0; // Not currently wrenching
}

// Helper function for aiming
void modeUpdateTarget_Aiming(simulatedPlayer_t* sPlayer, Player* player, VECTOR delta, VECTOR moveTarget)
{
	// Only aim at player if within combat range (not wandering)
	if (sPlayer->currTargetDist <= WANDER_DISTANCE_MIN || sPlayer->currTargetDist > WANDER_DISTANCE_MAX) {
		vector_copy(delta, player->playerPosition);
		vector_subtract(delta, delta, sPlayer->Player->playerPosition);
		delta[2] -= 1.5;

		float horizontalDist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
		float yaw = atan2f(delta[0], delta[1]);
		float len = sqrtf(horizontalDist * horizontalDist + delta[2] * delta[2]);
		float pitch = asinf(-delta[2] / len);

		sPlayer->Yaw = lerpfAngle(sPlayer->Yaw, yaw, 0.5);
		sPlayer->Pitch = pitch;
	} else {
		// In wander range - look in movement direction
		VECTOR moveDir;
		vector_subtract(moveDir, moveTarget, sPlayer->Player->playerPosition);
		moveDir[2] = 0;
		float moveYaw = atan2f(moveDir[0], moveDir[1]);
		sPlayer->Yaw = lerpfAngle(sPlayer->Yaw, moveYaw, 0.3);
		sPlayer->Pitch = 0;
	}

	MATRIX m;
	matrix_unit(m);
	matrix_rotate_y(m, m, sPlayer->Pitch);
	matrix_rotate_z(m, m, sPlayer->Yaw);
	memcpy(&sPlayer->Player->camera->uMtx, m, sizeof(VECTOR) * 3);
	vector_copy(sPlayer->Player->fps.cameraDir, &m[4]);
	sPlayer->Player->fps.vars.cameraY.rotation = sPlayer->Pitch;
	sPlayer->Player->fps.vars.cameraZ.rotation = sPlayer->Yaw;
}

void modeUpdateTarget_HandleJumpPad(
    simulatedPlayer_t* sPlayer, Player* player, Player* target, VECTOR moveTarget, struct padButtonStatus* pad)
{
	Moby* moby;
	VECTOR delta;

	// Decrement cooldown timer
	if (sPlayer->jumpPadSearchCooldown > 0) {
		--sPlayer->jumpPadSearchCooldown;
	}

	// Only search if we don't have a target AND cooldown expired
	if (!sPlayer->targetJumpPad && sPlayer->jumpPadSearchCooldown == 0) {
		// check height distance difference for of player and bot
		float heightDiff = player->playerPosition[2] - sPlayer->Player->playerPosition[2];
		if (heightDiff >= HEIGHT_DIFF_FOR_JUMP_PAD) {
			// check if bot is horizontally close to player
			vector_copy(delta, player->playerPosition);
			vector_subtract(delta, delta, sPlayer->Player->playerPosition);
			delta[2] = 0;
			float horizontalDist = vector_length(delta);

			if (horizontalDist <= 30.0f) {
				// Look for nearby jump pad
				moby = mobyListGetStart();
				Moby* end = mobyListGetEnd();
				while (moby < end) {
					if (moby->oClass == MOBY_ID_JUMP_PAD) {
						// Calculate distance to jump pad
						vector_subtract(delta, moby->position, sPlayer->Player->playerPosition);
						float dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
						if (dist < JUMP_PAD_SEARCH_DISTANCE &&
						    (sPlayer->Player->playerPosition[2] + 0.5f) > moby->position[2]) {
							sPlayer->targetJumpPad = moby;
							break;
						}
					}
					moby++;
				}

				// Set cooldown whether we found one or not
				sPlayer->jumpPadSearchCooldown = TPS * 5; // Search every 5s
			}
		}
	}

	// Use the jump pad if we have one
	if (sPlayer->targetJumpPad) {
		vector_copy(moveTarget, sPlayer->targetJumpPad->position);

		if (sPlayer->Player->mobys.ground == sPlayer->targetJumpPad) {
			pad->btns &= ~PAD_CROSS;
			sPlayer->targetJumpPad = NULL;
		}
	}
}

void modeUpdateTarget_CalculateMovement(simulatedPlayer_t* sPlayer,
                                        Player* player,
                                        Player* target,
                                        VECTOR moveTarget,
                                        float distanceToPlayer,
                                        float yaw,
                                        float approachSpeed)
{
	if (distanceToPlayer > WANDER_DISTANCE_MAX) {
		sPlayer->TicksToJumpFor = 0;
		vector_copy(moveTarget, player->playerPosition);
	} else if (distanceToPlayer > WANDER_DISTANCE_MIN) {
		// Wander - walk in random direction
		if (sPlayer->TicksToWanderSwitch == 0) {
			sPlayer->WanderAngle = randRangeFloat(0, MATH_TAU);
			sPlayer->TicksToWanderSwitch = randRangeInt(TPS * 2, TPS * 4);
		}
		sPlayer->TicksToWanderSwitch--;

		vector_fromyaw(moveTarget, sPlayer->WanderAngle);
		vector_scale(moveTarget, moveTarget, 10);
		vector_add(moveTarget, target->playerPosition, moveTarget);
	} else if (distanceToPlayer >= STRAFE_DISTANCE_MAX) {
		// Between strafe and wander - walk toward player
		sPlayer->TicksToJumpFor = 0;
		vector_copy(moveTarget, player->playerPosition);
	} else if (distanceToPlayer < STRAFE_DISTANCE_MIN) {
		// Too close - back up
		vector_fromyaw(moveTarget, yaw + MATH_PI);
		vector_scale(moveTarget, moveTarget, 3);
		vector_add(moveTarget, target->playerPosition, moveTarget);
	}
	// Note: strafe range (STRAFE_DISTANCE_MIN to STRAFE_DISTANCE_MAX) uses pure strafe logic
}

void modeUpdateTarget_ApplyInput(struct padButtonStatus* pad, float forwardInput, float strafeInput, float yaw)
{
	MATRIX m;
	matrix_unit(m);
	matrix_rotate_z(m, m, -yaw);

	VECTOR input = {forwardInput, strafeInput, 0, 0};
	vector_apply(input, input, m);

	pad->ljoy_h = (u8)(((clamp(-input[1], -1, 1) + 1) / 2) * 255);
	pad->ljoy_v = (u8)(((clamp(-input[0], -1, 1) + 1) / 2) * 255);
}

void modeUpdateTarget_Firing(simulatedPlayer_t* sPlayer, struct padButtonStatus* pad)
{
	if (State.AggroMode != TRAINING_AGGRESSION_PASSIVE)
		pad->btns &= ~PAD_R1;
}

void modeUpdateTarget_WeaponCycling(simulatedPlayer_t* sPlayer, struct padButtonStatus* pad)
{
	if (sPlayer->TicksToWeaponSwitch > 0) {
		sPlayer->TicksToWeaponSwitch--;
		return;
	}
	pad->btns &= ~PAD_TRIANGLE;
	sPlayer->TicksToWeaponSwitch = randRangeInt(TPS * 3, TPS * 8); // Switch every 3-8 seconds
}

void modeUpdateTarget(simulatedPlayer_t* sPlayer)
{
	VECTOR delta;
	Player* player = playerGetFromSlot(0);
	if (!sPlayer || !player || !sPlayer->Active)
		return;

	struct padButtonStatus* pad = (struct padButtonStatus*)sPlayer->Pad.rdata;
	Player* target = sPlayer->Player;
	struct trainingTargetMobyPVar* pvar = &sPlayer->Vars;
	if (!pvar)
		return;

	/* The simulated pad is updated by bots.c; do not overwrite boot options. */
	sPlayer->state = playerGetState(sPlayer->Player);

	if (TARGET_IDLE == true)
		return;

	// Decrement timers
	if (sPlayer->TicksToJump > 0)
		sPlayer->TicksToJump--;
	if (sPlayer->TicksToJumpFor > 0)
		sPlayer->TicksToJumpFor--;
	if (sPlayer->TicksToStrafeSwitch > 0)
		sPlayer->TicksToStrafeSwitch--;
	if (sPlayer->TicksToStrafeStop > 0)
		sPlayer->TicksToStrafeStop--;
	if (sPlayer->TicksToStrafeStopFor > 0)
		sPlayer->TicksToStrafeStopFor--;
	if (sPlayer->TicksToThrowWrench > 0)
		sPlayer->TicksToThrowWrench--;

	// Calculate distance and approach speed
	vector_subtract(delta, player->playerPosition, target->playerPosition);
	delta[2] -= 1.5;
	sPlayer->currTargetDist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	float approachSpeed = sPlayer->prevTargetDist - sPlayer->currTargetDist;
	sPlayer->prevTargetDist = sPlayer->currTargetDist;
	float yaw = atan2f(delta[0], delta[1]);

	// Ledge grab check
	if (sPlayer->state >= 23 && sPlayer->state <= 26)
		sPlayer->vtable->UpdateState(sPlayer->Player, PLAYER_STATE_LEDGE_JUMP, 1, 1, 0);

	// Wrench check
	if (State.AggroMode != TRAINING_AGGRESSION_PASSIVE &&
	    modeUpdateTarget_Wrench(sPlayer, player, pad, approachSpeed, yaw))
		return;

	VECTOR moveTarget;
	int inStrafeRange =
	    (sPlayer->currTargetDist >= STRAFE_DISTANCE_MIN && sPlayer->currTargetDist < STRAFE_DISTANCE_MAX);

	// Jump pad handling
	modeUpdateTarget_HandleJumpPad(sPlayer, player, target, moveTarget, pad);
	if (sPlayer->targetJumpPad) {
		vector_subtract(delta, moveTarget, sPlayer->Player->playerPosition);
		delta[2] = 0;
		vector_normalize(delta, delta);

		modeUpdateTarget_ApplyInput(pad, delta[0], delta[1], yaw);
		return;
	}

	// Calculate movement target for non-strafe ranges
	if (!inStrafeRange || playerGetHealth(player) <= 0) {
		modeUpdateTarget_CalculateMovement(
		    sPlayer, player, target, moveTarget, sPlayer->currTargetDist, yaw, approachSpeed);
		modeUpdateTarget_Aiming(sPlayer, player, delta, moveTarget);

		// Calculate movement to target
		vector_subtract(delta, moveTarget, target->playerPosition);
		delta[2] = 0;
		vector_normalize(delta, delta);

		// Strafe stop logic
		if (sPlayer->TicksToStrafeStop == 0) {
			sPlayer->TicksToStrafeStop = randRangeInt(TPS * 2, TPS * 5);
			sPlayer->TicksToStrafeStopFor = randRangeInt(5, 30);
		}
		if (sPlayer->TicksToStrafeStopFor > 0) {
			delta[0] = delta[1] = 0;
		}

		modeUpdateTarget_ApplyInput(pad, delta[0] * 1.5, delta[1] * 1.5, yaw);
	} else {
		// IN STRAFE RANGE
		modeUpdateTarget_Aiming(sPlayer, player, delta, moveTarget);

		// Switch strafe direction on timer
		if (sPlayer->TicksToStrafeSwitch == 0) {
			sPlayer->StrafeDir = !sPlayer->StrafeDir;
			sPlayer->TicksToStrafeSwitch = randRangeInt(TPS * 1, TPS * 3);
		}

		// Pure strafe input (full left or full right)
		float strafeInput = sPlayer->StrafeDir ? -1.0f : 1.0f;

		modeUpdateTarget_ApplyInput(pad, 0, strafeInput, yaw);

		// Weapon firing and cycling (only when strafing)
		modeUpdateTarget_Firing(sPlayer, pad);
		modeUpdateTarget_WeaponCycling(sPlayer, pad);
	}

	// Jump logic (applies to all ranges)
	if (sPlayer->TicksToJump == 0) {
		sPlayer->TicksToJump = modeGetJumpTicks();
		sPlayer->TicksToJumpFor = 3;
	}
	if (sPlayer->TicksToJumpFor > 0) {
		pad->btns &= ~PAD_CROSS;
	}
}

void modeInitTarget(simulatedPlayer_t* sPlayer)
{
	GameSettings* gs = gameGetSettings();

	// init vars
	struct trainingTargetMobyPVar* pvar = &sPlayer->Vars;

	pvar->State = TARGET_STATE_IDLE;
	pvar->TimeCreated = gameGetTime();
	pvar->LifeTicks = TARGET_LIFETIME_TICKS;
	pvar->Jitter = 0;
	pvar->JumpSpeed = 5;
	pvar->StrafeSpeed = 5;
	vector_write(pvar->Velocity, 0);

	sPlayer->Active = 1;
	sPlayer->TicksToJump = modeGetJumpTicks();
	playerSetHealth(sPlayer->Player, TARGET_HEALTH);
	vector_copy(pvar->SpawnPos, sPlayer->Player->playerPosition);
	sPlayer->Points = 0;

	// set player name
	sprintf(gs->PlayerNames[sPlayer->Player->mpIndex], "Fake %d", sPlayer->Idx);
}

void modeProcessPlayer(int pIndex)
{
}

void modeTick(void)
{
	/* Original experimental flyby/timer patches were tied to one overlay. */
}
