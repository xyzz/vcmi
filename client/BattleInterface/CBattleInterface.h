#pragma once


#include "../GUIBase.h"
#include "../../lib/CCreatureSet.h"
#include "../../lib/ConstTransitivePtr.h" //may be reundant
#include "../CAnimation.h"
#include "SStackAttackedInfo.h"
#include "CHexFieldControl.h"
#include "CShootingAnimation.h"
#include "../../lib/SHexField.h"
#include "../../lib/GameConstants.h"

/*
 * CBattleInterface.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

class CLabel;
class CCreatureSet;
class CGHeroInstance;
class CDefHandler;
class CStack;
class CCallback;
class AdventureMapButton;
class CHighlightableButton;
class CHighlightableButtonsGroup;
struct BattleResult;
struct BattleSpellCast;
struct CObstacleInstance;
template <typename T> struct CondSh;
struct SetStackEffect;;
struct BattleAction;
class CGTownInstance;
struct CatapultAttack;
class CBattleInterface;
struct SCatapultSProjectileInfo;
struct BattleTriggerEffect;
class CBattleAnimation;
class CBattleHero;
class CBattleConsole;
class CBattleResultWindow;
class CStackQueue;

/// Class which manages the locked hex fields that are blocked e.g. by obstacles
class CBattleObstacle
{
	std::vector<int> lockedHexes;
};

/// Struct for battle effect animation e.g. morale, prayer, armageddon, bless,...
struct SBattleEffect
{
	int x, y; //position on the screen
	int frame, maxFrame;
	CDefHandler * anim; //animation to display
	int effectID; //uniqueID equal ot ID of appropriate CSpellEffectAnim
};

/// Small struct which is needed for drawing the parabolic trajectory of the catapult cannon
struct SCatapultSProjectileInfo
{
	const double facA, facB, facC;
	const int fromX, toX;

	SCatapultSProjectileInfo() : facA(0), facB(0), facC(0), fromX(0), toX(0) { };
	SCatapultSProjectileInfo(double factorA, double factorB, double factorC, int fromXX, int toXX) : facA(factorA), facB(factorB), facC(factorC),
		fromX(fromXX), toX(toXX) { };

	double calculateY(double x);
};

/// Big class which handles the overall battle interface actions and it is also responsible for
/// drawing everything correctly.
class CBattleInterface : public CIntObject
{
	enum SpellSelectionType
	{
		ANY_LOCATION = 0, FRIENDLY_CREATURE, HOSTILE_CREATURE, ANY_CREATURE, OBSTACLE, TELEPORT, NO_LOCATION = -1, STACK_SPELL_CANCELLED = -2
	};
private:
	SDL_Surface * background, * menu, * amountNormal, * amountNegative, * amountPositive, * amountEffNeutral, * cellBorders, * backgroundWithHexes;
	AdventureMapButton * bOptions, * bSurrender, * bFlee, * bAutofight, * bSpell,
		* bWait, * bDefence, * bConsoleUp, * bConsoleDown, *btactNext, *btactEnd;
	CBattleConsole * console;
	CBattleHero * attackingHero, * defendingHero; //fighting heroes
	CStackQueue *queue;
	const CCreatureSet *army1, *army2; //copy of initial armies (for result window)
	const CGHeroInstance * attackingHeroInstance, * defendingHeroInstance;
	std::map< int, CCreatureAnimation * > creAnims; //animations of creatures from fighting armies (order by BattleInfo's stacks' ID)
	std::map< int, CDefHandler * > idToProjectile; //projectiles of creatures (creatureID, defhandler)
	std::map< int, CDefHandler * > idToObstacle; //obstacles located on the battlefield
	std::map< int, bool > creDir; // <creatureID, if false reverse creature's animation>
	ui8 animCount;
	const CStack * activeStack; //number of active stack; NULL - no one
	const CStack * stackToActivate; //when animation is playing, we should wait till the end to make the next stack active; NULL of none
	void activateStack(); //sets activeStack to stackToActivate etc.
	int mouseHoveredStack; //stack hovered by mouse; if -1 -> none
    time_t lastMouseHoveredStackAnimationTime; // time when last mouse hovered animation occurred
    static const time_t HOVER_ANIM_DELTA;
	std::vector<SHexField> occupyableHexes, //hexes available for active stack
		attackableHexes; //hexes attackable by active stack
    bool stackCountOutsideHexes[GameConstants::BFIELD_SIZE]; // hexes that when in front of a unit cause it's amount box to move back
	int previouslyHoveredHex; //number of hex that was hovered by the cursor a while ago
	int currentlyHoveredHex; //number of hex that is supposed to be hovered (for a while it may be inappropriately set, but will be renewed soon)
	int attackingHex; //hex from which the stack would perform attack with current cursor
	double getAnimSpeedMultiplier() const; //returns multiplier for number of frames in a group
	std::map<int, int> standingFrame; //number of frame in standing animation by stack ID, helps in showing 'random moves'

	CPlayerInterface *tacticianInterface; //used during tactics mode, points to the interface of player with higher tactics (can be either attacker or defender in hot-seat), valid onloy for human players
	bool tacticsMode;
	bool stackCanCastSpell; //if true, active stack could possibly cats some target spell
	bool spellDestSelectMode; //if true, player is choosing destination for his spell
	SpellSelectionType spellSelMode;
	BattleAction * spellToCast; //spell for which player is choosing destination
	ui32 creatureSpellToCast;
	void endCastingSpell(); //ends casting spell (eg. when spell has been cast or canceled)

	void showAliveStack(const CStack *stack, SDL_Surface * to); //helper function for function show
	void showAliveStacks(std::vector<const CStack *> *aliveStacks, int hex, std::vector<const CStack *> *flyingStacks, SDL_Surface *to); // loops through all stacks at a given hex position
	void showPieceOfWall(SDL_Surface * to, int hex, const std::vector<const CStack*> & stacks); //helper function for show
	void showObstacles(std::multimap<SHexField, int> *hexToObstacle, std::vector<CObstacleInstance> &obstacles, int hex, SDL_Surface *to); // show all obstacles at a given hex position
	void redrawBackgroundWithHexes(const CStack * activeStack);
	void printConsoleAttacked(const CStack * defender, int dmg, int killed, const CStack * attacker, bool Multiple);

	std::list<SProjectileInfo> projectiles; //projectiles flying on battlefield
	void projectileShowHelper(SDL_Surface * to); //prints projectiles present on the battlefield
	void giveCommand(ui8 action, SHexField tile, ui32 stack, si32 additional=-1);
	bool isTileAttackable(const SHexField & number) const; //returns true if tile 'number' is neighboring any tile from active stack's range or is one of these tiles
	bool blockedByObstacle(SHexField hex) const;
	bool isCatapultAttackable(SHexField hex) const; //returns true if given tile can be attacked by catapult

	std::list<SBattleEffect> battleEffects; //different animations to display on the screen like spell effects

	/// Class which is responsible for drawing the wall of a siege during battle
	class SiegeHelper
	{
	private:
		static std::string townTypeInfixes[GameConstants::F_NUMBER]; //for internal use only - to build filenames
		SDL_Surface* walls[18];
		const CBattleInterface * owner;
	public:
		const CGTownInstance * town; //besieged town
		
		SiegeHelper(const CGTownInstance * siegeTown, const CBattleInterface * _owner); //c-tor
		~SiegeHelper(); //d-tor

		//filename getters
		std::string getSiegeName(ui16 what, ui16 additInfo = 1) const; //what: 0 - background, 1 - background wall, 2 - keep, 3 - bottom tower, 4 - bottom wall, 5 - below gate, 6 - over gate, 7 - upper wall, 8 - uppert tower, 9 - gate, 10 - gate arch, 11 - bottom static wall, 12 - upper static wall, 13 - moat, 14 - mlip, 15 - keep creature cover, 16 - bottom turret creature cover, 17 - upper turret creature cover; additInfo: 1 - intact, 2 - damaged, 3 - destroyed

		void printPartOfWall(SDL_Surface * to, int what);//what: 1 - background wall, 2 - keep, 3 - bottom tower, 4 - bottom wall, 5 - below gate, 6 - over gate, 7 - upper wall, 8 - uppert tower, 9 - gate, 10 - gate arch, 11 - bottom static wall, 12 - upper static wall, 15 - keep creature cover, 16 - bottom turret creature cover, 17 - upper turret creature cover

		friend class CBattleInterface;
	} * siegeH;

	CPlayerInterface * attackerInt, * defenderInt; //because LOCPLINT is not enough in hotSeat
	const CGHeroInstance * getActiveHero(); //returns hero that can currently cast a spell
public:
	CPlayerInterface * curInt; //current player interface
	std::list<std::pair<CBattleAnimation *, bool> > pendingAnims; //currently displayed animations <anim, initialized>
	void addNewAnim(CBattleAnimation * anim); //adds new anim to pendingAnims
	ui32 animIDhelper; //for giving IDs for animations
	static CondSh<bool> animsAreDisplayed; //for waiting with the end of battle for end of anims

	CBattleInterface(const CCreatureSet * army1, const CCreatureSet * army2, CGHeroInstance *hero1, CGHeroInstance *hero2, const SDL_Rect & myRect, CPlayerInterface * att, CPlayerInterface * defen); //c-tor
	~CBattleInterface(); //d-tor

	//std::vector<TimeInterested*> timeinterested; //animation handling
	void setPrintCellBorders(bool set); //if true, cell borders will be printed
	void setPrintStackRange(bool set); //if true,range of active stack will be printed
	void setPrintMouseShadow(bool set); //if true, hex under mouse will be shaded
	void setAnimSpeed(int set); //speed of animation; 1 - slowest, 2 - medium, 4 - fastest
	int getAnimSpeed() const; //speed of animation; 1 - slowest, 2 - medium, 4 - fastest

	CHexFieldControl bfield[GameConstants::BFIELD_SIZE]; //11 lines, 17 hexes on each
	//std::vector< CBattleObstacle * > obstacles; //vector of obstacles on the battlefield
	SDL_Surface * cellBorder, * cellShade;
	CondSh<BattleAction *> *givenCommand; //data != NULL if we have i.e. moved current unit
	bool myTurn; //if true, interface is active (commands can be ordered)
	CBattleResultWindow * resWindow; //window of end of battle

	bool moveStarted; //if true, the creature that is already moving is going to make its first step
	int moveSh;		  // sound handler used when moving a unit

	//button handle funcs:
	void bOptionsf();
	void bSurrenderf();
	void bFleef();
	void reallyFlee(); //performs fleeing without asking player
	void reallySurrender(); //performs surrendering without asking player
	void bAutofightf();
	void bSpellf();
	void bWaitf();
	void bDefencef();
	void bConsoleUpf();
	void bConsoleDownf();
	void bTacticNextStack();
	void bEndTacticPhase();
	//end of button handle funcs
	//napisz tu klase odpowiadajaca za wyswietlanie bitwy i obsluge uzytkownika, polecenia ma przekazywac callbackiem
	void activate();
	void deactivate();
	void show(SDL_Surface * to);
	void keyPressed(const SDL_KeyboardEvent & key);
	void mouseMoved(const SDL_MouseMotionEvent &sEvent);
	void clickRight(tribool down, bool previousState);

	//call-ins
	void startAction(const BattleAction* action);
	void newStack(const CStack * stack); //new stack appeared on battlefield
	void stackRemoved(int stackID); //stack disappeared from batlefiled
	void stackActivated(const CStack * stack); //active stack has been changed
	void stackMoved(const CStack * stack, std::vector<SHexField> destHex, int distance); //stack with id number moved to destHex
	void waitForAnims();
	void stacksAreAttacked(std::vector<SStackAttackedInfo> attackedInfos); //called when a certain amount of stacks has been attacked
	void stackAttacking(const CStack * attacker, SHexField dest, const CStack * attacked, bool shooting); //called when stack with id ID is attacking something on hex dest
	void newRoundFirst( int round );
	void newRound(int number); //caled when round is ended; number is the number of round
	void hexLclicked(int whichOne); //hex only call-in
	void stackIsCatapulting(const CatapultAttack & ca); //called when a stack is attacking walls
	void battleFinished(const BattleResult& br); //called when battle is finished - battleresult window should be printed
	const BattleResult * bresult; //result of a battle; if non-zero then display when all animations end
	void displayBattleFinished(); //displays battle result
	void spellCast(const BattleSpellCast * sc); //called when a hero casts a spell
	void battleStacksEffectsSet(const SetStackEffect & sse); //called when a specific effect is set to stacks
	void castThisSpell(int spellID); //called when player has chosen a spell from spellbook
	void displayEffect(ui32 effect, int destTile); //displays effect of a spell on the battlefield; affected: true - attacker. false - defender
	void battleTriggerEffect(const BattleTriggerEffect & bte);
	void setBattleCursor(const int myNumber); //really complex and messy
	void endAction(const BattleAction* action);
	void hideQueue();
	void showQueue();
	
	
	friend class CPlayerInterface;
	friend class AdventureMapButton;
	friend class CInGameConsole;
	
	friend class CBattleResultWindow;
	friend class CBattleHero;
	friend class CSpellEffectAnimation;
	friend class CBattleStackAnimation;
	friend class CReverseAnimation;
	friend class CDefenceAnimation;
	friend class CMovementAnimation;
	friend class CMovementStartAnimation;
	friend class CAttackAnimation;
	friend class CMeleeAttackAnimation;
	friend class CShootingAnimation;
	friend class CHexFieldControl;
};