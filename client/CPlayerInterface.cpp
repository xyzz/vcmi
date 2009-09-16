#include "../stdafx.h"
#include "CAdvmapInterface.h"
#include "CBattleInterface.h"
#include "../CCallback.h"
#include "CCastleInterface.h"
#include "CCursorHandler.h"
#include "CGameInfo.h"
#include "CHeroWindow.h"
#include "CMessage.h"
#include "CPlayerInterface.h"
//#include "SDL_Extensions.h"
#include "SDL_Extensions.h"
//#include "SDL_framerate.h"

#include "SDL_framerate.h"
#include "CConfigHandler.h"
#include "CCreatureAnimation.h"
#include "Graphics.h"
#include "../hch/CArtHandler.h"
#include "../hch/CGeneralTextHandler.h"
#include "../hch/CHeroHandler.h"
#include "../hch/CLodHandler.h"
#include "../hch/CObjectHandler.h"
#include "../lib/Connection.h"
#include "../hch/CSpellHandler.h"
#include "../hch/CTownHandler.h"
#include "../lib/CondSh.h"
#include "../lib/NetPacks.h"
#include "../lib/map.h"
#include "../mapHandler.h"
#include "../timeHandler.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/assign/std/vector.hpp> 
#include <boost/assign/list_of.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <cmath>
#include <queue>
#include <sstream>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/*
 * CPlayerInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

using namespace boost::assign;
using namespace CSDL_Ext;

void processCommand(const std::string &message, CClient *&client);

extern std::queue<SDL_Event*> events;
extern boost::mutex eventsM;

CPlayerInterface * LOCPLINT;
enum  EMoveState {STOP_MOVE, WAITING_MOVE, CONTINUE_MOVE, DURING_MOVE};
CondSh<EMoveState> stillMoveHero; //used during hero movement

struct OCM_HLP_CGIN
{
	bool inline operator ()(const std::pair<const CGObjectInstance*,SDL_Rect>  & a, const std::pair<const CGObjectInstance*,SDL_Rect> & b) const
	{
		return (*a.first)<(*b.first);
	}
} ocmptwo_cgin ;



CPlayerInterface::CPlayerInterface(int Player, int serial)
{
	GH.defActionsDef = 0;
	LOCPLINT = this;
	curAction = NULL;
	playerID=Player;
	serialID=serial;
	human=true;
	castleInt = NULL;
	adventureInt = NULL;
	battleInt = NULL;
	pim = new boost::recursive_mutex;
	makingTurn = false;
	showingDialog = new CondSh<bool>(false);
	sysOpts = GDefaultOptions;
	//initializing framerate keeper
	mainFPSmng = new FPSmanager;
	SDL_initFramerate(mainFPSmng);
	SDL_setFramerate(mainFPSmng, 48);
	//framerate keeper initialized
	cingconsole = new CInGameConsole;
}
CPlayerInterface::~CPlayerInterface()
{
	delete pim;
	delete showingDialog;
	delete mainFPSmng;
	delete adventureInt;
	delete cingconsole;

	for(std::map<int,SDL_Surface*>::iterator i=graphics->heroWins.begin(); i!= graphics->heroWins.end(); i++)
		SDL_FreeSurface(i->second);
	for(std::map<int,SDL_Surface*>::iterator i=graphics->townWins.begin(); i!= graphics->townWins.end(); i++)
		SDL_FreeSurface(i->second);
}
void CPlayerInterface::init(ICallback * CB)
{
	cb = dynamic_cast<CCallback*>(CB);
	adventureInt = new CAdvMapInt(playerID);
	std::vector<const CGTownInstance*> tt = cb->getTownsInfo(false);
	for(int i=0;i<tt.size();i++)
	{
		SDL_Surface * pom = infoWin(tt[i]);
		graphics->townWins.insert(std::pair<int,SDL_Surface*>(tt[i]->id,pom));
	}
	recreateWanderingHeroes();
}
void CPlayerInterface::yourTurn()
{
	try
	{
		LOCPLINT = this;
		makingTurn = true;

		static bool firstCall = true;
		static int autosaveCount = 0;

		if(firstCall)
			firstCall = false;
		else
			LOCPLINT->cb->save("Autosave_" + boost::lexical_cast<std::string>(autosaveCount++ + 1));

		autosaveCount %= 5;

		for(std::map<int,SDL_Surface*>::iterator i=graphics->heroWins.begin(); i!=graphics->heroWins.end();i++) //redraw hero infoboxes
			SDL_FreeSurface(i->second);
		graphics->heroWins.clear();
		std::vector <const CGHeroInstance *> hh = cb->getHeroesInfo(false);
		for(int i=0;i<hh.size();i++)
		{
			SDL_Surface * pom = infoWin(hh[i]);
			graphics->heroWins.insert(std::pair<int,SDL_Surface*>(hh[i]->subID,pom));
		}

		/* TODO: This isn't quite right. First day in game should play
		 * NEWDAY. And we don't play NEWMONTH. */
		int day = cb->getDate(1);
		if (day != 1)
			CGI->soundh->playSound(soundBase::newDay);
		else
			CGI->soundh->playSound(soundBase::newWeek);

		adventureInt->infoBar.newDay(day);

		//select first hero if available.
		//TODO: check if hero is slept
		if(wanderingHeroes.size())
			adventureInt->select(wanderingHeroes[0]);
		else
			adventureInt->select(adventureInt->townList.items[0]);

		adventureInt->showAll(screen);
		GH.pushInt(adventureInt);
		adventureInt->activateKeys();

		while(makingTurn) // main loop
		{
			pim->lock();

			//if there are any waiting dialogs, show them
			if(dialogs.size() && !showingDialog->get())
			{
				showingDialog->set(true);
				GH.pushInt(dialogs.front());
				dialogs.pop_front();
			}

			GH.updateTime();
			GH.handleEvents();

			if(!adventureInt->active && adventureInt->scrollingDir) //player forces map scrolling though interface is disabled
				GH.totalRedraw();
			else
				GH.simpleRedraw();

			CGI->curh->draw1();
			CSDL_Ext::update(screen);
			CGI->curh->draw2();
			pim->unlock();
			SDL_framerateDelay(mainFPSmng);
		}

		adventureInt->deactivateKeys();
		GH.popInt(adventureInt);

		cb->endTurn();
	} HANDLE_EXCEPTION
}

inline void subRect(const int & x, const int & y, const int & z, const SDL_Rect & r, const int & hid)
{
	TerrainTile2 & hlp = CGI->mh->ttiles[x][y][z];
	for(int h=0; h<hlp.objects.size(); ++h)
		if(hlp.objects[h].first->id==hid)
		{
			hlp.objects[h].second = r;
			return;
		}
}

inline void delObjRect(const int & x, const int & y, const int & z, const int & hid)
{
	TerrainTile2 & hlp = CGI->mh->ttiles[x][y][z];
	for(int h=0; h<hlp.objects.size(); ++h)
		if(hlp.objects[h].first->id==hid)
		{
			hlp.objects.erase(hlp.objects.begin()+h);
			return;
		}
}
void CPlayerInterface::heroMoved(const TryMoveHero & details)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	const CGHeroInstance * ho = cb->getHeroInfo(details.id); //object representing this hero

	adventureInt->centerOn(ho->pos); //actualizing screen pos
	adventureInt->minimap.draw(screen2);
	adventureInt->heroList.draw(screen2);

	if(details.result == TryMoveHero::TELEPORTATION	||  details.start == details.end)
		return;

	int3 hp = details.start;

	if(makingTurn  &&  ho->tempOwner == playerID) //we are moving our hero
	{
		if (details.result != TryMoveHero::SUCCESS && details.result != TryMoveHero::FAILED) //hero didn't change tile but visit succeeded
		{
			adventureInt->paths.erase(ho);
			adventureInt->terrain.currentPath = NULL;
		}
		else if(adventureInt->terrain.currentPath) //&& hero is moving
		{
			//remove one node from the path (the one we went)
			adventureInt->terrain.currentPath->nodes.erase(adventureInt->terrain.currentPath->nodes.end()-1);
			if(!adventureInt->terrain.currentPath->nodes.size())  //if it was the last one, remove entire path
			{
				adventureInt->paths.erase(ho);
				adventureInt->terrain.currentPath = NULL;
			}
		}
	}

	if (details.result != TryMoveHero::SUCCESS) //hero failed to move
	{
		ho->isStanding = true;
		stillMoveHero.setn(STOP_MOVE);
		GH.totalRedraw();
		return;
	}

	//initializing objects and performing first step of move
	if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
	{
		//ho->moveDir = 1;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, -31)));
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 1, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 33, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 65, -31)));

		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 1)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, 1), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 33)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 33), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
	{
		//ho->moveDir = 2;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 0, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 32, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 64, -31)));

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, 1), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 33), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
	{
		//ho->moveDir = 3;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -1, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 31, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 63, -31)));
		CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, -31)));

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, 1), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 1)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 33), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 33)));

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
	{
		//ho->moveDir = 4;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, 0), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 0)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 32), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 32)));

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
	{
		//ho->moveDir = 5;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, -1), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, -1)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 31), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 31)));

		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -1, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 31, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 63, 63)));
		CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
	{
		//ho->moveDir = 6;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, -1), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 31), ho->id);

		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 0, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 32, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 64, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
	{
		//ho->moveDir = 7;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, -1)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, -1), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 31)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 31), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 63)));
		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 1, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 33, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 65, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
	{
		//ho->moveDir = 8;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 0)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, 0), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 32)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 32), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	//first initializing done
	SDL_framerateDelay(mainFPSmng); // after first move
	//main moving
	for(int i=1; i<32; i+=2*sysOpts.heroMoveSpeed)
	{
		if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
		{
			//setting advmap shift
			adventureInt->terrain.moveX = i-32;
			adventureInt->terrain.moveY = i-32;

			subRect(hp.x-3, hp.y-2, hp.z, genRect(32, 32, -31+i, -31+i), ho->id);
			subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, 1+i, -31+i), ho->id);
			subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 33+i, -31+i), ho->id);
			subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 65+i, -31+i), ho->id);

			subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, 1+i), ho->id);
			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, 1+i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, 1+i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, 1+i), ho->id);

			subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 33+i), ho->id);
			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 33+i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 33+i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 33+i), ho->id);
		}
		else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
		{
			//setting advmap shift
			adventureInt->terrain.moveY = i-32;

			subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, 0, -31+i), ho->id);
			subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 32, -31+i), ho->id);
			subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 64, -31+i), ho->id);

			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, 1+i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, 1+i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, 1+i), ho->id);

			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 33+i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 33+i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 33+i), ho->id);
		}
		else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
		{
			//setting advmap shift
			adventureInt->terrain.moveX = -i+32;
			adventureInt->terrain.moveY = i-32;

			subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, -1-i, -31+i), ho->id);
			subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 31-i, -31+i), ho->id);
			subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 63-i, -31+i), ho->id);
			subRect(hp.x+1, hp.y-2, hp.z, genRect(32, 32, 95-i, -31+i), ho->id);

			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, 1+i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, 1+i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, 1+i), ho->id);
			subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, 1+i), ho->id);

			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 33+i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 33+i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 33+i), ho->id);
			subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 33+i), ho->id);
		}
		else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
		{
			//setting advmap shift
			adventureInt->terrain.moveX = -i+32;

			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, 0), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, 0), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, 0), ho->id);
			subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, 0), ho->id);

			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 32), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 32), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 32), ho->id);
			subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 32), ho->id);
		}
		else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
		{
			
			//setting advmap shift
			adventureInt->terrain.moveX = -i+32;
			adventureInt->terrain.moveY = -i+32;

			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, -1-i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, -1-i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, -1-i), ho->id);
			subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, -1-i), ho->id);

			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 31-i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 31-i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 31-i), ho->id);
			subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 31-i), ho->id);

			subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, -1-i, 63-i), ho->id);
			subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 31-i, 63-i), ho->id);
			subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 63-i, 63-i), ho->id);
			subRect(hp.x+1, hp.y+1, hp.z, genRect(32, 32, 95-i, 63-i), ho->id);
		}
		else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
		{
			//setting advmap shift
			adventureInt->terrain.moveY = -i+32;

			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, -1-i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, -1-i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, -1-i), ho->id);

			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 31-i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 31-i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 31-i), ho->id);

			subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, 0, 63-i), ho->id);
			subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 32, 63-i), ho->id);
			subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 64, 63-i), ho->id);
		}
		else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
		{
			//setting advmap shift
			adventureInt->terrain.moveX = i-32;
			adventureInt->terrain.moveY = -i+32;

			subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, -1-i), ho->id);
			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, -1-i), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, -1-i), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, -1-i), ho->id);

			subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 31-i), ho->id);
			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 31-i), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 31-i), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 31-i), ho->id);

			subRect(hp.x-3, hp.y+1, hp.z, genRect(32, 32, -31+i, 63-i), ho->id);
			subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, 1+i, 63-i), ho->id);
			subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 33+i, 63-i), ho->id);
			subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 65+i, 63-i), ho->id);
		}
		else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
		{
			//setting advmap shift
			adventureInt->terrain.moveX = i-32;

			subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, 0), ho->id);
			subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, 0), ho->id);
			subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, 0), ho->id);
			subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, 0), ho->id);

			subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 32), ho->id);
			subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 32), ho->id);
			subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 32), ho->id);
			subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 32), ho->id);
		}
		adventureInt->updateScreen = true;
		adventureInt->show(screen);
		//LOCPLINT->adventureInt->show(); //updating screen
		CSDL_Ext::update(screen);

		SDL_Delay(5);
		SDL_framerateDelay(mainFPSmng); //for animation purposes
	} //for(int i=1; i<32; i+=4)
	//main moving done
	//finishing move

	//restoring adventureInt->terrain.move*
	adventureInt->terrain.moveX = adventureInt->terrain.moveY = 0;

	if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
	{
		delObjRect(hp.x, hp.y-2, hp.z, ho->id);
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
		delObjRect(hp.x-3, hp.y, hp.z, ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
	{
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
	{
		delObjRect(hp.x-2, hp.y-2, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x+1, hp.y, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
	{
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
	{
		delObjRect(hp.x-2, hp.y+1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
		delObjRect(hp.x+1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-3, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x, hp.y+1, hp.z, ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
	}

	//restoring good rects
	subRect(details.end.x-2, details.end.y-1, details.end.z, genRect(32, 32, 0, 0), ho->id);
	subRect(details.end.x-1, details.end.y-1, details.end.z, genRect(32, 32, 32, 0), ho->id);
	subRect(details.end.x, details.end.y-1, details.end.z, genRect(32, 32, 64, 0), ho->id);

	subRect(details.end.x-2, details.end.y, details.end.z, genRect(32, 32, 0, 32), ho->id);
	subRect(details.end.x-1, details.end.y, details.end.z, genRect(32, 32, 32, 32), ho->id);
	subRect(details.end.x, details.end.y, details.end.z, genRect(32, 32, 64, 32), ho->id);

	//restoring good order of objects
	std::stable_sort(CGI->mh->ttiles[details.end.x-2][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-2][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x-1][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-1][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);

	std::stable_sort(CGI->mh->ttiles[details.end.x-2][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-2][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x-1][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-1][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);

	ho->isStanding = true;
	//move finished
	adventureInt->minimap.draw(screen2);
	adventureInt->heroList.updateMove(ho);

	//check if user cancelled movement
	{
		boost::unique_lock<boost::mutex> un(eventsM);
		while(events.size())
		{
			SDL_Event *ev = events.front();
			events.pop();
			switch(ev->type)
			{
			case SDL_MOUSEBUTTONDOWN:
				stillMoveHero.setn(STOP_MOVE);
				break;
			case SDL_KEYDOWN:
				if(ev->key.keysym.sym < SDLK_F1  ||  ev->key.keysym.sym > SDLK_F15)
					stillMoveHero.setn(STOP_MOVE);
				break;
			}
			delete ev;
		}
	}

	if(stillMoveHero.get() == WAITING_MOVE)
		stillMoveHero.setn(DURING_MOVE);

}
void CPlayerInterface::heroKilled(const CGHeroInstance* hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	graphics->heroWins.erase(hero->ID);
	wanderingHeroes -= hero;
	adventureInt->heroList.updateHList(hero);
}
void CPlayerInterface::heroCreated(const CGHeroInstance * hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(graphics->heroWins.find(hero->subID)==graphics->heroWins.end())
		graphics->heroWins.insert(std::pair<int,SDL_Surface*>(hero->subID,infoWin(hero)));
	wanderingHeroes.push_back(hero);
	adventureInt->heroList.updateHList();
}
void CPlayerInterface::openTownWindow(const CGTownInstance * town)
{
	castleInt = new CCastleInterface(town);
	GH.pushInt(castleInt);
}

SDL_Surface * CPlayerInterface::infoWin(const CGObjectInstance * specific) //specific=0 => draws info about selected town/hero
{
	if (specific)
	{
		switch (specific->ID)
		{
		case HEROI_TYPE:
			return graphics->drawHeroInfoWin(dynamic_cast<const CGHeroInstance*>(specific));
			break;
		case TOWNI_TYPE:
			return graphics->drawTownInfoWin(dynamic_cast<const CGTownInstance*>(specific));
			break;
		default:
			return NULL;
			break;
		}
	}
	else
	{
		switch (adventureInt->selection->ID)
		{
		case HEROI_TYPE:
			{
				const CGHeroInstance * curh = (const CGHeroInstance *)adventureInt->selection;
				return graphics->drawHeroInfoWin(curh);
			}
		case TOWNI_TYPE:
			{
				return graphics->drawTownInfoWin((const CGTownInstance *)adventureInt->selection);
			}
		default:
			tlog1 << "Strange... selection is neither hero nor town\n";
			return NULL;
		}
	}
}

int3 CPlayerInterface::repairScreenPos(int3 pos)
{
	if(pos.x<-CGI->mh->frameW)
		pos.x = -CGI->mh->frameW;
	if(pos.y<-CGI->mh->frameH)
		pos.y = -CGI->mh->frameH;
	if(pos.x>CGI->mh->map->width - this->adventureInt->terrain.tilesw + CGI->mh->frameW)
		pos.x = CGI->mh->map->width - this->adventureInt->terrain.tilesw + CGI->mh->frameW;
	if(pos.y>CGI->mh->map->height - this->adventureInt->terrain.tilesh + CGI->mh->frameH)
		pos.y = CGI->mh->map->height - this->adventureInt->terrain.tilesh + CGI->mh->frameH;
	return pos;
}
void CPlayerInterface::heroPrimarySkillChanged(const CGHeroInstance * hero, int which, si64 val)
{
	if(which >= PRIMARY_SKILLS) //no need to redraw infowin if this is experience (exp is treated as prim skill with id==4)
		return;
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	redrawHeroWin(hero);
}
void CPlayerInterface::heroManaPointsChanged(const CGHeroInstance * hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	redrawHeroWin(hero);
}
void CPlayerInterface::heroMovePointsChanged(const CGHeroInstance * hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	//adventureInt->heroList.draw();
}
void CPlayerInterface::receivedResource(int type, int val)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	GH.totalRedraw();
}

void CPlayerInterface::heroGotLevel(const CGHeroInstance *hero, int pskill, std::vector<ui16>& skills, boost::function<void(ui32)> &callback)
{
	waitWhileDialog();
	CGI->soundh->playSound(soundBase::heroNewLevel);

	boost::unique_lock<boost::recursive_mutex> un(*pim);
	CLevelWindow *lw = new CLevelWindow(hero,pskill,skills,callback);
	GH.pushInt(lw);
}
void CPlayerInterface::heroInGarrisonChange(const CGTownInstance *town)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	//redraw infowindow
	SDL_FreeSurface(graphics->townWins[town->id]);
	graphics->townWins[town->id] = infoWin(town);


	if(town->garrisonHero && vstd::contains(wanderingHeroes,town->garrisonHero)) //wandering hero moved to the garrison
	{
		CGI->mh->hideObject(town->garrisonHero);
		wanderingHeroes -= town->garrisonHero;
	}

	if(town->visitingHero && !vstd::contains(wanderingHeroes,town->visitingHero)) //hero leaves garrison
	{
		CGI->mh->printObject(town->visitingHero);
		wanderingHeroes.push_back(town->visitingHero);
	}

	//adventureInt->heroList.updateHList();

	CCastleInterface *c = castleInt;
	if(c)
	{
		c->garr->highlighted = NULL;
		c->hslotup.hero = town->garrisonHero;
		c->garr->odown = c->hslotdown.hero = town->visitingHero;
		c->garr->set2 = town->visitingHero ? &town->visitingHero->army : NULL;
		c->garr->recreateSlots();
	}
	GH.totalRedraw();
}
void CPlayerInterface::heroVisitsTown(const CGHeroInstance* hero, const CGTownInstance * town)
{
	if(hero->tempOwner != town->tempOwner)
		return;
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	openTownWindow(town);
}
void CPlayerInterface::garrisonChanged(const CGObjectInstance * obj)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(obj->ID == HEROI_TYPE) //hero
	{
		const CGHeroInstance * hh;
		if(hh = dynamic_cast<const CGHeroInstance*>(obj))
		{
			SDL_FreeSurface(graphics->heroWins[hh->subID]);
			graphics->heroWins[hh->subID] = infoWin(hh);
		}
	}
	else if (obj->ID == TOWNI_TYPE) //town
	{
		const CGTownInstance * tt;
		if(tt = static_cast<const CGTownInstance*>(obj))
		{
			SDL_FreeSurface(graphics->townWins[tt->id]);
			graphics->townWins[tt->id] = infoWin(tt);
		}
		if(tt->visitingHero)
		{
			SDL_FreeSurface(graphics->heroWins[tt->visitingHero->subID]);
			graphics->heroWins[tt->visitingHero->subID] = infoWin(tt->visitingHero);
		}

	}

	bool wasGarrison = false;
	for(std::list<IShowActivable*>::iterator i = GH.listInt.begin(); i != GH.listInt.end(); i++)
	{
		if((*i)->type & IShowActivable::WITH_GARRISON)
		{
			CWindowWithGarrison *wwg = static_cast<CWindowWithGarrison*>(*i);
			wwg->garr->recreateSlots();
			wasGarrison = true;
		}
	}

	GH.totalRedraw();
}

void CPlayerInterface::buildChanged(const CGTownInstance *town, int buildingID, int what) //what: 1 - built, 2 - demolished
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	switch (buildingID)
	{
	case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 15:
		{
			SDL_FreeSurface(graphics->townWins[town->id]);
			graphics->townWins[town->id] = infoWin(town);
			break;
		}
	}
	if(!castleInt)
		return;
	if(castleInt->town!=town)
		return;
	switch(what)
	{
	case 1:
		CGI->soundh->playSound(soundBase::newBuilding);
		castleInt->addBuilding(buildingID);
		break;
	case 2:
		castleInt->removeBuilding(buildingID);
		break;
	}
}

void CPlayerInterface::battleStart(CCreatureSet *army1, CCreatureSet *army2, int3 tile, CGHeroInstance *hero1, CGHeroInstance *hero2, bool side) //called by engine when battle starts; side=0 - left, side=1 - right
{
	while(showingDialog->get())
		SDL_Delay(20);

	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt = new CBattleInterface(army1, army2, hero1, hero2, genRect(600, 800, (conf.cc.resx - 800)/2, (conf.cc.resy - 600)/2));
	CGI->musich->playMusicFromSet(CGI->musich->battleMusics, -1);
	GH.pushInt(battleInt);
}

void CPlayerInterface::battlefieldPrepared(int battlefieldType, std::vector<CObstacle*> obstacles) //called when battlefield is prepared, prior the battle beginning
{
}

void CPlayerInterface::battleStacksHealedRes(const std::vector<std::pair<ui32, ui32> > & healedStacks)
{
	for(int b=0; b<healedStacks.size(); ++b)
	{
		const CStack * healed = cb->battleGetStackByID(healedStacks[b].first);
		if(battleInt->creAnims[healed->ID]->getType() == 5)
		{
			//stack has been resurrected
			battleInt->creAnims[healed->ID]->setType(2);
		}
	}
}

void CPlayerInterface::battleNewStackAppeared(int stackID)
{
	//changing necessary things in battle interface
	battleInt->newStack(stackID);
}

void CPlayerInterface::battleObstaclesRemoved(const std::set<si32> & removedObstacles)
{
	for(std::set<si32>::const_iterator it = removedObstacles.begin(); it != removedObstacles.end(); ++it)
	{
		for(std::map< int, CDefHandler * >::iterator itBat = battleInt->idToObstacle.begin(); itBat != battleInt->idToObstacle.end(); ++itBat)
		{
			if(itBat->first == *it) //remove this obstacle
			{
				battleInt->idToObstacle.erase(itBat);
				break;
			}
		}
	}
	//update accessible hexes
	battleInt->redrawBackgroundWithHexes(battleInt->activeStack);
}

void CPlayerInterface::battleCatapultAttacked(const CatapultAttack & ca)
{
	for(std::set< std::pair<ui8, ui8> >::const_iterator it = ca.attackedParts.begin(); it != ca.attackedParts.end(); ++it)
	{
		SDL_FreeSurface(battleInt->siegeH->walls[it->first + 2]);
		battleInt->siegeH->walls[it->first + 2] = BitmapHandler::loadBitmap(
			battleInt->siegeH->getSiegeName(it->first + 2, cb->battleGetWallState(it->first)) );
	}
}

void CPlayerInterface::battleStacksRemoved(const BattleStacksRemoved & bsr)
{
	for(std::set<ui32>::const_iterator it = bsr.stackIDs.begin(); it != bsr.stackIDs.end(); ++it) //for each removed stack
	{
		battleInt->stackRemoved(*it);
	}
}

void CPlayerInterface::battleNewRound(int round) //called at the beggining of each turn, round=-1 is the tactic phase, round=0 is the first "normal" turn
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt->newRound(round);
}

void CPlayerInterface::actionStarted(const BattleAction* action)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	curAction = new BattleAction(*action);
	if( (action->actionType==2 || (action->actionType==6 && action->destinationTile!=cb->battleGetPos(action->stackNumber))) )
	{
		battleInt->moveStarted = true;
		if(battleInt->creAnims[action->stackNumber]->framesInGroup(20))
		{
			battleInt->pendingAnims.push_back(std::make_pair(new CBattleMoveStart(battleInt, action->stackNumber), false));
		}
	}


	battleInt->deactivate();

	const CStack *stack = cb->battleGetStackByID(action->stackNumber);
	char txt[400];

	if(action->actionType == 1)
	{
		if(action->side)
			battleInt->defendingHero->setPhase(4);
		else
			battleInt->attackingHero->setPhase(4);
		return;
	}
	if(!stack)
	{
		tlog1<<"Something wrong with stackNumber in actionStarted. Stack number: "<<action->stackNumber<<std::endl;
		return;
	}

	int txtid = 0;
	switch(action->actionType)
	{
	case 3: //defend
		txtid = 120;
		break;
	case 8: //wait
		txtid = 136;
		break;
	case 11: //bad morale
		txtid = -34; //negative -> no separate singular/plural form		
		battleInt->displayEffect(30,stack->position);
		break;
	}

	if(txtid > 0  &&  stack->amount != 1)
		txtid++; //move to plural text
	else if(txtid < 0)
		txtid = -txtid;

	if(txtid)
	{
		sprintf(txt, CGI->generaltexth->allTexts[txtid].c_str(),  (stack->amount != 1) ? stack->creature->namePl.c_str() : stack->creature->nameSing.c_str(), 0);
		LOCPLINT->battleInt->console->addText(txt);
	}
}

void CPlayerInterface::actionFinished(const BattleAction* action)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	delete curAction;
	curAction = NULL;
	//if((action->actionType==2 || (action->actionType==6 && action->destinationTile!=cb->battleGetPos(action->stackNumber)))) //activating interface when move is finished
	{
		battleInt->activate();
	}
	if(action->actionType == 1)
	{
		if(action->side)
			battleInt->defendingHero->setPhase(0);
		else
			battleInt->attackingHero->setPhase(0);
	}
	if(action->actionType == 2 && battleInt->creAnims[action->stackNumber]->getType() != 2) //walk or walk & attack
	{
		battleInt->pendingAnims.push_back(std::make_pair(new CBattleMoveEnd(battleInt, action->stackNumber, action->destinationTile), false));
	}

}

BattleAction CPlayerInterface::activeStack(int stackID) //called when it's turn of that stack
{
	CBattleInterface *b = battleInt;
	{
		boost::unique_lock<boost::recursive_mutex> un(*pim);

		const CStack *stack = cb->battleGetStackByID(stackID);
		if(vstd::contains(stack->state,MOVED)) //this stack has moved and makes second action -> high morale
		{
			std::string hlp = CGI->generaltexth->allTexts[33];
			boost::algorithm::replace_first(hlp,"%s",(stack->amount != 1) ? stack->creature->namePl : stack->creature->nameSing);
			battleInt->displayEffect(20,stack->position);
			battleInt->console->addText(hlp);
		}

		b->stackActivated(stackID);
	}
	//wait till BattleInterface sets its command
	boost::unique_lock<boost::mutex> lock(b->givenCommand->mx);
	while(!b->givenCommand->data)
		b->givenCommand->cond.wait(lock);

	//tidy up
	BattleAction ret = *(b->givenCommand->data);
	delete b->givenCommand->data;
	b->givenCommand->data = NULL;

	//return command
	return ret;
}

void CPlayerInterface::battleEnd(BattleResult *br)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt->battleFinished(*br);
}

void CPlayerInterface::battleStackMoved(int ID, int dest, int distance, bool end)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt->stackMoved(ID, dest, end, distance);
}
void CPlayerInterface::battleSpellCast(SpellCast *sc)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt->spellCast(sc);
}
void CPlayerInterface::battleStacksEffectsSet(SetStackEffect & sse)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	battleInt->battleStacksEffectsSet(sse);
}
void CPlayerInterface::battleStacksAttacked(std::set<BattleStackAttacked> & bsa)
{
	tlog5 << "CPlayerInterface::battleStackAttacked - locking...";
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	tlog5 << "done!\n";


	std::vector<SStackAttackedInfo> arg;
	for(std::set<BattleStackAttacked>::iterator i = bsa.begin(); i != bsa.end(); i++)
	{
		if(i->isEffect() && i->effect != 12) //and not armageddon
		{
			const CStack *stack = cb->battleGetStackByID(i->stackAttacked, false);
			if (stack != NULL)
				battleInt->displayEffect(i->effect, stack->position);
		}
		SStackAttackedInfo to_put = {i->stackAttacked, i->damageAmount, i->killedAmount, i->attackerID, LOCPLINT->curAction->actionType==7, i->killed()};
		arg.push_back(to_put);
	}

	if(bsa.begin()->isEffect() && bsa.begin()->effect == 12) //for armageddon - I hope this condition is enough
	{
		battleInt->displayEffect(bsa.begin()->effect, -1);
	}

	battleInt->stacksAreAttacked(arg);
}
void CPlayerInterface::battleAttack(BattleAttack *ba)
{
	tlog5 << "CPlayerInterface::battleAttack - locking...";
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	tlog5 << "done!\n";
	assert(curAction);
	if(ba->lucky()) //lucky hit
	{
		const CStack *stack = cb->battleGetStackByID(ba->stackAttacking);
		std::string hlp = CGI->generaltexth->allTexts[45];
		boost::algorithm::replace_first(hlp,"%s",(stack->amount != 1) ? stack->creature->namePl.c_str() : stack->creature->nameSing.c_str());
		battleInt->console->addText(hlp);
		battleInt->displayEffect(18,stack->position);
	}
	//TODO: bad luck?

	if(ba->shot())
	{
		for(std::set<BattleStackAttacked>::iterator i = ba->bsa.begin(); i != ba->bsa.end(); i++)
			battleInt->stackIsShooting(ba->stackAttacking,cb->battleGetPos(i->stackAttacked));
	}
	else
	{
		const CStack * attacker = cb->battleGetStackByID(ba->stackAttacking);
		int shift = 0;
		if(ba->counter() && BattleInfo::mutualPosition(curAction->destinationTile, attacker->position) < 0)
		{
			if( BattleInfo::mutualPosition(curAction->destinationTile + 1, attacker->position) >= 0 )
				shift = 1;
			else
				shift = -1;
		}
		battleInt->stackAttacking( ba->stackAttacking, ba->counter() ? curAction->destinationTile + shift : curAction->additionalInfo );
	}
}

void CPlayerInterface::showComp(SComponent comp)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);

	CGI->soundh->playSoundFromSet(CGI->soundh->pickupSounds);

	adventureInt->infoBar.showComp(&comp,4000);
}

void CPlayerInterface::showInfoDialog(const std::string &text, const std::vector<Component*> &components, int soundID)
{
	std::vector<SComponent*> intComps;
	for(int i=0;i<components.size();i++)
		intComps.push_back(new SComponent(*components[i]));
	showInfoDialog(text,intComps,soundID);
}

void CPlayerInterface::showInfoDialog(const std::string &text, const std::vector<SComponent*> & components, int soundID)
{
	waitWhileDialog();
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	
	if(stillMoveHero.get() == DURING_MOVE)//if we are in the middle of hero movement
		stillMoveHero.setn(STOP_MOVE); //after showing dialog movement will be stopped

	std::vector<std::pair<std::string,CFunctionList<void()> > > pom;
	pom.push_back(std::pair<std::string,CFunctionList<void()> >("IOKAY.DEF",0));
	CInfoWindow * temp = new CInfoWindow(text,playerID,0,components,pom,false);

	if(makingTurn && GH.listInt.size())
	{
		CGI->soundh->playSound(static_cast<soundBase::soundID>(soundID));
		showingDialog->set(true);
		GH.pushInt(temp);
	}
	else
	{
		dialogs.push_back(temp);
	}
}

void CPlayerInterface::showYesNoDialog(const std::string &text, const std::vector<SComponent*> & components, CFunctionList<void()> onYes, CFunctionList<void()> onNo, bool DelComps)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	LOCPLINT->showingDialog->setn(true);
	std::vector<std::pair<std::string,CFunctionList<void()> > > pom;
	pom.push_back(std::pair<std::string,CFunctionList<void()> >("IOKAY.DEF",0));
	pom.push_back(std::pair<std::string,CFunctionList<void()> >("ICANCEL.DEF",0));
	CInfoWindow * temp = new CInfoWindow(text,playerID,0,components,pom,DelComps);
	temp->delComps = DelComps;
	for(int i=0;i<onYes.funcs.size();i++)
		temp->buttons[0]->callback += onYes.funcs[i];
	for(int i=0;i<onNo.funcs.size();i++)
		temp->buttons[1]->callback += onNo.funcs[i];

	GH.pushInt(temp);
}

void CPlayerInterface::showBlockingDialog( const std::string &text, const std::vector<Component> &components, ui32 askID, int soundID, bool selection, bool cancel )
{
	waitWhileDialog();
	boost::unique_lock<boost::recursive_mutex> un(*pim);

	CGI->soundh->playSound(static_cast<soundBase::soundID>(soundID));

	if(!selection && cancel) //simple yes/no dialog
	{
		std::vector<SComponent*> intComps;
		for(int i=0;i<components.size();i++)
			intComps.push_back(new SComponent(components[i])); //will be deleted by close in window

		showYesNoDialog(text,intComps,boost::bind(&CCallback::selectionMade,cb,1,askID),boost::bind(&CCallback::selectionMade,cb,0,askID),true);
	}
	else if(selection)
	{
		std::vector<CSelectableComponent*> intComps;
		for(int i=0;i<components.size();i++)
			intComps.push_back(new CSelectableComponent(components[i])); //will be deleted by CSelWindow::close

		std::vector<std::pair<std::string,CFunctionList<void()> > > pom;
		pom.push_back(std::pair<std::string,CFunctionList<void()> >("IOKAY.DEF",0));
		if(cancel)
		{
			pom.push_back(std::pair<std::string,CFunctionList<void()> >("ICANCEL.DEF",0));
		}

		CSelWindow * temp = new CSelWindow(text,playerID,35,intComps,pom,askID);
		GH.pushInt(temp);
		intComps[0]->clickLeft(true, false);
	}

}

void CPlayerInterface::tileRevealed(const std::set<int3> &pos)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	for(std::set<int3>::const_iterator i=pos.begin(); i!=pos.end();i++)
		adventureInt->minimap.showTile(*i);
}

void CPlayerInterface::tileHidden(const std::set<int3> &pos)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	for(std::set<int3>::const_iterator i=pos.begin(); i!=pos.end();i++)
		adventureInt->minimap.hideTile(*i);
}

void CPlayerInterface::openHeroWindow(const CGHeroInstance *hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	adventureInt->heroWindow->setHero(hero);
	adventureInt->heroWindow->quitButton->callback = boost::bind(&CHeroWindow::quit,adventureInt->heroWindow);
	GH.pushInt(adventureInt->heroWindow);
}

void CPlayerInterface::heroArtifactSetChanged(const CGHeroInstance*hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(adventureInt->heroWindow->curHero) //hero window is opened
	{
		adventureInt->heroWindow->deactivate();
		adventureInt->heroWindow->setHero(adventureInt->heroWindow->curHero);
		adventureInt->heroWindow->activate();
		return;
	}
	CExchangeWindow* cew = dynamic_cast<CExchangeWindow*>(GH.topInt());
	if(cew) //exchange window is open
	{
		cew->deactivate();
		for(int g=0; g<ARRAY_COUNT(cew->heroInst); ++g)
		{
			if(cew->heroInst[g] == hero)
			{
				cew->artifs[g]->setHero(hero);
			}
		}
		cew->prepareBackground();
		cew->activate();
	}
}

void CPlayerInterface::updateWater()
{

}

void CPlayerInterface::availableCreaturesChanged( const CGDwelling *town )
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(castleInt && town->ID == TOWNI_TYPE)
	{
		CFortScreen *fs = dynamic_cast<CFortScreen*>(GH.topInt());
		if(fs)
			fs->draw(castleInt,false);
	}
	else if(GH.listInt.size() && (town->ID == 17  ||  town->ID == 20)) //external dwelling
	{
		CRecruitmentWindow *crw = dynamic_cast<CRecruitmentWindow*>(GH.topInt());
		if(crw)
			crw->initCres();
	}
}

void CPlayerInterface::heroBonusChanged( const CGHeroInstance *hero, const HeroBonus &bonus, bool gain )
{
	if(bonus.type == HeroBonus::NONE)	return;
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	redrawHeroWin(hero);
}

template <typename Handler> void CPlayerInterface::serializeTempl( Handler &h, const int version )
{
	h & playerID & serialID;
	h & sysOpts;
	h & CBattleInterface::settings;
}

void CPlayerInterface::serialize( COSer<CSaveFile> &h, const int version )
{
	serializeTempl(h,version);
}

void CPlayerInterface::serialize( CISer<CLoadFile> &h, const int version )
{
	serializeTempl(h,version);
	sysOpts.apply();
}

void CPlayerInterface::redrawHeroWin(const CGHeroInstance * hero)
{
	if(!vstd::contains(graphics->heroWins,hero->subID))
	{
		tlog1 << "Cannot redraw infowindow for hero with subID=" << hero->subID << " - not present in our map\n";
		return;
	}
	SDL_FreeSurface(graphics->heroWins[hero->subID]);
	graphics->heroWins[hero->subID] = infoWin(hero); 
	if (adventureInt->selection == hero)
		adventureInt->infoBar.draw(screen);
}

bool CPlayerInterface::moveHero( const CGHeroInstance *h, CGPath path )
{
	if (!h)
		return false; //can't find hero

	bool result = false;
	path.convert(0);
	boost::unique_lock<boost::mutex> un(stillMoveHero.mx);
	stillMoveHero.data = CONTINUE_MOVE;

	enum TerrainTile::EterrainType currentTerrain = TerrainTile::border; // not init yet
	enum TerrainTile::EterrainType newTerrain;
	int sh = -1;

	for(int i=path.nodes.size()-1; i>0 && stillMoveHero.data == CONTINUE_MOVE; i--)
	{
		//stop sending move requests if hero exhausted all his move points
		if(!h->movement)
		{
			stillMoveHero.data = STOP_MOVE;
			break;
		}
		// Start a new sound for the hero movement or let the existing one carry on.
#if 0
		// TODO
		if (hero is flying && sh == -1)
			sh = CGI->soundh->playSound(soundBase::horseFlying, -1);
#endif
		{
			newTerrain = cb->getTileInfo(CGHeroInstance::convertPosition(path.nodes[i].coord, false))->tertype;

			if (newTerrain != currentTerrain) {
				CGI->soundh->stopSound(sh);
				sh = CGI->soundh->playSound(CGI->soundh->horseSounds[newTerrain], -1);
				currentTerrain = newTerrain;
			}
		}

		stillMoveHero.data = WAITING_MOVE;

		int3 endpos(path.nodes[i-1].coord.x, path.nodes[i-1].coord.y, h->pos.z);
		cb->moveHero(h,endpos);
		while(stillMoveHero.data != STOP_MOVE  &&  stillMoveHero.data != CONTINUE_MOVE)
			stillMoveHero.cond.wait(un);
	}

	CGI->soundh->stopSound(sh);

	//stillMoveHero = false;
	cb->recalculatePaths();
	return result;
}

bool CPlayerInterface::shiftPressed() const
{
	return SDL_GetKeyState(NULL)[SDLK_LSHIFT]  ||  SDL_GetKeyState(NULL)[SDLK_RSHIFT];
}

void CPlayerInterface::showGarrisonDialog( const CArmedInstance *up, const CGHeroInstance *down, bool removableUnits, boost::function<void()> &onEnd )
{
	{
		boost::unique_lock<boost::mutex> un(showingDialog->mx);
		while(showingDialog->data)
			showingDialog->cond.wait(un);
	}

	boost::unique_lock<boost::recursive_mutex> un(*pim);
	while(dialogs.size())
	{
		pim->unlock();
		SDL_Delay(20);
		pim->lock();
	}
	CGarrisonWindow *cgw = new CGarrisonWindow(up,down,removableUnits);
	cgw->quit->callback += onEnd;
	GH.pushInt(cgw);
}

void CPlayerInterface::requestRealized( PackageApplied *pa )
{
	if(stillMoveHero.get() == DURING_MOVE)
		stillMoveHero.setn(CONTINUE_MOVE);
}

void CPlayerInterface::heroExchangeStarted(si32 hero1, si32 hero2)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	GH.pushInt(new CExchangeWindow(hero2, hero1));
}

void CPlayerInterface::objectPropertyChanged(const SetObjectProperty * sop)
{
	//redraw minimap if owner changed
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(sop->what == 1)
	{
		const CGObjectInstance * obj = cb->getObjectInfo(sop->id);
		std::set<int3> pos = obj->getBlockedPos();
		for(std::set<int3>::const_iterator it = pos.begin(); it != pos.end(); ++it)
		{
			if(cb->isVisible(*it))
				adventureInt->minimap.showTile(*it);
		}

		if(obj->ID == TOWNI_TYPE)
			adventureInt->townList.genList();
	}

}

void CPlayerInterface::recreateWanderingHeroes()
{
	wanderingHeroes.clear();
	std::vector<const CGHeroInstance*> heroes = cb->getHeroesInfo();
	for(size_t i = 0; i < heroes.size(); i++)
		if(!heroes[i]->inTownGarrison)
			wanderingHeroes.push_back(heroes[i]);
}

const CGHeroInstance * CPlayerInterface::getWHero( int pos )
{
	if(pos < 0 || pos >= wanderingHeroes.size())
		return NULL;
	return wanderingHeroes[pos];
}

void CPlayerInterface::showRecruitmentDialog(const CGDwelling *dwelling, const CArmedInstance *dst, int level)
{
	waitWhileDialog();
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	CRecruitmentWindow *cr = new CRecruitmentWindow(dwelling, level, dst, boost::bind(&CCallback::recruitCreatures, cb, dwelling, _1, _2));
	GH.pushInt(cr);
}

void CPlayerInterface::waitWhileDialog()
{
	boost::unique_lock<boost::mutex> un(showingDialog->mx);
	while(showingDialog->data)
		showingDialog->cond.wait(un);
}

void CPlayerInterface::showShipyardDialog(const IShipyard *obj)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	int state = obj->state();
	std::vector<si32> cost;
	obj->getBoatCost(cost);
	CShipyardWindow *csw = new CShipyardWindow(cost, state, boost::bind(&CCallback::buildBoat, cb, obj));
	GH.pushInt(csw);
}

void CPlayerInterface::newObject( const CGObjectInstance * obj )
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	CGI->mh->printObject(obj);
	//we might have built a boat in shipyard in opened town screen
	if(obj->ID == 8 
		&& LOCPLINT->castleInt  
		&&  obj->pos-obj->getVisitableOffset() == LOCPLINT->castleInt->town->bestLocation())
	{
		CGI->soundh->playSound(soundBase::newBuilding);
		LOCPLINT->castleInt->recreateBuildings();
	}
}

void CPlayerInterface::centerView (int3 pos, int focusTime)
{
	LOCPLINT->adventureInt->centerOn (pos);
	if(focusTime)
	{
		bool activeAdv = (GH.topInt() == adventureInt  &&  adventureInt->active);
		if(activeAdv)
			adventureInt->deactivate();

		SDL_Delay(focusTime);

		if(activeAdv)
			adventureInt->activate();
	}
}

void CPlayerInterface::objectRemoved( const CGObjectInstance *obj )
{
	if(obj->ID == HEROI_TYPE  &&  obj->tempOwner == playerID)
	{
		const CGHeroInstance *h = static_cast<const CGHeroInstance*>(obj);
		heroKilled(h);
	}
}

void SystemOptions::setMusicVolume( int newVolume )
{
	musicVolume = newVolume;
	CGI->musich->setVolume(newVolume);
	settingsChanged();
}

void SystemOptions::setSoundVolume( int newVolume )
{
	soundVolume = newVolume;
	CGI->soundh->setVolume(newVolume);
	settingsChanged();
}

void SystemOptions::setHeroMoveSpeed( int newSpeed )
{
	heroMoveSpeed = newSpeed;
	settingsChanged();
}

void SystemOptions::setMapScrollingSpeed( int newSpeed )
{
	mapScrollingSpeed = newSpeed;
	settingsChanged();
}

void SystemOptions::settingsChanged()
{
	CSaveFile settings("config" PATHSEPARATOR "sysopts.bin");

	if(settings.sfile)
		settings << *this;
	else
		tlog1 << "Cannot save settings to config" PATHSEPARATOR "sysopts.bin!\n";
}

void SystemOptions::apply()
{
	CGI->musich->setVolume(musicVolume);
	CGI->soundh->setVolume(soundVolume);
	settingsChanged();
}