#include "stdafx.h"
#include "CCallback.h"
#include "client/CGameInfo.h"
#include "lib/CGameState.h"
#include "client/CPlayerInterface.h"
#include "client/Client.h"
#include "lib/map.h"
#include "hch/CBuildingHandler.h"
#include "hch/CDefObjInfoHandler.h"
#include "hch/CGeneralTextHandler.h"
#include "hch/CHeroHandler.h"
#include "hch/CObjectHandler.h"
#include "lib/Connection.h"
#include "lib/NetPacks.h"
#include "client/mapHandler.h"
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "hch/CSpellHandler.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/*
 * CCallback.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


HeroMoveDetails::HeroMoveDetails(int3 Src, int3 Dst, CGHeroInstance*Ho)
	:src(Src),dst(Dst),ho(Ho)
{
	owner = ho->getOwner();
};

template <ui16 N> bool isType(CPack *pack)
{
	return pack->getType() == N;
}

bool CCallback::teleportHero(const CGHeroInstance *who, const CGTownInstance *where)
{
	CastleTeleportHero pack(who->id, where->id, 1);
	sendRequest(&pack);
	return true;
}

bool CCallback::moveHero(const CGHeroInstance *h, int3 dst)
{
	MoveHero pack(dst,h->id);
	sendRequest(&pack);
	return true;
}
void CCallback::selectionMade(int selection, int asker)
{
	QueryReply pack(asker,selection);
	*cl->serv << &pack;
}
void CCallback::recruitCreatures(const CGObjectInstance *obj, ui32 ID, ui32 amount)
{
	if(player!=obj->tempOwner  &&  obj->ID != 106)
		return;

	RecruitCreatures pack(obj->id,ID,amount);
	sendRequest(&pack);
}


bool CCallback::dismissCreature(const CArmedInstance *obj, int stackPos)
{
	if(((player>=0)  &&  obj->tempOwner != player) || (obj->stacksCount()<2  && obj->needsLastStack()))
		return false;

	DisbandCreature pack(stackPos,obj->id);
	sendRequest(&pack);
	return true;
}
bool CCallback::upgradeCreature(const CArmedInstance *obj, int stackPos, int newID)
{
	UpgradeCreature pack(stackPos,obj->id,newID);
	sendRequest(&pack);
	return false;
}
void CCallback::endTurn()
{
	tlog5 << "Player " << (unsigned)player << " ended his turn." << std::endl;
	EndTurn pack;
	sendRequest(&pack); //report that we ended turn
}
UpgradeInfo CCallback::getUpgradeInfo(const CArmedInstance *obj, int stackPos) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->getUpgradeInfo(const_cast<CArmedInstance*>(obj),stackPos);
}

const StartInfo * CCallback::getStartInfo() const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->scenarioOps;
}

int CCallback::getSpellCost(const CSpell * sp, const CGHeroInstance * caster) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);

	//if there is a battle
	if(gs->curB)
		return gs->curB->getSpellCost(sp, caster);

	//if there is no battle
	return caster->getSpellCost(sp);
}

int CCallback::estimateSpellDamage(const CSpell * sp) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);

	if(!gs->curB)
		return 0;

	const CGHeroInstance * ourHero = gs->curB->heroes[0]->tempOwner == player ? gs->curB->heroes[0] : gs->curB->heroes[1];
	return gs->curB->calculateSpellDmg(sp, ourHero, NULL, ourHero->getSpellSchoolLevel(sp), ourHero->getPrimSkillLevel(2));
}

void CCallback::getThievesGuildInfo(SThievesGuildInfo & thi, const CGObjectInstance * obj)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);

	if(obj == NULL)
		return;

	if(obj->ID == TOWNI_TYPE) //it is a town
	{
		gs->obtainPlayersStats(thi, gs->players[player].towns.size());
	}
	else if(obj->ID == 97) //Den of Thieves
	{
		gs->obtainPlayersStats(thi, 20);
	}
}

int CCallback::howManyTowns() const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->players[player].towns.size();
}

const CGTownInstance * CCallback::getTownInfo(int val, bool mode) const //mode = 0 -> val = serial; mode = 1 -> val = ID
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if (!mode)
	{
		const std::vector<CGTownInstance *> &towns = gs->players[gs->currentPlayer].towns;
		if(val < towns.size())
			return towns[val];
		else 
			return NULL;
	}
	else if(mode == 1)
	{
		const CGObjectInstance *obj = getObjectInfo(val);
		if(!obj)
			return NULL;
		if(obj->ID != TOWNI_TYPE)
			return NULL;
		else
			return static_cast<const CGTownInstance *>(obj);
	}
	return NULL;
}

bool CCallback::getTownInfo( const CGObjectInstance *town, InfoAboutTown &dest ) const
{
	if(!isVisible(town, player)) //it's not a town or it's not visible for layer
		return false;

	bool detailed = hasAccess(town->tempOwner);

	//TODO vision support, info about allies
	if(town->ID == TOWNI_TYPE)
		dest.initFromTown(static_cast<const CGTownInstance *>(town), detailed);
	else if(town->ID == 33 || town->ID == 219)
		dest.initFromGarrison(static_cast<const CGGarrison *>(town), detailed);
	else
		return false;
	return true;
}

int3 CCallback::guardingCreaturePosition (int3 pos) const
{
	return gs->guardingCreaturePosition(pos);
}

int CCallback::howManyHeroes(bool includeGarrisoned) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return cl->getHeroCount(player,includeGarrisoned);
}
const CGHeroInstance * CCallback::getHeroInfo(int val, int mode) const //mode = 0 -> val = serial; mode = 1 -> val = ID
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx); //TODO use me?
	//if (gs->currentPlayer!=player) //TODO: checking if we are allowed to give that info
	//	return NULL;
	if (!mode) //esrial id
	{
		if(val<gs->players[player].heroes.size())
		{
			return gs->players[player].heroes[val];
		}
		else
		{
			return NULL;
		}
	}
	else if(mode==1) //it's hero type id
	{
		for (size_t i=0; i < gs->players[player].heroes.size(); ++i)
		{
			if (gs->players[player].heroes[i]->type->ID==val)
			{
				return gs->players[player].heroes[i];
			}
		}
	}
	else //object id
	{
		return static_cast<const CGHeroInstance*>(gs->map->objects[val]);
	}
	return NULL;
}

const CGObjectInstance * CCallback::getObjectInfo(int ID) const
{
	//TODO: check for visibility
	return gs->map->objects[ID];
}

bool CCallback::getHeroInfo( const CGObjectInstance *hero, InfoAboutHero &dest ) const
{
	const CGHeroInstance *h = dynamic_cast<const CGHeroInstance *>(hero);
	if(!h || !isVisible(h->getPosition(false))) //it's not a hero or it's not visible for layer
		return false;
	
	//TODO vision support, info about allies
	dest.initFromHero(h, hasAccess(h->tempOwner));
	return true;
}

int CCallback::getResourceAmount(int type) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->players[player].resources[type];
}
std::vector<si32> CCallback::getResourceAmount() const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->players[player].resources;
}
int CCallback::getDate(int mode) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->getDate(mode);
}
std::vector < std::string > CCallback::getObjDescriptions(int3 pos) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	std::vector<std::string> ret;
	if(!isVisible(pos,player))
		return ret;
	BOOST_FOREACH(const CGObjectInstance * obj, gs->map->terrain[pos.x][pos.y][pos.z].blockingObjects)
		ret.push_back(obj->getHoverText());
	return ret;
}
bool CCallback::verifyPath(CPath * path, bool blockSea) const
{
	for (size_t i=0; i < path->nodes.size(); ++i)
	{
		if ( CGI->mh->ttiles[path->nodes[i].coord.x][path->nodes[i].coord.y][path->nodes[i].coord.z].tileInfo->blocked 
			&& (! (CGI->mh->ttiles[path->nodes[i].coord.x][path->nodes[i].coord.y][path->nodes[i].coord.z].tileInfo->visitable)))
			return false; //path is wrong - one of the tiles is blocked

		if (blockSea)
		{
			if (i==0)
				continue;

			if (
					((CGI->mh->ttiles[path->nodes[i].coord.x][path->nodes[i].coord.y][path->nodes[i].coord.z].tileInfo->tertype==TerrainTile::water)
					&&
					(CGI->mh->ttiles[path->nodes[i-1].coord.x][path->nodes[i-1].coord.y][path->nodes[i-1].coord.z].tileInfo->tertype!=TerrainTile::water))
				  ||
					((CGI->mh->ttiles[path->nodes[i].coord.x][path->nodes[i].coord.y][path->nodes[i].coord.z].tileInfo->tertype!=TerrainTile::water)
					&&
					(CGI->mh->ttiles[path->nodes[i-1].coord.x][path->nodes[i-1].coord.y][path->nodes[i-1].coord.z].tileInfo->tertype==TerrainTile::water))
				  ||
				  (CGI->mh->ttiles[path->nodes[i-1].coord.x][path->nodes[i-1].coord.y][path->nodes[i-1].coord.z].tileInfo->tertype==TerrainTile::rock)
					
				)
				return false;
		}


	}
	return true;
}

std::vector< std::vector< std::vector<unsigned char> > > & CCallback::getVisibilityMap() const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->players[player].fogOfWarMap;
}


bool CCallback::isVisible(int3 pos, int Player) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->map->isInTheMap(pos) && gs->isVisible(pos, Player);
}

std::vector < const CGTownInstance *> CCallback::getTownsInfo(bool onlyOur) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	std::vector < const CGTownInstance *> ret = std::vector < const CGTownInstance *>();
	for ( std::map<ui8, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end();i++)
	{
		for (size_t j=0; j < (*i).second.towns.size(); ++j)
		{
			if ((*i).first==player  
				|| (isVisible((*i).second.towns[j],player) && !onlyOur))
			{
				ret.push_back((*i).second.towns[j]);
			}
		}
	} //	for ( std::map<int, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end();i++)
	return ret;
}
std::vector < const CGHeroInstance *> CCallback::getHeroesInfo(bool onlyOur) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	std::vector < const CGHeroInstance *> ret;
	for(size_t i=0;i<gs->map->heroes.size();i++)
	{
		if(	 (gs->map->heroes[i]->tempOwner==player) ||
		   (isVisible(gs->map->heroes[i]->getPosition(false),player) && !onlyOur)	)
		{
			ret.push_back(gs->map->heroes[i]);
		}
	}
	return ret;
}

bool CCallback::isVisible(int3 pos) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return isVisible(pos,player);
}

bool CCallback::isVisible( const CGObjectInstance *obj, int Player ) const
{
	return gs->isVisible(obj, Player);
}

int CCallback::getMyColor() const
{
	return player;
}

int CCallback::getHeroSerial(const CGHeroInstance * hero) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	for (size_t i=0; i<gs->players[player].heroes.size();i++)
	{
		if (gs->players[player].heroes[i]==hero)
			return i;
	}
	return -1;
}

const CCreatureSet* CCallback::getGarrison(const CGObjectInstance *obj) const
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	const CArmedInstance *armi = dynamic_cast<const CArmedInstance*>(obj);
	if(!armi)
		return NULL;
	else 
		return armi;
}

int CCallback::swapCreatures(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2)
{
	ArrangeStacks pack(1,p1,p2,s1->id,s2->id,0);
	sendRequest(&pack);
	return 0;
}

int CCallback::mergeStacks(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2)
{
	ArrangeStacks pack(2,p1,p2,s1->id,s2->id,0);
	sendRequest(&pack);
	return 0;
}
int CCallback::splitStack(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2, int val)
{
	ArrangeStacks pack(3,p1,p2,s1->id,s2->id,val);
	sendRequest(&pack);
	return 0;
}

bool CCallback::dismissHero(const CGHeroInstance *hero)
{
	if(player!=hero->tempOwner) return false;

	DismissHero pack(hero->id);
	sendRequest(&pack);
	return true;
}

int CCallback::getMySerial() const
{	
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->players[player].serial;
}

bool CCallback::swapArtifacts(const CGHeroInstance * hero1, ui16 pos1, const CGHeroInstance * hero2, ui16 pos2)
{
	if(player!=hero1->tempOwner || player!=hero2->tempOwner)
		return false;

	ExchangeArtifacts ea(hero1->id, hero2->id, pos1, pos2);
	sendRequest(&ea);
	return true;
}

/**
 * Assembles or disassembles a combination artifact.
 * @param hero Hero holding the artifact(s).
 * @param artifactSlot The worn slot ID of the combination- or constituent artifact.
 * @param assemble True for assembly operation, false for disassembly.
 * @param assembleTo If assemble is true, this represents the artifact ID of the combination
 * artifact to assemble to. Otherwise it's not used.
 */
bool CCallback::assembleArtifacts (const CGHeroInstance * hero, ui16 artifactSlot, bool assemble, ui32 assembleTo)
{
	if (player != hero->tempOwner)
		return false;

	AssembleArtifacts aa(hero->id, artifactSlot, assemble, assembleTo);
	sendRequest(&aa);
	return true;
}

bool CCallback::buildBuilding(const CGTownInstance *town, si32 buildingID)
{
	CGTownInstance * t = const_cast<CGTownInstance *>(town);

	if(town->tempOwner!=player)
		return false;
	CBuilding *b = CGI->buildh->buildings[t->subID][buildingID];
	for(int i=0;i<b->resources.size();i++)
		if(b->resources[i] > gs->players[player].resources[i])
			return false; //lack of resources

	BuildStructure pack(town->id,buildingID);
	sendRequest(&pack);
	return true;
}

int CCallback::battleGetBattlefieldType()
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->battleGetBattlefieldType();
}

int CCallback::battleGetObstaclesAtTile(int tile) //returns bitfield 
{
	//TODO - write
	return -1;
}

std::vector<CObstacleInstance> CCallback::battleGetAllObstacles()
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(gs->curB)
		return gs->curB->obstacles;
	else
		return std::vector<CObstacleInstance>();
}

int CCallback::battleGetStack(int pos, bool onlyAlive)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->battleGetStack(pos, onlyAlive);
}

const CStack* CCallback::battleGetStackByID(int ID, bool onlyAlive)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->curB) return NULL;
	return gs->curB->getStack(ID, onlyAlive);
}

int CCallback::battleMakeAction(BattleAction* action)
{
	MakeCustomAction mca(*action);
	sendRequest(&mca);
	return 0;
}

const CStack* CCallback::battleGetStackByPos(int pos, bool onlyAlive)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return battleGetStackByID(battleGetStack(pos, onlyAlive), onlyAlive);
}

int CCallback::battleGetPos(int stack)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->curB)
	{
		tlog2<<"battleGetPos called when there is no battle!"<<std::endl;
		return -1;
	}
	for(size_t g=0; g<gs->curB->stacks.size(); ++g)
	{
		if(gs->curB->stacks[g]->ID == stack)
			return gs->curB->stacks[g]->position;
	}
	return -1;
}

std::map<int, CStack> CCallback::battleGetStacks()
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	std::map<int, CStack> ret;
	if(!gs->curB) //there is no battle
	{
		return ret;
	}

	for(size_t g=0; g<gs->curB->stacks.size(); ++g)
	{
		ret[gs->curB->stacks[g]->ID] = *(gs->curB->stacks[g]);
	}
	return ret;
}

void CCallback::getStackQueue( std::vector<const CStack *> &out, int howMany )
{
	if(!gs->curB)
	{
		tlog2 << "battleGetStackQueue called when there is not battle!" << std::endl;
		return;
	}
	gs->curB->getStackQueue(out, howMany);
}

CCreature CCallback::battleGetCreature(int number)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx); //TODO use me?
	if(!gs->curB)
	{
		tlog2<<"battleGetCreature called when there is no battle!"<<std::endl;
	}
	for(size_t h=0; h<gs->curB->stacks.size(); ++h)
	{
		if(gs->curB->stacks[h]->ID == number) //creature found
			return *(gs->curB->stacks[h]->type);
	}
#ifndef __GNUC__
	throw new std::exception("Cannot find the creature");
#else
	throw new std::exception();
#endif
}

std::vector<int> CCallback::battleGetAvailableHexes(int ID, bool addOccupiable)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->curB)
	{
		tlog2<<"battleGetAvailableHexes called when there is no battle!"<<std::endl;
		return std::vector<int>();
	}
	return gs->curB->getAccessibility(ID, addOccupiable);
	//return gs->battleGetRange(ID);
}

bool CCallback::battleIsStackMine(int ID)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->curB)
	{
		tlog2<<"battleIsStackMine called when there is no battle!"<<std::endl;
		return false;
	}
	for(size_t h=0; h<gs->curB->stacks.size(); ++h)
	{
		if(gs->curB->stacks[h]->ID == ID) //creature found
			return gs->curB->stacks[h]->owner == player;
	}
	return false;
}

bool CCallback::battleCanShoot(int ID, int dest)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);

	if(!gs->curB) return false;

	return gs->battleCanShoot(ID, dest);
}

bool CCallback::battleCanCastSpell()
{
	if(!gs->curB) //there is no battle
		return false;

	if(gs->curB->side1 == player)
		return gs->curB->castSpells[0] == 0 && gs->curB->heroes[0]->getArt(17);
	else
		return gs->curB->castSpells[1] == 0 && gs->curB->heroes[1]->getArt(17);
}

bool CCallback::battleCanFlee()
{
	return gs->battleCanFlee(player);
}

const CGTownInstance *CCallback::battleGetDefendedTown()
{
	if(!gs->curB || gs->curB->tid == -1)
		return NULL;

	return static_cast<const CGTownInstance *>(gs->map->objects[gs->curB->tid]);
}

ui8 CCallback::battleGetWallState(int partOfWall)
{
	if(!gs->curB || gs->curB->siege == 0)
	{
		return 0;
	}
	return gs->curB->si.wallState[partOfWall];
}

int CCallback::battleGetWallUnderHex(int hex)
{
	if(!gs->curB || gs->curB->siege == 0)
	{
		return -1;
	}
	return gs->curB->hexToWallPart(hex);
}

std::pair<ui32, ui32> CCallback::battleEstimateDamage(int attackerID, int defenderID)
{
	if(!gs->curB)
		return std::make_pair(0, 0);

	const CGHeroInstance * attackerHero, * defenderHero;

	if(gs->curB->side1 == player)
	{
		attackerHero = gs->curB->heroes[0];
		defenderHero = gs->curB->heroes[1];
	}
	else
	{
		attackerHero = gs->curB->heroes[1];
		defenderHero = gs->curB->heroes[0];
	}

	const CStack * attacker = gs->curB->getStack(attackerID, false),
		* defender = gs->curB->getStack(defenderID);

	return gs->curB->calculateDmgRange(attacker, defender, attackerHero, defenderHero, battleCanShoot(attacker->ID, defender->position), 0, false);
}

ui8 CCallback::battleGetSiegeLevel()
{
	if(!gs->curB)
		return 0;

	return gs->curB->siege;
}

const CGHeroInstance * CCallback::battleGetFightingHero(ui8 side) const
{
	if(!gs->curB)
		return 0;

	return gs->curB->heroes[side];
}

void CCallback::swapGarrisonHero( const CGTownInstance *town )
{
	if(town->tempOwner != player) return;

	GarrisonHeroSwap pack(town->id);
	sendRequest(&pack);
}

void CCallback::buyArtifact(const CGHeroInstance *hero, int aid)
{
	if(hero->tempOwner != player) return;

	BuyArtifact pack(hero->id,aid);
	sendRequest(&pack);
}

std::vector < const CGObjectInstance * > CCallback::getBlockingObjs( int3 pos ) const
{
	std::vector<const CGObjectInstance *> ret;
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->map->isInTheMap(pos) || !isVisible(pos))
		return ret;
	BOOST_FOREACH(const CGObjectInstance * obj, gs->map->terrain[pos.x][pos.y][pos.z].blockingObjects)
		ret.push_back(obj);
	return ret;
}

std::vector < const CGObjectInstance * > CCallback::getVisitableObjs( int3 pos ) const
{
	std::vector<const CGObjectInstance *> ret;
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	if(!gs->map->isInTheMap(pos) || !isVisible(pos))
		return ret;
	BOOST_FOREACH(const CGObjectInstance * obj, gs->map->terrain[pos.x][pos.y][pos.z].visitableObjects)
		ret.push_back(obj);
	return ret;
}

std::vector < const CGObjectInstance * > CCallback::getFlaggableObjects(int3 pos) const
{
	if(!isVisible(pos))
		return std::vector < const CGObjectInstance * >();

	std::vector < const CGObjectInstance * > ret;

	std::vector < std::pair<const CGObjectInstance*,SDL_Rect> > & objs = CGI->mh->ttiles[pos.x][pos.y][pos.z].objects;
	for(size_t b=0; b<objs.size(); ++b)
	{
		if(objs[b].first->tempOwner!=254 && !((objs[b].first->defInfo->blockMap[pos.y - objs[b].first->pos.y + 5] >> (objs[b].first->pos.x - pos.x)) & 1))
			ret.push_back(CGI->mh->ttiles[pos.x][pos.y][pos.z].objects[b].first);
	}
	return ret;
}

int3 CCallback::getMapSize() const
{
	return CGI->mh->sizes;
}

void CCallback::trade(const CGObjectInstance *market, int mode, int id1, int id2, int val1, const CGHeroInstance *hero/* = NULL*/)
{
	TradeOnMarketplace pack;
	pack.market = market;
	pack.hero = hero;
	pack.mode = mode;
	pack.r1 = id1;
	pack.r2 = id2;
	pack.val = val1;
	sendRequest(&pack);
}

void CCallback::setFormation(const CGHeroInstance * hero, bool tight)
{
	const_cast<CGHeroInstance*>(hero)-> formation = tight;
	SetFormation pack(hero->id,tight);
	sendRequest(&pack);
}

void CCallback::setSelection(const CArmedInstance * obj)
{
	SetSelection ss;
	ss.player = player;
	ss.id = obj->id;
	sendRequest(&ss);

	if(obj->ID == HEROI_TYPE)
	{
		cl->gs->calculatePaths(static_cast<const CGHeroInstance *>(obj), *cl->pathInfo);
		//nasty workaround. TODO: nice workaround
		cl->gs->getPlayer(player)->currentSelection = obj->id;
	}
}

void CCallback::recruitHero(const CGTownInstance *town, const CGHeroInstance *hero)
{
	ui8 i=0;
	for(; i<gs->players[player].availableHeroes.size(); i++)
	{
		if(gs->players[player].availableHeroes[i] == hero)
		{
			HireHero pack(i,town->id);
			sendRequest(&pack);
			return;
		}
	}
}

std::vector<const CGHeroInstance *> CCallback::getAvailableHeroes(const CGTownInstance * town) const
{
	std::vector<const CGHeroInstance *> ret(gs->players[player].availableHeroes.size());
	std::copy(gs->players[player].availableHeroes.begin(),gs->players[player].availableHeroes.end(),ret.begin());
	return ret;
}	

const TerrainTile * CCallback::getTileInfo( int3 tile ) const
{
	if(!gs->map->isInTheMap(tile)) 
	{
		tlog1 << tile << "is outside the map! (call to getTileInfo)\n";
		return NULL;
	}
	if(!isVisible(tile, player)) return NULL;
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return &gs->map->getTile(tile);
}

int CCallback::canBuildStructure( const CGTownInstance *t, int ID )
{
	return gs->canBuildStructure(t,ID);
}

std::set<int> CCallback::getBuildingRequiments( const CGTownInstance *t, int ID )
{
	return gs->getBuildingRequiments(t,ID);
}

bool CCallback::getPath(int3 src, int3 dest, const CGHeroInstance * hero, CPath &ret)
{
	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
	return gs->getPath(src,dest,hero, ret);
}

void CCallback::save( const std::string &fname )
{
	cl->save(fname);
}


void CCallback::sendMessage(const std::string &mess)
{
	PlayerMessage pm(player, mess);
	sendRequest(&pm);
}

void CCallback::buildBoat( const IShipyard *obj )
{
	BuildBoat bb;
	bb.objid = obj->o->id;
	sendRequest(&bb);
}

template <typename T>
void CCallback::sendRequest(const T* request)
{
	//TODO? should be part of CClient but it would have to be very tricky cause template/serialization issues
	if(waitTillRealize)
		cl->waitingRequest.set(true);

	*cl->serv << request;

	if(waitTillRealize)
		cl->waitingRequest.waitWhileTrue();
}

CCallback::CCallback( CGameState * GS, int Player, CClient *C ) 
	:gs(GS), cl(C), player(Player)
{
	waitTillRealize = false;
}

const CMapHeader * CCallback::getMapHeader() const
{
	return gs->map;
}

const CGPathNode * CCallback::getPathInfo( int3 tile )
{
	return &cl->pathInfo->nodes[tile.x][tile.y][tile.z];
}

bool CCallback::getPath2( int3 dest, CGPath &ret )
{
	if (!gs->map->isInTheMap(dest))
		return false;

	const CGHeroInstance *h = cl->IGameCallback::getSelectedHero(player);
	assert(cl->pathInfo->hero == h);
	if(cl->pathInfo->hpos != h->getPosition(false)) //hero position changed, must update paths
	{ 
		recalculatePaths();
	}
	return cl->pathInfo->getPath(dest, ret);
}

void CCallback::recalculatePaths()
{
	gs->calculatePaths(cl->IGameCallback::getSelectedHero(player), *cl->pathInfo);
}

void CCallback::calculatePaths( const CGHeroInstance *hero, CPathsInfo &out, int3 src /*= int3(-1,-1,-1)*/, int movement /*= -1*/ )
{
	gs->calculatePaths(hero, out, src, movement);
}

int3 CCallback::getGrailPos( float &outKnownRatio )
{
	if (CGObelisk::obeliskCount == 0)
	{
		outKnownRatio = 0.0f;
	}
	else
	{
		outKnownRatio = (float)CGObelisk::visited[player] / CGObelisk::obeliskCount;
	}
	return gs->map->grailPos;
}

void CCallback::dig( const CGObjectInstance *hero )
{
	DigWithHero dwh;
	dwh.id = hero->id;
	sendRequest(&dwh);
}

si8 CCallback::battleGetStackMorale( int stackID )
{
	return gs->curB->getStack(stackID)->MoraleVal();
}

si8 CCallback::battleGetStackLuck( int stackID )
{
	return gs->curB->getStack(stackID)->LuckVal();
}

void CCallback::castSpell(const CGHeroInstance *hero, int spellID, const int3 &pos)
{
	CastAdvSpell cas;
	cas.hid = hero->id;
	cas.sid = spellID;
	cas.pos = pos;
	sendRequest(&cas);
}

bool CCallback::hasAccess(int playerId) const
{
	return playerId == player  ||  player < 0;
}

si8 CCallback::battleHasDistancePenalty( int stackID, int destHex )
{
	return gs->curB->hasDistancePenalty(stackID, destHex);
}

si8 CCallback::battleHasWallPenalty( int stackID, int destHex )
{
	return gs->curB->hasWallPenalty(stackID, destHex);
}

si8 CCallback::battleCanTeleportTo(int stackID, int destHex, int telportLevel)
{
	return gs->curB->canTeleportTo(stackID, destHex, telportLevel);
}

int CCallback::getPlayerStatus(int player) const
{
	const PlayerState *ps = gs->getPlayer(player, false);
	if(!ps)
		return -1;
	return ps->status;
}
InfoAboutTown::InfoAboutTown()
{
	tType = NULL;
	details = NULL;
	fortLevel = 0;
	owner = -1;
}

InfoAboutTown::~InfoAboutTown()
{
	delete details;
}

void InfoAboutTown::initFromTown( const CGTownInstance *t, bool detailed )
{
	obj = t;
	army = t->getArmy();
	built = t->builded;
	fortLevel = t->fortLevel();
	name = t->name;
	tType = t->town;
	owner = t->tempOwner;

	if(detailed) 
	{
		//include details about hero
		details = new Details;
		details->goldIncome = t->dailyIncome();
		details->customRes = vstd::contains(t->builtBuildings, 15);
		details->hallLevel = t->hallLevel();
		details->garrisonedHero = t->garrisonHero;
	}
	/*else
	{
		//hide info about hero stacks counts
		for(std::map<si32,std::pair<ui32,si32> >::iterator i = slots.begin(); i != slots.end(); ++i)
		{
			i->second.second = 0;
		}
	}*/
}

void InfoAboutTown::initFromGarrison(const CGGarrison *garr, bool detailed)
{
	obj = garr;
	fortLevel = 0;
	army = garr->getArmy();
	name = CGI->generaltexth->names[33]; // "Garrison"
	owner = garr->tempOwner;
	built = false;
	tType = NULL;

	// Show detailed info only to owning player.
	if(detailed)
	{
		details = new InfoAboutTown::Details;
		details->customRes = false;
		details->garrisonedHero = false;
		details->goldIncome = -1;
		details->hallLevel = -1;
	}
}