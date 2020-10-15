#include <gint/std/stdlib.h>
#include <gint/gray.h>
#include <gint/gint.h>
#include <gint/timer.h>
#include <stdbool.h>
#include <gint/std/string.h>
#include <gint/clock.h>
#include <gint/bfile.h>
#include <gint/hardware.h>

#include "defs.h"
#include "syscalls.h"
#include "world.h"
#include "entity.h"
#include "render.h"
#include "menu.h"
#include "save.h"
#include "crafting.h"
#include "update.h"

// Fixes linker problem for newlib
int __errno = 0;

// Syscalls
const unsigned int sc003B[] = { SCA, SCB, SCE, 0x003B };
const unsigned int sc019F[] = { SCA, SCB, SCE, 0x019F };
const unsigned int sc0236[] = { SCA, SCB, SCE, 0x0236 };
const unsigned int sc042E[] = { SCA, SCB, SCE, 0x042E };

// Global variables
struct SaveData save;
struct World world;
struct Player player;
char versionBuffer[16];

// Governs the 60UPS loop
int frameCallback(volatile int *flag)
{
	*flag = 1;
	return TIMER_CONTINUE;
}

// Checks if the RAM we'll be using to store world tiles works
bool testRAM()
{
// Make sure we don't mess with the RAM on GIII models
#ifndef USE_HEAP
	unsigned int *RAMAddress = (void*)RAM_START;
	unsigned int *RAMTestAddress = (void*)0x88000000;
	unsigned int save = *RAMAddress;
//	Ensures this isn't a 256kB-RAM calc
	if(gint[HWRAM] < 500000) return false;
	*RAMAddress = 0xC0FFEE;
	if(*RAMAddress == 0xC0FFEE && *RAMAddress != *RAMTestAddress)
	{
		*RAMAddress = save;
		return true;
	}
	*RAMAddress = save;
#endif
	return false;
}

void positionPlayerAtWorldMiddle()
{
	int playerX = (WORLD_WIDTH / 2);
	int playerY = 0;
	while(1)
	{
		if(getTile(playerX, playerY).id != TILE_NOTHING)
		{
			playerY = (playerY - 3);
			break;
		}
		playerY++;
	}

	player.props.x = playerX << 3;
	player.props.y = playerY << 3;
}

void gameLoop(volatile int *flag)
{
	int respawnCounter = 0;
	int frames = 0;
	enum UpdateReturnCodes updateRet;
	bool renderThisFrame = true;

	while(true)
	{
		if(!respawnCounter) playerUpdate(frames);

		updateRet = keyboardUpdate();
		if(updateRet == UPDATE_EXIT) break;

		doEntityCycle(frames);
		doSpawningCycle();
		if(player.combat.health <= 0)
		{
			if(respawnCounter == 1)
			{
				positionPlayerAtWorldMiddle();
				player.combat.health = player.maxHealth;
				player.props.xVel = 0;
				player.props.yVel = 0;
				player.props.dropping = false;
				respawnCounter = 0;
				player.combat.currImmuneFrames = player.combat.immuneFrames;
			}
			else if(respawnCounter > 0) respawnCounter--;
			else
			{
				createExplosion(&world.explosion, player.props.x + (player.props.width >> 1), player.props.y + (player.props.height >> 1));
				respawnCounter = 300;
			}
		}
		if(renderThisFrame)
		{
			render();
			dupdate();
			renderThisFrame = false;
		}
		else renderThisFrame = true;

		frames++;
		world.timeTicks++;
		if(world.timeTicks >= DAY_TICKS) world.timeTicks = 0;
		while(!*flag) sleep();
		*flag = 0;
	}
}

// frames increases 60 times per second, take into account
int main(void)
{
	int menuSelect;
	extern font_t font_smalltext;
	int w, h;
	int timer;
	volatile int flag = 0;
	int mediaFree[2];

	save = (struct SaveData){
		.tileDataSize = WORLD_HEIGHT * WORLD_WIDTH * sizeof(Tile),
		.regionsX = REGIONS_X,
		.regionsY = REGIONS_Y,
#ifndef USE_HEAP
		.tileData = (void*)RAM_START,
#else
		.tileData = malloc(WORLD_WIDTH * WORLD_HEIGHT),
#endif
		.regionData = { 0 },
		.error = -99,
	};

#ifndef USE_HEAP
	if(!testRAM()) 
	{
		incompatibleMenu();
		return 1;
	}
#else
	allocCheck(save.tileData);
#endif

//	Makes it easier to see if a world load error occurs
	memset(save.tileData, 0, WORLD_WIDTH * WORLD_HEIGHT * sizeof(Tile));

	srand(RTC_GetTicks());

	dgray(DGRAY_ON);

	menuSelect = mainMenu();
	if(menuSelect == -1) return 1;
	
	player = (struct Player){

		.props = {
			.width = 12, 
			.height = 21
		},

		.combat = {
			.health = 100,
			.alignment = ALIGN_NEUTRAL,
			.immuneFrames = 40
		},

		.cursor = {75, 32},

		.inventory = {
			.getFirstFreeSlot = &getFirstFreeSlot,
			.removeItem = &removeItem,
			.stackItem = &stackItem,
			.tallyItem = &tallyItem,
			.findSlot = &findSlot,
			.getSelected = &getSelected
		},

		.tool = { TOOL_TYPE_NONE },
		.maxHealth = 100,

		.physics = &handlePhysics
	};

	for(int slot = 0; slot < INVENTORY_SIZE; slot++) player.inventory.items[slot] = (Item){ITEM_NULL, 0};

	world = (struct World)
	{
		.tiles = (Tile*)save.tileData,
		.entities = (Entity*)malloc(MAX_ENTITIES * sizeof(Entity)),
		.explosion = {
			.particles = malloc(50 * sizeof(Particle))
		},
		.chests = {
			.chests = malloc(MAX_CHESTS * sizeof(struct Chest)),
			.addChest = &addChest,
			.removeChest = &removeChest,
			.findChest = &findChest
		},

		.placeTile = &placeTile,
		.removeTile = &removeTile,
		
		.spawnEntity = &spawnEntity,
		.removeEntity = &removeEntity,
		.checkFreeEntitySlot = &checkFreeEntitySlot
	};
	allocCheck(world.entities);
	allocCheck(world.explosion.particles);
	allocCheck(world.chests.chests);

//	An entity ID of -1 is considered a free slot
	for(int idx = 0; idx < MAX_ENTITIES; idx++) world.entities[idx] = (Entity){ -1 };

	if(menuSelect == 0) // New game
	{
		generateWorld();
		memset(save.regionData, 1, save.regionsX * save.regionsY);
		player.inventory.items[0] = (Item){ITEM_COPPER_SWORD, 1};
		player.inventory.items[1] = (Item){ITEM_COPPER_PICK, 1};
		world.timeTicks = timeToTicks(8, 15);
	} 
	else if(menuSelect == 1) // Load game
	{
		dgray(DGRAY_OFF);
		dclear(C_WHITE);
		dupdate();

		gint_switch(&getVersionInfo);
		if(strcmp(versionBuffer, VERSION) != 0) saveVersionDifferenceMenu(versionBuffer);

		dclear(C_WHITE);
		dsize("Loading World...", NULL, &w, &h);
		dtext(64 - w / 2, 32 - h / 2, C_BLACK, "Loading World...");
		dupdate();
		gint_switch(&loadSave);
		if(save.error != -99) 
		{
			loadFailMenu();
			return 1;
		}

		dgray(DGRAY_ON);
	}

	dfont(&font_smalltext);

	positionPlayerAtWorldMiddle();

	timer = timer_setup(TIMER_ANY, (1000 / 60) * 1000, &frameCallback, &flag);
	timer_start(timer);

	// Do the game
	gameLoop(&flag);

	timer_stop(timer);
	dgray(DGRAY_OFF);
	dclear(C_WHITE);
	dfont(NULL);
	dsize("Saving World...", NULL, &w, &h);
	dtext(64 - w / 2, 32 - h / 2, C_BLACK, "Saving World...");
	dupdate();

	free(world.entities);
	destroyExplosion(&world.explosion);

	gint_switch(&saveGame);
	free(world.chests.chests);
	if(save.error != -99) saveFailMenu();
	
	Bfile_GetMediaFree_OS(u"\\\\fls0", mediaFree);
	if(mediaFree[1] < 250000) lowSpaceMenu(mediaFree[1]);

#ifdef USE_HEAP
	free(save.tileData);
#endif

	return 1;
}
