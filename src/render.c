#include <gint/gray.h>
#include <gint/defs/util.h>

#include "syscalls.h"
#include "render.h"
#include "defs.h"

const unsigned int camMinX = SCREEN_WIDTH >> 1;
const unsigned int camMaxX = (WORLD_WIDTH << 3) - (SCREEN_WIDTH >> 1);
const unsigned int camMinY = SCREEN_HEIGHT >> 1;
const unsigned int camMaxY = (WORLD_HEIGHT << 3) - (SCREEN_HEIGHT >> 1);

void render(struct World* world, struct Player* player)
{
	extern image_t img_player1, img_cursor;
	int camX = min(max(player->props.x + (player->props.width >> 1), camMinX), camMaxX);
	int camY = min(max(player->props.y + (player->props.height >> 1), camMinY), camMaxY);

//	Translating cam bounds to tile bounds is painful
	unsigned int tileLeftX = max(0, ((camX - (SCREEN_WIDTH >> 1)) >> 3) - 1);
	unsigned int tileRightX = min(WORLD_WIDTH - 1, tileLeftX + (SCREEN_WIDTH >> 3) + 1);
	unsigned int tileTopY = max(0, ((camY - (SCREEN_HEIGHT >> 1)) >> 3) - 1);
	unsigned int tileBottomY = min(WORLD_HEIGHT - 1, tileTopY + (SCREEN_HEIGHT >> 3) + 1);

	Tile* tile;
	const TileData* currTile;
	unsigned int currTileX, currTileY;
	int camOffsetX = (camX - (SCREEN_WIDTH >> 1));
	int camOffsetY = (camY - (SCREEN_HEIGHT >> 1));
	bool marginLeft, marginRight, marginTop, marginBottom;
	int flags;
	int subrectX, subrectY;

//	This probably shouldn't be here but cam positions can't be accessed anywhere else right now
	player->cursorTile.x = (camX + player->cursor.x - (SCREEN_WIDTH >> 1)) >> 3;
	player->cursorTile.y = (camY + player->cursor.y - (SCREEN_HEIGHT >> 1)) >> 3;

	gclear(C_WHITE);

	for(unsigned int y = tileTopY; y <= tileBottomY; y++)
	{
		for(unsigned int x = tileLeftX; x <= tileRightX; x++)
		{
			tile = &world->tiles[y * WORLD_WIDTH + x];
			currTile = &tiles[tile->idx];
			currTileX = (x << 3) - camOffsetX;
			currTileY = (y << 3) - camOffsetY;
			if(currTile->render)
			{
				/* Disable clipping unless it's a block on the edges of the screen.
				This reduces rendering time a bit (edges still need clipping or
				we might crash trying to write outside the VRAM). */
				marginLeft = x - tileLeftX <= 1;
				marginRight = tileRightX - x <= 1;
				marginTop = y - tileTopY <= 1;
				marginBottom = tileBottomY - y <= 1;
				if(marginLeft | marginRight | marginTop | marginBottom)
				{
					flags = DIMAGE_NONE;
				} else
				{
					flags = DIMAGE_NOCLIP;
				}
				if(currTile->hasSpritesheet)
				{
//					Spritesheet layout allows for very fast calculation of the position of the sprite
					subrectX = tile->state % 4;
					subrectX = (subrectX << 3) + subrectX + 1;
					subrectY = tile->state >> 2;
					subrectY = (subrectY << 3) + subrectY + 1;
					gsubimage(currTileX, currTileY, currTile->sprite, subrectX, subrectY, 8, 8, flags);
				}
				else
				{
					gsubimage(currTileX, currTileY, currTile->sprite, 0, 0, 8, 8, flags);
				}
				
			}
		}
	}
	gimage(player->props.x - (camX - (SCREEN_WIDTH >> 1)), player->props.y - (camY - (SCREEN_HEIGHT >> 1)), &img_player1);
	gimage(player->cursor.x, player->cursor.y, &img_cursor);
}