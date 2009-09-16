#ifndef __CCONFIGHANDLER_H__
#define __CCONFIGHANDLER_H__
#include "../global.h"
class CAdvMapInt;

/*
 * CConfighandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

namespace config
{
	struct ClientConfig
	{
		int resx, resy, bpp, fullscreen; //client resolution/colours
		int port, localInformation;
		std::string server, //server address (e.g. 127.0.0.1)
			defaultAI; //dll name
	};
	struct ButtonInfo
	{
		std::string defName;
		std::vector<std::string> additionalDefs;
		int x, y; //position on the screen
		bool playerColoured; //if true button will be colored to main player's color (works properly only for appropriate 8bpp graphics)
	};
	struct AdventureMapConfig
	{
		//minimap properties
		int minimapX, minimapY, minimapW, minimapH;
		//statusbar
		int statusbarX, statusbarY; //pos
		std::string statusbarG; //graphic name
		//resdatabar
		int resdatabarX, resdatabarY, resDist, resDateDist, resOffsetX, resOffsetY; //pos
		std::string resdatabarG; //graphic name
		//infobox
		int infoboxX, infoboxY;
		//advmap
		int advmapX, advmapY, advmapW, advmapH;
		bool smoothMove;
		bool puzzleSepia;
		//general properties
		std::string mainGraphic;
		//buttons
		ButtonInfo kingOverview, underground, questlog,	sleepWake, moveHero, spellbook,	advOptions,
			sysOptions,	nextHero, endTurn;
		//hero list
		int hlistX, hlistY, hlistSize;
		std::string hlistMB, hlistMN, hlistAU, hlistAD;
		//town list
		int tlistX, tlistY, tlistSize;
		std::string tlistAU, tlistAD;
		//gems
		int gemX[4], gemY[4];
		std::vector<std::string> gemG;
	};
	struct GUIOptions
	{
		AdventureMapConfig ac;
	};
	class CConfigHandler
	{
	public:
		ClientConfig cc;
		std::map<std::pair<int,int>, GUIOptions > guiOptions;
		GUIOptions *go(); //return pointer to gui options appropriate for used screen resolution
		void init();
		CConfigHandler(void); //c-tor
		~CConfigHandler(void); //d-tor
	};
}
extern config::CConfigHandler conf;

#endif // __CCONFIGHANDLER_H__