/******************************************************************************

    JoeBOT - a bot for Counter-Strike
    Copyright (C) 2000-2002  Johannes Lampel

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

******************************************************************************/
// JoeBot
// by Johannes Lampel
// Johannes.Lampel@gmx.de
// http://joebot.counter-strike.de

//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// dll.cpp
//

#include <iostream.h>

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "entity_state.h"

#ifdef _WIN32
#include <time.h>
#endif

#ifdef USE_METAMOD
//#define SDK_UTIL_H  // util.h already included
#include "meta_api.h"
#endif /* USE_METAMOD */

#include "bot.h"
#include "bot_func.h"
#include "bot_modid.h"
#include "bot_wpdir.h"
#include "CBotCS.h"
#include "CBotDOD.h"
#include "CCommand.h"
#include "ChatHost.h"
#include "Commandfunc.h"
#include "CSDecals.h"
#include "CSkill.h"
#include "globalvars.h"
#include "NNWeapon.h"
#include "WorldGnome.h"

#include "NeuralNet.h"

CWorldGnome CWG;

#define _MAXCFGLINESPERFRAME 5

#ifndef USE_METAMOD
extern GETENTITYAPI other_GetEntityAPI;
extern GETNEWDLLFUNCTIONS other_GetNewDLLFunctions;
#endif /* not USE_METAMOD */
extern enginefuncs_t g_engfuncs;
#ifdef DEBUGENGINE
extern int debug_engine;
#endif
extern globalvars_t  *gpGlobals;

#ifdef USE_METAMOD
// Must provide at least one of these..
static META_FUNCTIONS gMetaFunctionTable = {
	GetEntityAPI,		// pfnGetEntityAPI		HL SDK; called before game DLL
	NULL,			// pfnGetEntityAPI_Post		META; called after game DLL
	NULL,			// pfnGetEntityAPI2		HL SDK2; called before game DLL
	NULL,			// pfnGetEntityAPI2_Post	META; called after game DLL
	NULL,			// pfnGetNewDLLFunctions	HL SDK2; called before game DLL
	NULL,			// pfnGetNewDLLFunctions_Post	META; called after game DLL
	GetEngineFunctions,	// pfnGetEngineFunctions	META; called before HL engine
	NULL,			// pfnGetEngineFunctions_Post	META; called after HL engine
};

// Description of plugin
plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION,	// ifvers
	"JoeBOT",	// name
	"1.6.3",	// version
	"2001/04/01",	// date
	"@$3.1415rin <as3.1415rin@bots-united.com>",	// author
	"http://joebot.bots-united.com/",	// url
	"JOEBOT",	// logtag
	PT_STARTUP,	// (when) loadable
	PT_ANYPAUSE,	// (when) unloadable
};

// Global vars from metamod:
meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;

// Metamod message to dll that it will be loaded as a metamod plugin
void Meta_Init(void)
{
}

// Metamod requesting info about this plugin:
//  ifvers			(given) interface_version metamod is using
//  pPlugInfo		(requested) struct with info about plugin
//  pMetaUtilFuncs	(given) table of utility functions provided by metamod
int Meta_Query(char *ifvers, plugin_info_t **pPlugInfo,
		mutil_funcs_t *pMetaUtilFuncs)
{
	if(ifvers);	// to satisfy gcc -Wunused

	// Check for valid pMetaUtilFuncs before we continue.
	if(!pMetaUtilFuncs) {
		ALERT(at_logged, "[%s] ERROR: Meta_Query called with null pMetaUtilFuncs\n", Plugin_info.logtag);
		return(FALSE);
	}
	gpMetaUtilFuncs=pMetaUtilFuncs;

	// Give metamod our plugin_info struct
	*pPlugInfo=&Plugin_info;

	// Check for interface version compatibility.
	if(!FStrEq(ifvers, Plugin_info.ifvers)) {
		int mmajor=0, mminor=0, pmajor=0, pminor=0;
		LOG_MESSAGE(PLID, "WARNING: meta-interface version mismatch; requested=%s ours=%s",
				Plugin_info.logtag, ifvers);
		// If plugin has later interface version, it's incompatible (update
		// metamod).
		sscanf(ifvers, "%d:%d", &mmajor, &mminor);
		sscanf(META_INTERFACE_VERSION, "%d:%d", &pmajor, &pminor);
		if(pmajor > mmajor || (pmajor==mmajor && pminor > mminor)) {
			LOG_ERROR(PLID, "metamod version is too old for this plugin; update metamod");
			return(FALSE);
		}
		// If plugin has older major interface version, it's incompatible
		// (update plugin).
		else if(pmajor < mmajor) {
			LOG_ERROR(PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");
			return(FALSE);
		}
		// Minor interface is older, but this is guaranteed to be backwards
		// compatible, so we warn, but we still accept it.
		else if(pmajor==mmajor && pminor < mminor)
			LOG_MESSAGE(PLID, "WARNING: metamod version is newer than expected; consider finding a newer version of this plugin");
		else
			LOG_ERROR(PLID, "unexpected version comparison; metavers=%s, mmajor=%d, mminor=%d; plugvers=%s, pmajor=%d, pminor=%d", ifvers, mmajor, mminor, META_INTERFACE_VERSION, pmajor, pminor);
	}

	return(TRUE);
}

// Metamod attaching plugin to the server.
//  now				(given) current phase, ie during map, during changelevel, or at startup
//  pFunctionTable	(requested) table of function tables this plugin catches
//  pMGlobals		(given) global vars from metamod
//  pGamedllFuncs	(given) copy of function tables from game dll
int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable,
		meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs)
{
	if(now > Plugin_info.loadable) {
		LOG_ERROR(PLID, "Can't load plugin right now");
		return(FALSE);
	}
	if(!pMGlobals) {
		LOG_ERROR(PLID, "Meta_Attach called with null pMGlobals");
		return(FALSE);
	}
	gpMetaGlobals=pMGlobals;
	if(!pFunctionTable) {
		LOG_ERROR(PLID, "Meta_Attach called with null pFunctionTable");
		return(FALSE);
	}
	memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));
	gpGamedllFuncs=pGamedllFuncs;

	LOG_MESSAGE(PLID, "%s v%s  %s", Plugin_info.name, Plugin_info.version, 
			Plugin_info.date);
	LOG_MESSAGE(PLID, "by %s", Plugin_info.author);
	LOG_MESSAGE(PLID, "   %s", Plugin_info.url);

	// Let's go.
	return(TRUE);
}

// Metamod detaching plugin from the server.
// now		(given) current phase, ie during map, etc
// reason	(given) why detaching (refresh, console unload, forced unload, etc)
int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
	if(now > Plugin_info.unloadable && reason != PNL_CMD_FORCED) {
		LOG_ERROR(PLID, "Can't unload plugin right now");
		return(FALSE);
	}
	// Done!
	return(TRUE);
}
#endif /* USE_METAMOD */

CSkill Skill;
CSDecals SDecals;
float TIMETOBEBORED = _TIMETOBEBORED;
float TIMEBEINGBORED = _TIMEBEINGBORED;
float g_fGameCommenced = -1;

// TheFatal - START
int g_msecnum;
float g_msecdel;
float g_msecval;
// TheFatal - END

GrenadeLog_t gSmoke[_MAXGRENADEREC];
GrenadeLog_t gFlash[_MAXGRENADEREC];
HostageLog_t gHossi[_MAXHOSTAGEREC];

long lGDCount = 4;

#ifndef USE_METAMOD
DLL_FUNCTIONS other_gFunctionTable;
#endif /* not USE_METAMOD */
DLL_GLOBAL const Vector g_vecZero = Vector(0,0,0);

CChatHost g_ChatHost;

int mod_id = -1;
int m_spriteTexture = 0;
//int default_bot_skill = 2;
bool isFakeClientCommand = FALSE;
int fake_arg_count;
float bot_check_time = 2.0;
int num_bots = 0;
int prev_num_bots = 0;
bool g_GameRules = FALSE;
edict_t *clients[32];
edict_t *listenserver_edict = NULL;
bool bWelcome[32];
float welcome_time[32];
bool g_bIsSteam = false;

CCommands Commands;

float gf_5th=0.0;
float gf_5thd = 0;
bool g_b5th;

float pfPlayerDistance[32][32];

float is_team_play = 0.0;
//char team_names[MAX_TEAMS][MAX_TEAMNAME_LENGTH];
int num_teams = 0;
bool checked_teamplay = FALSE;
edict_t *pent_info_tfdetect = NULL;
edict_t *pent_info_ctfdetect = NULL;
edict_t *pent_info_frontline = NULL;
edict_t *pent_item_tfgoal = NULL;
int max_team_players[4];  // for TFC
int team_class_limits[4];  // for TFC
int team_allies[4];  // TFC bit mapped allies BLUE, RED, YELLOW, and GREEN
int max_teams = 0;	// for TFC
//FLAG_S flags[MAX_FLAGS];  // for TFC
int num_flags = 0;  // for TFC

FILE *bot_cfg_fp = NULL;
bool need_to_open_cfg = TRUE;
float bot_cfg_pause_time = 0.0;
float respawn_time = 0.0;
bool spawn_time_reset = FALSE;

int flf_bug_fix;  // for FLF 1.1 capture point bug
int flf_bug_check;  // for FLF 1.1 capture point bug

edict_t *pEdictLastJoined;

CBotWPDir g_WPDir;

char welcome_msg[200];
char _JOEBOTVERSION[80];
char _JOEBOTVERSIONWOOS[80]= "1.6.3";
bool bDedicatedWelcome = false;
int g_iTypeoM;

float f_round_start;	// time of roundstart
float f_start_round = 0.0;	// when to start after freeze time
float f_timesrs = 1000;		// time since round start	->	updated every frame by start frame

extern bool bNNInit;
extern bool bNNInitError;

bool g_bMyBirthday;
long g_lAge;

void BotNameInit(void);
#ifndef USE_METAMOD
void UpdateClientData(const struct edict_s *ent, int sendweapons, struct clientdata_s *cd);
#endif /* USE_METAMOD */
void ProcessBotCfgFile(void);

bool bInitInfo = true;

bool MyBirthday(void){
	time_t now = time(NULL);
	tm *tm_now = localtime(&now);
	
	if(tm_now->tm_mon == 6
		&&tm_now->tm_mday==8){			// 8.7.
		g_lAge = tm_now->tm_year - 82;
		return true;
	}
	return false;
}

long CountLines(const char *szString){
	long lCount = 0;
	while(*szString){
		if(*szString=='\n')
			lCount++;
		szString ++;
	}
	return lCount;
}

void CalcDistances(void){
	edict_t *pP[32],*pPlayer;
	int i,i2;

	memset(pP,0,32*sizeof(edict_t *));

	for (i = 1; i<=gpGlobals->maxClients; i++){
		pPlayer = INDEXENT(i);
		
		// skip invalid players and skip self (i.e. this bot)
		if ((pPlayer) && (!pPlayer->free))
		{
			// skip this player if not alive (i.e. dead or dying)
			if (!IsAlive(pPlayer)){
				continue;
			}
			pP[i] = pPlayer;
			
			for(i2 = i; i2 > 0; i2--){
				if(pP[i2]){
					pfPlayerDistance[i][i2]
						= pfPlayerDistance[i2][i]
						= (pP[i]->v.origin - pP[i2]->v.origin).Length();
				}
			}
		}
	}
}

void JBServerCmd(void)
{
	bool bCmdOK = Commands.Exec(NULL, CM_DEDICATED, CMD_ARGV(1), CMD_ARGV(2), CMD_ARGV(3), CMD_ARGV(4), CMD_ARGV(5));
	if (!bCmdOK)
#ifdef USE_METAMOD
		SERVER_PRINT("Unrecognized joebot server command\n");
#else /* not USE_METAMOD */
		(*g_engfuncs.pfnServerPrint)("Unrecognized joebot server command\n");
#endif /* USE_METAMOD */
}

void GameDLLInit( void )
{
	long lschl;
	printf("JoeBOT: Launching DLL (CBB%liCBC%liCBD%li%li@%s)\n",sizeof(CBotBase),sizeof(CBotCS),sizeof(CBotDOD),time(NULL),"---");
#ifdef USE_METAMOD
	REG_SVR_COMMAND("joebot", JBServerCmd);
#else /* not USE_METAMOD */
	(*g_engfuncs.pfnAddServerCommand)("joebot", JBServerCmd);
#endif /* USE_METAMOD */
	JBRegCvars();

	WeaponDefs.Init();

	g_bMyBirthday = MyBirthday();
	
#ifdef _WIN32
	sprintf(_JOEBOTVERSION,"%s (win32)",_JOEBOTVERSIONWOOS);
#else
	sprintf(_JOEBOTVERSION,"%s (linux)",_JOEBOTVERSIONWOOS);
#endif
#ifdef _DEBUG
	strcat(_JOEBOTVERSION," DBGV");
#endif
	
	sprintf(welcome_msg,"----- JoeBOT %s by @$3.1415rin { http://joebot.bots-united.com/ }; -----\n\0",_JOEBOTVERSION);
	
	for (int i=0; i<32; i++){
		clients[i] = NULL;
		Buy[i] = 0;
		bWelcome[i] = false;
		welcome_time[i] = 5;
		bDedicatedWelcome = false;
	}
	if(bInitInfo){
		for (int i=0; i<32; i++){
			SBInfo[i].bot_class = 0;
			SBInfo[i].bot_skill = 90;
			SBInfo[i].bot_team = 5;
			SBInfo[i].kick_time = 0;
			*SBInfo[i].name = 0;
			SBInfo[i].respawn_state = RESPAWN_IDLE;
			*SBInfo[i].skin = 0;
		}
		bInitInfo = false;
	}

	memset(weapon_defs,0,sizeof(bot_weapon_t) * MAX_WEAPONS);
	
	for(lschl=0;lschl < _MAXGRENADEREC;lschl++){
		gFlash[lschl].p = 0;
		gSmoke[lschl].p = 0;
	}
	for(lschl=0;lschl < _MAXHOSTAGEREC;lschl++){
		gHossi[lschl].p = 0;
	}
	
	for(lschl = time(NULL)&255;lschl;lschl--){
		long l = RANDOM_LONG(0,100);
	}
	
	g_WPDir.Init();
	Skill.Load();
	Names.init();
	UpdateLanguage();
	// precaching chat files - at least the standard file :)
	g_ChatHost.GetChat("texts.txt");
	
	// init func table for buying weapons
	Buy[CS_WEAPON_P228]			= BotBuy_CS_WEAPON_P228;
	Buy[CS_WEAPON_SCOUT]		= BotBuy_CS_WEAPON_SCOUT;
	Buy[CS_WEAPON_HEGRENADE]	= BotBuy_CS_WEAPON_HEGRENADE;
	Buy[CS_WEAPON_XM1014]		= BotBuy_CS_WEAPON_XM1014;
	Buy[CS_WEAPON_MAC10]		= BotBuy_CS_WEAPON_MAC10;
	Buy[CS_WEAPON_AUG]			= BotBuy_CS_WEAPON_AUG;
	Buy[CS_WEAPON_SMOKEGRENADE] = BotBuy_CS_WEAPON_SMOKEGRENADE;
	Buy[CS_WEAPON_ELITE]		= BotBuy_CS_WEAPON_ELITE;
	Buy[CS_WEAPON_FIVESEVEN]	= BotBuy_CS_WEAPON_FIVESEVEN;
	Buy[CS_WEAPON_UMP45]		= BotBuy_CS_WEAPON_UMP45;
	Buy[CS_WEAPON_SG550]		= BotBuy_CS_WEAPON_SG550;
	Buy[CS_WEAPON_GALIL]        = BotBuy_CS_WEAPON_GALIL;
	Buy[CS_WEAPON_FAMAS]        = BotBuy_CS_WEAPON_FAMAS;
	Buy[CS_WEAPON_USP]			= BotBuy_CS_WEAPON_USP;
	Buy[CS_WEAPON_GLOCK18]		= BotBuy_CS_WEAPON_GLOCK18;
	Buy[CS_WEAPON_AWP]			= BotBuy_CS_WEAPON_AWP;
	Buy[CS_WEAPON_MP5NAVY]		= BotBuy_CS_WEAPON_MP5NAVY;
	Buy[CS_WEAPON_M249]			= BotBuy_CS_WEAPON_M249;
	Buy[CS_WEAPON_M3]			= BotBuy_CS_WEAPON_M3;
	Buy[CS_WEAPON_M4A1]			= BotBuy_CS_WEAPON_M4A1;
	Buy[CS_WEAPON_TMP]			= BotBuy_CS_WEAPON_TMP;
	Buy[CS_WEAPON_G3SG1]		= BotBuy_CS_WEAPON_G3SG1;
	Buy[CS_WEAPON_FLASHBANG]	= BotBuy_CS_WEAPON_FLASHBANG;
	Buy[CS_WEAPON_DEAGLE]		= BotBuy_CS_WEAPON_DEAGLE;
	Buy[CS_WEAPON_SG552]		= BotBuy_CS_WEAPON_SG552;
	Buy[CS_WEAPON_AK47]			= BotBuy_CS_WEAPON_AK47;
	Buy[CS_WEAPON_P90]			= BotBuy_CS_WEAPON_P90;
	
	if (!bNNInit){
		// init SOMPattern
		//SP.SetMaxPatternNum(1000000);
		char szFileNameNN[80];
		//try{
		UTIL_BuildFileName(szFileNameNN,"joebot","nn.br3");
		//NNCombat->LoadFile(szFileNameNN);
		CBaseNeuralNet *NNCombatP=0;
		LoadNet(NNCombatP,szFileNameNN);
		NNCombat = (CBaseNeuralNetFF *)NNCombatP;
		if (IS_DEDICATED_SERVER())
			if(NNCombat)
				printf("JoeBOT: loading neural network: %s\n", szFileNameNN);
			else
				printf("JoeBOT: error loading neural network: %s\n", szFileNameNN);
			if(!NNCombat)
				NNCombat = new CNeuralNetBProp;
			
			bNNInitError = false;
			/*}catch(...){		// just create a fuckin' NN to avoid any unforseen consequences
			if (IS_DEDICATED_SERVER())
			printf("JoeBOT : %s - havn't found NN - creating fake ones, to prevent crashing\n",szFileNameNN);
			NNCombat->SetLayerNum(4);
			NNCombat->SetNNeuronsOnLayer(0,IEND);
			NNCombat->SetNNeuronsOnLayer(1,7);
			NNCombat->SetNNeuronsOnLayer(2,7);
			NNCombat->SetNNeuronsOnLayer(3,OEND);
			NNCombat->AllocNeurons();
			NNCombat->ConnectLayer(0,1);
			NNCombat->ConnectLayer(1,2);
			NNCombat->ConnectLayer(2,3);
			bNNInitError = true;
	}*/
			try{
				UTIL_BuildFileName(szFileNameNN,"joebot","nnc.br3");
				//NNColl.LoadFile(szFileNameNN);
				CBaseNeuralNet *NNCollP=0;
				LoadNet(NNCollP,szFileNameNN);
				NNColl = (CBaseNeuralNetFF *)NNCollP;
				if (IS_DEDICATED_SERVER())
					if(NNColl)
						printf("JoeBOT: loading neural network: %s\n", szFileNameNN);
					else
						printf("JoeBOT: error loading neural network: %s\n", szFileNameNN);
					if(!NNColl)
						NNColl = new CNeuralNetBProp;
					
					bNNInitError = false;
			}catch(...){		// just create a fuckin' NN to avoid any unforseen consequences
				if (IS_DEDICATED_SERVER())
					printf("JoeBOT: %s - havn't found NN - creating fake ones, to prevent crashing\n",szFileNameNN);
				NNColl = new CNeuralNetBProp;
				NNCombat = new CNeuralNetBProp;
				bNNInitError = true;
			}
			bNNInit = true;
	}

	// prop nets  -  kind of precaching
	//cout << NNCombat << endl << NNColl << endl << endl;
	NNCombat->Propagate();
	NNColl->Propagate();

	if (IS_DEDICATED_SERVER())
		printf("JoeBOT: 'Precaching' nets\n");
	
#ifdef USE_METAMOD
	RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
	//other_gFunctionTable.pfnGameInit;
	(*other_gFunctionTable.pfnGameInit)();
#endif /* USE_METAMOD */
}

int DispatchSpawn( edict_t *pent )
{
	int index;
	
	if (gpGlobals->deathmatch)
	{
		char *pClassname = (char *)STRING(pent->v.classname);
		
		BOT_LOG("DispatchSpawn", "pent=%x, classname=%s, model=%s", pent, pClassname, pent->v.model ? STRING(pent->v.model) : "");
		
		if (FStrEq(pClassname, "worldspawn"))
		{
			g_bMyBirthday = MyBirthday();		// check if it is my birthday :D
			
			// do level initialization stuff here...
			WaypointInit();
			WaypointLoad(NULL);
			WPStat.Save();
			WPStat.Load();
			
			SDecals.Load();
			
			memset(AWP_ED,0,sizeof(AWP_EntLogItem)*32);
			
			for (int i=0; i<32; i++){
				bWelcome[i] = false;
				welcome_time[i] = 0;
			}
			bDedicatedWelcome = false;
			
			// determining g_iTypeoM
			if(!strncmp(STRING(gpGlobals->mapname),"cs",sizeof(char)*2)){
				g_iTypeoM = MT_CS;
			}
			else if(!strncmp(STRING(gpGlobals->mapname),"de",sizeof(char)*2)){
				g_iTypeoM = MT_DE;
			}
			else if(!strncmp(STRING(gpGlobals->mapname),"as",sizeof(char)*2)){
				g_iTypeoM = MT_AS;
			}
			else if(!strncmp(STRING(gpGlobals->mapname),"es",sizeof(char)*2)){
				g_iTypeoM = MT_ES;
			}
			else
				g_iTypeoM = MT_CS;
			
			pent_info_tfdetect = NULL;
			pent_info_ctfdetect = NULL;
			pent_info_frontline = NULL;
			pent_item_tfgoal = NULL;
			
			for (index=0; index < 4; index++)
			{
				max_team_players[index] = 0;  // no player limit
				team_class_limits[index] = 0;  // no class limits
				team_allies[index] = 0;
			}
			
			max_teams = 0;
			num_flags = 0;
			
			PRECACHE_SOUND("weapons/xbow_hit1.wav");      // waypoint add
			PRECACHE_SOUND("weapons/mine_activate.wav");  // waypoint delete
			PRECACHE_SOUND("common/wpn_hudoff.wav");      // path add/delete start
			PRECACHE_SOUND("common/wpn_hudon.wav");       // path add/delete done
			PRECACHE_SOUND("common/wpn_moveselect.wav");  // path add/delete cancel
			PRECACHE_SOUND("common/wpn_denyselect.wav");  // path add/delete error
			PRECACHE_SOUND("vox/loading.wav");
			PRECACHE_SOUND("vox/save.wav");
			PRECACHE_SOUND("player/sprayer.wav");
			
			m_spriteTexture = PRECACHE_MODEL( "sprites/lgtning.spr");
			
			g_GameRules = TRUE;
			
			is_team_play = 0.0;
			num_teams = 0;
			checked_teamplay = FALSE;
			
			bot_cfg_pause_time = 0.0;
			respawn_time = 0.0;
			spawn_time_reset = FALSE;
			
			prev_num_bots = num_bots;
			num_bots = 0;
			
			flf_bug_fix = 0;
			flf_bug_check = 0;
			
			bot_check_time = gpGlobals->time + _PAUSE_TIME;
			gf_5th = 0;
		}
	}
	
#ifdef USE_METAMOD
	RETURN_META_VALUE(MRES_IGNORED, 0);
#else /* not USE_METAMOD */
	return (*other_gFunctionTable.pfnSpawn)(pent);
#endif /* USE_METAMOD */
}

#ifndef USE_METAMOD
void DispatchThink( edict_t *pent )
{
	(*other_gFunctionTable.pfnThink)(pent);
}

void DispatchUse( edict_t *pentUsed, edict_t *pentOther )
{
	(*other_gFunctionTable.pfnUse)(pentUsed, pentOther);
}

void DispatchTouch( edict_t *pentTouched, edict_t *pentOther )
{
	(*other_gFunctionTable.pfnTouch)(pentTouched, pentOther);
}

void DispatchBlocked( edict_t *pentBlocked, edict_t *pentOther )
{
	(*other_gFunctionTable.pfnBlocked)(pentBlocked, pentOther);
}

void DispatchKeyValue( edict_t *pentKeyvalue, KeyValueData *pkvd )
{
	//BOT_LOG("DispatchKeyValue", "pentKeyvalue=%x, %s=%s", pentKeyvalue, pkvd->szKeyName, pkvd->szValue);
	(*other_gFunctionTable.pfnKeyValue)(pentKeyvalue, pkvd);
}

void DispatchSave( edict_t *pent, SAVERESTOREDATA *pSaveData )
{
	(*other_gFunctionTable.pfnSave)(pent, pSaveData);
}

int DispatchRestore( edict_t *pent, SAVERESTOREDATA *pSaveData, int globalEntity )
{
	return (*other_gFunctionTable.pfnRestore)(pent, pSaveData, globalEntity);
}

void DispatchObjectCollsionBox( edict_t *pent )
{
	(*other_gFunctionTable.pfnSetAbsBox)(pent);
}

void SaveWriteFields( SAVERESTOREDATA *pSaveData, const char *pname, void *pBaseData, TYPEDESCRIPTION *pFields, int fieldCount )
{
	(*other_gFunctionTable.pfnSaveWriteFields)(pSaveData, pname, pBaseData, pFields, fieldCount);
}

void SaveReadFields( SAVERESTOREDATA *pSaveData, const char *pname, void *pBaseData, TYPEDESCRIPTION *pFields, int fieldCount )
{
	(*other_gFunctionTable.pfnSaveReadFields)(pSaveData, pname, pBaseData, pFields, fieldCount);
}

void SaveGlobalState( SAVERESTOREDATA *pSaveData )
{
	(*other_gFunctionTable.pfnSaveGlobalState)(pSaveData);
}

void RestoreGlobalState( SAVERESTOREDATA *pSaveData )
{
	(*other_gFunctionTable.pfnRestoreGlobalState)(pSaveData);
}

void ResetGlobalState( void )
{
	(*other_gFunctionTable.pfnResetGlobalState)();
}
#endif /* not USE_METAMOD */

BOOL ClientConnect( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ]  )
{ 
	if (gpGlobals->deathmatch)
	{
		int i;
		int count = 0;
		
		BOT_LOG("ClientConnect", "pEntity=%x, pszName=%s", pEntity, pszName);
		
		// check if this client is the listen server client
		if (FStrEq(pszAddress, "loopback"))
		{
			// save the edict of the listen server client...
			listenserver_edict = pEntity;
			
			//bot_cfg_pause_time = gpGlobals->time + .5f;
		}
		
		// check if this is NOT a bot joining the server...
		if (!FStrEq(pszAddress, "127.0.0.1"))
		{
			// don't try to add bots for 60 seconds, give client time to get added
			//bot_check_time = gpGlobals->time + 60.0;
			bot_check_time = gpGlobals->time + _PAUSE_TIME*2;
			/*
			for (i=0; i < 32; i++)
			{
				if (bots[i])  // count the number of bots in use
					count++;
			}
			
			// if there are currently more than the minimum number of bots running
			// then kick one of the bots off the server...
			if ((count > int(jb_botsmin->value)) && (int(jb_botsmin->value) != -1))
			{
				for (i=0; i < 32; i++){
					if (bots[i]){  // is this slot used?
						sprintf(szTemp, "kick \"%s\"\n", STRING(bots[i]->pEdict->v.netname));
						
						SERVER_COMMAND(szTemp);  // kick the bot using (kick "name")
						
						break;
					}
				}
			}*/
			int clientindex = UTIL_ClientIndex(pEntity);
			if(clientindex != -1){
				welcome_time[clientindex] = gpGlobals->time + 10.0f;		// set welcome time to some seconds in the future :D
				bWelcome[clientindex] = false;
			}
		}
		pEdictLastJoined = pEntity;
		long lProp = long(200.f / float(UTIL_ClientsInGame()));
		for (i=0; i < 32; i++){
			if (bots[i]){
				if(RANDOM_LONG(0,100) < lProp){
					bots[i]->Chat.l_ChatEvent |= E_WELCOME;
				}
			}
		}
	}
	
#ifdef USE_METAMOD
	RETURN_META_VALUE(MRES_IGNORED, TRUE);
#else /* not USE_METAMOD */
	return (*other_gFunctionTable.pfnClientConnect)(pEntity, pszName, pszAddress, szRejectReason);
#endif /* USE_METAMOD */
}


void ClientDisconnect( edict_t *pEntity )
{
	if (gpGlobals->deathmatch)
	{
		int i;
		BOT_LOG("ClientDisconnect", "pEntity=%x", pEntity);
		
		i = 0;
		while ((i < 32) && (clients[i] != pEntity))
			i++;
		
		if (i < 32)
			clients[i] = NULL;
		
		i = UTIL_GetBotIndex(pEntity);
		if(i!=-1){
			if ( FBitSet( VARS( pEntity )->flags, FL_THIRDPARTYBOT ) )	// fix by Leon Hartwig
			{
				//FREE_PRIVATE( pEntity );
			}
			
			// someone kicked this bot off of the server...
			// copy stuff
			SBInfo[i].bot_class = bots[i]->bot_class;
			SBInfo[i].bot_skill = bots[i]->bot_skill;
			SBInfo[i].bot_team = bots[i]->bot_team;
			strcpy(SBInfo[i].name,bots[i]->name);
			strcpy(SBInfo[i].skin,bots[i]->skin);
			
			// save nn and stats
			char szFileName[80];
			WPStat.Save();
			
			UTIL_BuildFileName(szFileName,"joebot","nn_changed.br3");
			NNCombat->SaveFile(szFileName);
			
			SBInfo[i].kick_time = gpGlobals->time;  // save the kicked time
			
			delete bots[i];
			bots[i] = 0;
		}
	}
	
#ifdef USE_METAMOD
	RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
	(*other_gFunctionTable.pfnClientDisconnect)(pEntity);
#endif /* USE_METAMOD */
}

void ClientKill( edict_t *pEntity )
{
	BOT_LOG("ClientKill", "pEntity=%x", pEntity);
#ifdef USE_METAMOD
	RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
	(*other_gFunctionTable.pfnClientKill)(pEntity);
#endif /* USE_METAMOD */
}

void ClientPutInServer( edict_t *pEntity )
{
	BOT_LOG("ClientPutInServer", "pEntity=%x", pEntity);
	
	int i = 0;
	
	while ((i < 32) && (clients[i] != NULL))
		i++;
	
	if (i < 32)
		clients[i] = pEntity;  // store this clients edict in the clients array
	
	if ( !(pEntity->v.flags & (FL_FAKECLIENT | FL_THIRDPARTYBOT)) )
	{
		int count = 0;
		for (i=0; i < 32; i++)
		{
			if (bots[i])  // count the number of bots in use
				count++;
		}
		
		// if there are currently more than the minimum number of bots running
		// then kick one of the bots off the server...
		if ((count > int(jb_botsmin->value)) && (int(jb_botsmin->value) != -1))
		{
			for (i=0; i < 32; i++){
				if (bots[i]){  // is this slot used?
					sprintf(szTemp, "kick \"%s\"\n", STRING(bots[i]->pEdict->v.netname));

					SERVER_COMMAND(szTemp);  // kick the bot using (kick "name")
					
					break;
				}
			}
		}
	}

#ifdef USE_METAMOD
	RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
	(*other_gFunctionTable.pfnClientPutInServer)(pEntity);
#endif /* USE_METAMOD */
}

void FillServer(int iType, int iTypeAdd){
/*
iType	:	FILL_0
FILL_1
FILL_ALL
iTypeAdd:	FILL_HALF
FILL_FULL
	*/
	int iP0 = 0,		// which players are there already ?
		iP1 = 0,
		iPAll = 0;
	int iNeed2Fill,iFillTeam,ischl=0;
	
	int i;
	edict_t *pEnt;
	for (i = gpGlobals->maxClients; i; i--){
		pEnt = INDEXENT(i);
		if (!FNullEnt(pEnt) && (!pEnt->free)){
			if(!strncmp(STRING(pEnt->v.classname),"player",6)){
				char *infobuffer; 
				char cl_name[128]; 
				cl_name[0]='\0'; 
				infobuffer = (*g_engfuncs.pfnGetInfoKeyBuffer)(pEnt); 
				strcpy(cl_name,g_engfuncs.pfnInfoKeyValue(infobuffer, "name")); 
				if(cl_name[0]!='\0') 
				{ 
					iPAll ++;
					if(UTIL_GetTeam(pEnt) == 0){
						iP0++;
					}
					else
						iP1++;
				}
			}
		}
	}
	if(iTypeAdd == FILL_FULL){
		if(iType == FILL_ALL){
			iNeed2Fill = gpGlobals->maxClients - iPAll;
		}
		else if(iType == FILL_0){
			iNeed2Fill = gpGlobals->maxClients - iP0;
		}
		else if(iType == FILL_1){
			iNeed2Fill = gpGlobals->maxClients - iP1;
		}
		if(iNeed2Fill < 1)
			return;
	}
	else{
		if(iType == FILL_ALL){
			iNeed2Fill = gpGlobals->maxClients/2 - iPAll;
		}
		else if(iType == FILL_0){
			iNeed2Fill = gpGlobals->maxClients/2 - iP0;
		}
		else if(iType == FILL_1){
			iNeed2Fill = gpGlobals->maxClients/2 - iP1;
		}
		if(iNeed2Fill < 1)
			return;
	}
	if(iType == FILL_0)
		iFillTeam = 0;
	else if(iType == FILL_1)
		iFillTeam = 1;
	else 
		iFillTeam = 4;			// later 1 is added
	
	ischl = 0;
	//cout << iNeed2Fill<<"-"<<iPAll<<"-"<<gpGlobals->maxClients <<endl;
	while(iNeed2Fill && ischl < 32){
		if(!bots[ischl]){
			iNeed2Fill --;
			
			*SBInfo[ischl].name = 0;
			*SBInfo[ischl].skin = 0;
			SBInfo[ischl].bot_team = iFillTeam+1;
			SBInfo[ischl].bot_skill = -1;
			SBInfo[ischl].bot_class = -1;
			SBInfo[ischl].respawn_state = RESPAWN_NEED_TO_RESPAWN;
			SBInfo[ischl].kick_time = gpGlobals->time;
			
			//cout << "^";
		}
		ischl ++;
	}
	respawn_time = 0;
}

void KickBots(edict_t *pEntity,int iTeam,int iAll){
	int i;
	char szName[100];
	for(i=0;i<32;i++){
		if(bots[i]){
			sprintf(szName, "kick \"%s\"\n", STRING(bots[i]->pEdict->v.netname));
			if(iTeam != -1){
				if((iTeam ? 2 : 1) == bots[i]->bot_team){
					SERVER_COMMAND(szName);
					if(!iAll)
						return;
				}
			}
			else{
				SERVER_COMMAND(szName);
				if(!iAll)
					return;
			}
		}
	}
}

#define MAX_TRIES 500
#define _MAXERROR .2
#define MAX_CTRIES 10

void TrainNN(edict_t *pEntity){
/*char szDir[80];
if(mod_id == CSTRIKE_DLL){
#ifdef _WIN32
sprintf(szDir,"cstrike\\joebot\\");
#else
sprintf(szDir,"cstrike/joebot/");
#endif
}
else if(mod_id == DOD_DLL){
#ifdef _WIN32
sprintf(szDir,"dod\\joebot\\");
#else
sprintf(szDir,"dod/joebot/");
#endif
}
char szLoadText[80];
char szSave[80];
char msg[200];
strcpy(szLoadText,szDir);
strcat(szLoadText,"nntrain.pta");
strcpy(szSave,szDir);
strcat(szSave,"nntrain.ptt");

	CPattern NNCPattern;
	try{
	NNCPattern.LoadText(szLoadText);
	}
	catch(...){
	sprintf(msg,"Error loading %s\n",szLoadText);
	ClientPrint( VARS(pEntity), HUD_PRINTNOTIFY, msg);
	return;
	}	
	sprintf(msg,"Sucessfully loading %s\n",szLoadText);
	ClientPrint( VARS(pEntity), HUD_PRINTNOTIFY, msg);
	NNCPattern.Save(szSave);
	NNCPattern.SetNN(NNCombat);
	
	 NNCombat->InitConnections(-.3,.3);
	 
	  bool bflag=true;
	  long lloop=0,lschl,lEpoch=0;
	  double dError;
	  while(bflag){
	  lloop++;
	  for(lschl=0;lschl < MAX_TRIES;lschl++){
	  NNCPattern.LearnEpochMM();
	  lEpoch++;
	  //cout << lEpoch << " ";cout.flush();
	  dError = NNCPattern.dMaxErrorMax;
	  if(dError < _MAXERROR)
	  break;
	  }
	  if(lschl >= MAX_TRIES){
	  lEpoch = 0;
	  NNCombat->InitConnections(-.3,.3);
	  sprintf(msg,"%i.after %i epochs the net could not be trained to a max error of %.2f, it's %.2f - resetting nn and trying again\n",lloop,MAX_TRIES, _MAXERROR,dError);
	  ClientPrint( VARS(pEntity), HUD_PRINTNOTIFY, msg);
	  }
	  else{
	  bflag = false;
	  }
	  if(lloop>10){
	  sprintf(msg,"%i. after %i epochs the net could be trained to a max error of %.2f - canceling training and reloading",lloop,MAX_TRIES, _MAXERROR);
	  ClientPrint( VARS(pEntity), HUD_PRINTNOTIFY, msg);
	  char filename[80];
	  UTIL_BuildFileName(filename,"joebot","nn.br3");
	  NNCombat->Save(filename);
	  return;
	  }
	  }
	  if(dError < _MAXERROR){
	  sprintf(msg,"after %i epochs the net could be trained to a max error of %.2f\nNow you can test the net and if u wish to save it, do 'savenn'\n",lloop, _MAXERROR);
	  ClientPrint( VARS(pEntity), HUD_PRINTNOTIFY, msg);
}*/
}

void Endround(void){
	int i;
	for(i=0;i<32;i++){
		if(bots[i]){
			if(IsAlive(bots[i]->pEdict)){
				bots[i]->pEdict->v.frags++;
#ifdef USE_METAMOD
				MDLL_ClientKill(bots[i]->pEdict);
#else /* not USE_METAMOD */
				ClientKill(bots[i]->pEdict);
#endif /* not USE_METAMOD */
			}
		}
	}
}

void ClientCommand( edict_t *pEntity )
{
	// only allow custom commands if deathmatch mode and NOT dedicated server and
	// client sending command is the listen server client...
	
	if ((gpGlobals->deathmatch) && (!IS_DEDICATED_SERVER()) &&
		(pEntity == listenserver_edict))
	{
		BOT_LOG("ClientCommand", "g_argv=%s", g_argv);
		if (Commands.Exec(pEntity, CM_CONSOLE, CMD_ARGV(0), CMD_ARGV(1), CMD_ARGV(2), CMD_ARGV(3), CMD_ARGV(4)))
#ifdef USE_METAMOD
			RETURN_META(MRES_SUPERCEDE);
#else /* not USE_METAMOD */
			return;
#endif /* USE_METAMOD */
	}
	
#ifdef USE_METAMOD
	RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
	(*other_gFunctionTable.pfnClientCommand)(pEntity);
#endif /* USE_METAMOD */
}

#ifndef USE_METAMOD
void ClientUserInfoChanged( edict_t *pEntity, char *infobuffer )
{
	BOT_LOG("ClientUserInfoChanged", "pEntity=%x, infobuffer=%s", pEntity, infobuffer);
	(*other_gFunctionTable.pfnClientUserInfoChanged)(pEntity, infobuffer);
}

void ServerActivate( edict_t *pEdictList, int edictCount, int clientMax )
{
	(*other_gFunctionTable.pfnServerActivate)(pEdictList, edictCount, clientMax);
}

void ServerDeactivate( void )
{
	(*other_gFunctionTable.pfnServerDeactivate)();
}

void PlayerPreThink( edict_t *pEntity )
{
	(*other_gFunctionTable.pfnPlayerPreThink)(pEntity);
}

void PlayerPostThink( edict_t *pEntity )
{
	(*other_gFunctionTable.pfnPlayerPostThink)(pEntity);
}
#endif /* not USE_METAMOD */

void SearchEs_CSTRIKE(void){		// search entities
	long lNumS = 0,lNumF=0,lNumH=0;
	edict_t *pEnt;
	char szModel[80];
	
	pEnt = 0;
	
	while(pEnt = UTIL_FindEntityByClassname(pEnt,"hostage_entity")){
		gHossi[lNumH].p = pEnt;
		gHossi[lNumH].fVelocity = (gHossi[lNumH].VOrigin-pEnt->v.origin).Length();
		gHossi[lNumH].VOrigin = pEnt->v.origin;
		gHossi[lNumH].bUsed = true;
		lNumH ++;
		
		if(lNumH >= _MAXHOSTAGEREC)
			lNumH--;
	}
	while(pEnt = UTIL_FindEntityByClassname(pEnt,"grenade")){
		strcpy(szModel,STRING(pEnt->v.model));
		if(FStrEq(szModel,"models/w_smokegrenade.mdl")){
			gSmoke[lNumS].p = pEnt;
			gSmoke[lNumS].VOrigin = pEnt->v.origin;
			gSmoke[lNumS].bUsed = true;
			lNumS ++;
			
			if(lNumS >= _MAXGRENADEREC)
				lNumS--;
		}
		else if(FStrEq(szModel,"models/w_flashbang.mdl")){
			gFlash[lNumF].p = pEnt;
			gFlash[lNumF].VOrigin = pEnt->v.origin;
			gFlash[lNumF].bUsed = true;
			lNumF ++;
			
			if(lNumF >= _MAXGRENADEREC)
				lNumF--;
		}
	}
	
	for(;lNumS < _MAXGRENADEREC;lNumS++){
		gSmoke[lNumS].p = 0;
		gSmoke[lNumS].bUsed = false;
	}
	for(;lNumF < _MAXGRENADEREC;lNumF++){
		gFlash[lNumF].p = 0;
		gFlash[lNumF].bUsed = false;
	}
}
#ifdef _DEBUG
void ShowInfo(void){
	if(!IS_DEDICATED_SERVER()){
		if(listenserver_edict){
			edict_t *pInfo;
			char szText[1000];
			char szTemp[200];
			char szTemp2[200];
			float fMin = 10;
		
			szText[0] = 0;
			sprintf(szTemp,"time: %.0f/%.0f\n",gpGlobals->time,f_timesrs);
			strcat(szText,szTemp);
			pInfo = GetNearestPlayer(listenserver_edict->v.origin+(Vector(0,0,2)),-1,fMin,10,listenserver_edict);
			fMin = 100000;
			if(!pInfo){
				pInfo = GetNearestPlayer(listenserver_edict,-1,fMin,true,true);
			}
			if(pInfo){
				int iBI = UTIL_GetBotIndex(pInfo);
				if(iBI == -1){
				}
				else{
					sprintf(szTemp,"%s / %3.0f%% Health / %3.0f(%3.0f) ",STRING(pInfo->v.netname),pInfo->v.health,pInfo->v.velocity.Length(),bots[iBI]->f_max_speed);
					if(bots[iBI]->HasWeapon(1<<CS_WEAPON_FLASHBANG)){
						strcat(szTemp,"F");
					}
					if(bots[iBI]->HasWeapon(1<<CS_WEAPON_HEGRENADE)){
						strcat(szTemp,"H");
					}
					if(bots[iBI]->HasWeapon(1<<CS_WEAPON_SMOKEGRENADE)){
						strcat(szTemp,"S");
					}
					if(bots[iBI]->bReplay){
						strcat(szTemp," Replay");
					}
					strcat(szTemp,"\n");
					strcat(szText,szTemp);
					
					if(bots[iBI]->current_weapon.iId > -1 && bots[iBI]->current_weapon.iId <32){
						sprintf(szTemp,"%s %i/%0.0lf/%i",weapon_defs[bots[iBI]->current_weapon.iId].szClassname,bots[iBI]->current_weapon.iClip,WeaponDefs.ipClipSize[mod_id][bots[iBI]->current_weapon.iId],bots[iBI]->current_weapon.iAmmo1);
						strcat(szText,szTemp);
						sprintf(szTemp," - M:%2.2f A:%2.2f AH:%2.2f\n",bots[iBI]->d_Manner,bots[iBI]->f_Aggressivity,bots[iBI]->f_AimHead);
						strcat(szText,szTemp);
						sprintf(szTemp," - OV %.2f %.2f / VA %.0f %.0f --- fLT %.0f\n",bots[iBI]->v_Offset.y,bots[iBI]->v_Offset.x,bots[iBI]->pEdict->v.v_angle.y,bots[iBI]->pEdict->v.v_angle.x,bots[iBI]->f_LookTo - gpGlobals->time);
						strcat(szText,szTemp);
						sprintf(szTemp,"FG:%3.i/G:%3.i/C:%3.i\n",bots[iBI]->iGoal,bots[iBI]->iFarGoal,bots[iBI]->i_CurrWP);
						strcat(szText,szTemp);
					}
					
					if(bots[iBI]->f_ducktill > gpGlobals->time){
						sprintf(szTemp,"ducking: %3.f\n",bots[iBI]->f_ducktill-gpGlobals->time);
						strcat(szText,szTemp);
					}
					if(bots[iBI]->GOrder.lTypeoG){
						sprintf(szTemp,"GOrder: State(%i) Type(%s-%i)\n",bots[iBI]->GOrder.lState,weapon_defs[bots[iBI]->GOrder.lTypeoG].szClassname,bots[iBI]->GOrder.lTypeoG);
						WaypointDrawBeamDebug(listenserver_edict,bots[iBI]->pEdict->v.origin,bots[iBI]->GOrder.VAim,10,0,200,0,0,200,10);
						strcat(szText,szTemp);
					}
					sprintf(szTemp,"%i Task(s)\n",bots[iBI]->Task.lNOT);
					strcat(szText,szTemp);
					if(bots[iBI]->Task.current){
						CTaskItem *p;

						int i;
						for(i=bots[iBI]->Task.lNOT-1;i>=0&&i>bots[iBI]->Task.lNOT - 6;i--){
							p = bots[iBI]->Task.GetTask(i);
							sprintf(szTemp,"Task(%i): ",i);
							if(p->lType & BT_COVER){
								sprintf(szTemp2,"BT_COVER ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_HELP){
								sprintf(szTemp2,"BT_HELP ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_FOLLOW){
								sprintf(szTemp2,"BT_FOLLOW ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_ROAMTEAM){
								sprintf(szTemp2,"BT_ROAMTEAM ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_GOTO){
								sprintf(szTemp2,"BT_GOTO ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_GUARD){
								sprintf(szTemp2,"BT_GUARD ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_CAMPATGOAL){
								sprintf(szTemp2,"BT_CAMPATGOAL ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_WAIT4TM8){
								sprintf(szTemp2,"BT_WAIT4TM8 ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_HOLDPOS){
								sprintf(szTemp2,"BT_HOLDPOS ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_PICKUP){
								sprintf(szTemp2,"BT_PICKUP ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_HIDE){
								sprintf(szTemp2,"BT_HIDE ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_FLEE){
								sprintf(szTemp2,"BT_FLEE ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_RELOAD){
								sprintf(szTemp2,"BT_RELOAD ");
								strcat(szTemp,szTemp2);
							}
							/*if(p->lType & BT_PAUSE){
							sprintf(szTemp2,"BT_PAUSE ");
							strcat(szTemp,szTemp2);
						}*/
							if(p->lType & BT_GOBUTTON){
								sprintf(szTemp2,"BT_GOBUTTON ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_HUNT){
								sprintf(szTemp2,"BT_HUNT ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_CROUCH){
								sprintf(szTemp2,"BT_CROUCH ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_CAMP){
								sprintf(szTemp2,"BT_CAMP ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_BLINDED){
								sprintf(szTemp2,"BT_BLINDED ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_IGNOREENEMY){
								sprintf(szTemp2,"BT_IGNOREENEMY ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_TMP){
								sprintf(szTemp2,"BT_TMP ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_DEL){
								sprintf(szTemp2,"BT_DEL ");
								strcat(szTemp,szTemp2);
							}
							if(p->lType & BT_LOCKED){
								sprintf(szTemp2,"BT_LOCKED ");
								strcat(szTemp,szTemp2);
							}
							sprintf(szTemp2,"(%2.f)\n",(p->fLive2>0?p->fLive2:p->fLive2+gpGlobals->time - 1)-gpGlobals->time);
							strcat(szTemp,szTemp2);
							strcat(szText,szTemp);
							szText[512] = 0;
						}
					}
				}
			}
			else{
				strcat(szText,"\n\nno edict to display information about\n");
			}
			
			hudtextparms_t message_params;
			
			if(!pInfo)
				pInfo = listenserver_edict;
			
			message_params.x = 0;
			message_params.y = 1;
			message_params.effect = 0;
			message_params.r1 = !(UTIL_GetTeam(pInfo))*255;
			message_params.g1 = 128;
			message_params.b1 = (UTIL_GetTeam(pInfo))*255;
			message_params.a1 = 1;
			message_params.r2 = message_params.r1;
			message_params.g2 = message_params.g1;
			message_params.b2 = message_params.b1;
			message_params.a2 = 1;
			message_params.fadeinTime = 0.00;
			message_params.fadeoutTime = 0;
			message_params.holdTime = gf_5thd+0.02f;
			message_params.fxTime = 0;
			message_params.channel = 1;

			long lNL = CountLines(szText);
			lNL = 13 - lNL;
			for(;lNL;lNL--){
				strcat(szText,"\n");
			}
			
			UTIL_ShowText(listenserver_edict,message_params,szText);
			
			int iWPNear = WaypointFindNearest(listenserver_edict,100,-1,0,false,false,false);
			if(iWPNear != -1){
			/*int ischl;
			
			for(ischl = 0;ischl < num_waypoints;ischl ++){
				if ((waypoints[ischl].flags & W_FL_DELETED) == W_FL_DELETED)
					continue;  // skip any deleted waypoints
				
				if(WPStat.GetVisible(iWPNear,ischl)){
					WaypointDrawBeam(listenserver_edict,waypoints[ischl].origin,waypoints[iWPNear].origin,5,0,200,200,200,200,2);
				}
			}*/
			}
		}
	}
}
#endif

void StartFrame( void )
{
	//try{
	//cout << RANDOM_LONG(0,10) << endl;
	//cout << 1.f/gpGlobals->frametime<<endl;
	lbeam = 0;
	//cout << "-------------------------------------------------------------------------------"<<endl;
	if (gpGlobals->deathmatch)
	{
		edict_t *pPlayer;
		static int i, index, player_index, bot_index;
		static float previous_time = -1.0;
		static float client_update_time = 0.0;
		clientdata_s cd;
		char msg[256];
		int count;
		
		// if a new map has started then (MUST BE FIRST IN StartFrame)...
		if ((gpGlobals->time + 0.1) < previous_time)
		{
			char filename[256];
			char mapname[64];
			
			// check if mapname_bot.cfg file exists...
			
			//strcpy(mapname, STRING(gpGlobals->mapname));
			strcpy(mapname, "bot.cfg");
			UTIL_BuildFileName(filename,"joebot","bot.cfg");
			
			//UTIL_BuildFileName(filename, "maps", mapname);
			
			if ((bot_cfg_fp = fopen(filename, "r")) != NULL)
			{
				sprintf(msg, "JoeBOT: Executing %s\n", filename);
				ALERT( at_console, msg );
				printf(msg);
				
				/*for (index = 0; index < 32; index++)
				{
				bots[index].is_used = FALSE;
				bots[index].respawn_state = 0;
				bots[index].kick_time = 0.0;
			}*/
				if (IS_DEDICATED_SERVER())
					bot_cfg_pause_time = gpGlobals->time + _PAUSE_TIME;
				else
					bot_cfg_pause_time = gpGlobals->time + 20.0;
			}
			/*else
			{*/
			count = 0;
			
			// mark the bots as needing to be respawned...
			for (index = 0; index < 32; index++)
			{
				if (count >= prev_num_bots)
				{
					//bots[index].is_used = FALSE;
					SBInfo[index].respawn_state = 0;
					SBInfo[index].kick_time = 0.0;
				}
				
				if (bots[index])  // is this slot used?
				{
					SBInfo[index].respawn_state = RESPAWN_NEED_TO_RESPAWN;
					count++;
				}
				
				// check for any bots that were very recently kicked...
				if ((SBInfo[index].kick_time + 5.0) > previous_time)
				{
					SBInfo[index].respawn_state = RESPAWN_NEED_TO_RESPAWN;
					count++;
				}
				else
					SBInfo[index].kick_time = 0.0;  // reset to prevent false spawns later
			}
			
			// set the respawn time
			if (IS_DEDICATED_SERVER())
				respawn_time = gpGlobals->time + _PAUSE_TIME;
			else
				respawn_time = gpGlobals->time + _PAUSE_TIME;
			//}
			
			client_update_time = gpGlobals->time + 10.0;  // start updating client data again
			
			//bot_check_time = gpGlobals->time + 30.0;
			bot_check_time = gpGlobals->time + _PAUSE_TIME*2;
		}
		
		if(bool(jb_wprecalc->value)
			&&!bool(jb_wp->value)
			&& num_waypoints)
			WPStat.CalcSlice();
		
		if (gf_5th <= gpGlobals->time){								// this is every .2 s the case
			g_b5th=true;
			if(mod_id == CSTRIKE_DLL||mod_id == CSCLASSIC_DLL)
				f_timesrs = gpGlobals->time - f_round_start;
			//cout << gf_5th << endl;
			gf_5thd = gpGlobals->time - gf_5th + .2f;
			gf_5th = gpGlobals->time + 0.2f;
			
			if(mod_id == CSTRIKE_DLL||mod_id == CSCLASSIC_DLL){
				lGDCount --;
				if(!lGDCount){
					lGDCount = 5;			// one time per second :D
					
					SearchEs_CSTRIKE();
					CalcDistances();
				}
				
				InitGlobalRS();
			}
			else if(mod_id == DOD_DLL){
			}
			
#ifdef _DEBUG
			ShowInfo();
#endif
			
			/* begin all .2s code */
			if(bool(jb_msgwelcome->value)||g_bMyBirthday){
				if(!bDedicatedWelcome
					&& gpGlobals->time > _PAUSE_TIME){
					bDedicatedWelcome = true;
					cout << "*************************************************************" << endl;
					cout << "*************************************************************" << endl;
					cout << "                                                           **" << endl;
					cout << "              JoeBOT is running on this Server             **" << endl;
					cout << "                                                           **" << endl;
					cout << "*************************************************************" << endl;
					cout << "*************************************************************" << endl;
					cout << "**" << endl;
					cout << "** JoeBOT "<< _JOEBOTVERSION << endl;
					cout << "** (c) Johannes Lampel alias @$3.1415rin"<<endl;
					if(g_bMyBirthday){
						cout << "** who celebrates his "<<g_lAge<<". birthday today ! <<<<<<<<<<<"<<endl;
					}
					cout << "**"<<endl;
					cout << "** Please read the readme.html carefully before asking for"<< endl;
					cout << "** support via johannes@joebot.net - thx" << endl;
					cout << "**" << endl;
					cout << "*************************************************************" << endl;
					cout << "*************************************************************" << endl;
				}
				int i;
				edict_t *pEnt;
				if(g_bMyBirthday){
					for (i = gpGlobals->maxClients; i; i--){
						if(!bWelcome[i]){
							pEnt = INDEXENT(i);
							
							// skip invalid players and skip self
							if ((pEnt) && (!pEnt->free)){
								if(IS_DEDICATED_SERVER()
									||( g_fGameCommenced > 0 && g_fGameCommenced < gpGlobals->time - 4.0)){
									// are they out of observer mode yet?
									if (!IsAlive(pEnt)){
										welcome_time[i] = gpGlobals->time + 5;  // welcome in 5 seconds
										continue;
									}
									if(UTIL_GetBotIndex(pEnt) != -1){
										bWelcome[i] = true;
										continue;
									}
									if ((welcome_time[i] > 0.0)
										&& (welcome_time[i] < gpGlobals->time))
									{		
										bWelcome[i] = true;  // clear this so we only do it once
										
										char szOut[1000];
										szOut[0] = 0;
										strcat(szOut,"\n\n\n\nToday, it's July the 8th\n\n");
										strcat(szOut,"This means that this is\n\n");
										strcat(szOut,"The birthday of the creator\n\n");
										strcat(szOut,"Of the bots you are playing\n\n");
										strcat(szOut,"With, @$3.1415rin !\n\n\n");
										
										hudtextparms_t message_params;
										
										message_params.x = -1;
										message_params.y = -1;
										message_params.effect = 2;
										message_params.r1 = 255;
										message_params.g1 = 255;
										message_params.b1 = 255;
										message_params.a1 = 1;
										message_params.r2 = 255;
										message_params.g2 = 0;
										message_params.b2 = 0;
										message_params.a2 = 1;
										message_params.fadeinTime = 0.1;
										message_params.fadeoutTime = 0.9;
										message_params.holdTime = 8;
										message_params.fxTime = 4;
										message_params.channel = 1;
										
										UTIL_ShowText(pEnt,message_params,szOut);
										
										message_params.effect = 2;
										message_params.r1 = 255;
										message_params.g1 = 255;
										message_params.b1 = 255;
										message_params.a1 = 1;
										message_params.r2 = 0;
										message_params.g2 = 255;
										message_params.b2 = 0;
										message_params.a2 = 200;
										message_params.fadeinTime = 0.05;
										message_params.fadeoutTime = 0.9;
										message_params.holdTime = 8;
										message_params.fxTime = 4;
										message_params.channel = 2;
										
										UTIL_ShowText(pEnt,message_params,szOut);
										
										message_params.effect = 1;
										message_params.r1 = 255;
										message_params.g1 = 255;
										message_params.b1 = 255;
										message_params.a1 = 1;
										message_params.r2 = 0;
										message_params.g2 = 0;
										message_params.b2 = 255;
										message_params.a2 = 0;
										message_params.fadeinTime = 4;
										message_params.fadeoutTime = 4;
										message_params.holdTime = 0;
										message_params.fxTime = 4;
										message_params.channel = 3;
										
										UTIL_ShowText(pEnt,message_params,szOut);
									}
								}
							}
						}
					}
				}
				else{
					for (i = gpGlobals->maxClients; i; i--){
						if(!bWelcome[i]){
							pEnt = INDEXENT(i);
							
							// skip invalid players and skip self
							if ((pEnt) && (!pEnt->free)){
								if(IS_DEDICATED_SERVER()
									||( g_fGameCommenced > 0 && g_fGameCommenced < gpGlobals->time - 4.0)){
									// are they out of observer mode yet?
									if (!IsAlive(pEnt)){
										welcome_time[i] = gpGlobals->time + _PAUSE_TIME;  // welcome in 5 seconds
										continue;
									}
									if(pEnt->v.flags & FL_THIRDPARTYBOT){
										bWelcome[i] = true;
										continue;
									}
									if ((welcome_time[i] > 0.0)
										&& (welcome_time[i] < gpGlobals->time))
									{
										cout << "welcoming " << STRING(pEnt->v.netname) << endl;
										// let's send a welcome message to this client...
										//UTIL_SayText(welcome_msg, listenserver_edict);
										
										//welcome_sent = TRUE;  // clear this so we only do it once
										// let's send a welcome message to this client...
										UTIL_SayText(welcome_msg, pEnt);
										
										bWelcome[i] = true;  // clear this so we only do it once
										
										char *szText,*szOut1,*szOut2;
										char szFileName[128];
										char mapname[128];
										
										szText = new char[1000];
										szOut1 = new char[1000];
										szOut2 = new char[1000];
										
										*szOut1 = *szOut2 = '\0';
										strcat (szOut2,"\n\n\n\n");
										
										strcpy(mapname, STRING(gpGlobals->mapname));
										strcat(mapname, ".wpj");
										
										WaypointGetDir(mapname,szFileName);
										
										sprintf(szOut1,"JoeBOT %s using waypoint file:\n%s%s\n",_JOEBOTVERSIONWOOS,szFileName,mapname);
										sprintf(szTemp,"( %li waypoints / stat:%li/%li )",num_waypoints,WPStat.d.lKill,WPStat.lKillMax);
										strcat(szOut1,szTemp);
										
										strcat(szFileName,STRING(gpGlobals->mapname));
										strcat(szFileName,".txt");
										
										long lSize = CParser :: GetFileSize(szFileName);
										FILE *fhd;
										fhd = fopen ( szFileName,"r");
										if(fhd && lSize<480){
											memset(szText,0,1000*sizeof(char));
											fread(szText,lSize,sizeof(char),fhd);
											strcat(szOut2,szText);
											long lNL = CountLines(szOut2);
											for(;lNL;lNL--){
												strcat(szOut1,"\n");
											}
											//UTIL_ShowMenu(clients[welcome_index], 0x1F, 6, FALSE, szText);
											fclose(fhd);
										}
										
										if(!num_waypoints){
											strcat(szOut2,"\n\nWaypoint File couldn't be loaded");
										}
										else{
											if(!fhd){
												strcat(szOut2,"\n\nNo Description File available");
											}
										}
										if(lSize>=480){
											strcat(szOut2,"\n\nThe text in the information file is too long, sorry I can't display it :(");
										}
										
										hudtextparms_t message_params;
										
										message_params.x = -1;
										message_params.y = -1;
										message_params.effect = 1;
										message_params.r1 = 255;
										message_params.g1 = 255;
										message_params.b1 = 255;
										message_params.a1 = 1;
										message_params.r2 = 0;
										message_params.g2 = 255;
										message_params.b2 = 0;
										message_params.a2 = 1;
										message_params.fadeinTime = 4;
										message_params.fadeoutTime = 4;
										message_params.holdTime = 0;
										message_params.fxTime = .1;
										message_params.channel = 2;
										
										UTIL_ShowText(pEnt,message_params,szOut1);
										
										message_params.x = -1;
										message_params.y = -1;
										message_params.effect = 2;
										message_params.r1 = 255;
										message_params.g1 = 255;
										message_params.b1 = 255;
										message_params.a1 = 1;
										message_params.r2 = !(UTIL_GetTeam(pEnt))*255;
										message_params.g2 = 0;
										message_params.b2 = (UTIL_GetTeam(pEnt))*255;
										message_params.a2 = 1;
										message_params.fadeinTime = 0.02;
										message_params.fadeoutTime = 0.6;
										message_params.holdTime = 4;
										message_params.fxTime = 3;
										message_params.channel = 3;
										
										UTIL_ShowText(pEnt,message_params,szOut2);
										delete [] szOut1,szOut2,szText;
									}
								}
							}
						}
					}
				}
			}
			
			if (client_update_time <= gpGlobals->time)
			{
				client_update_time = gpGlobals->time + 1.0;
				
				for (i=0; i < 32; i++)
				{
					if (bots[i])
					{
						memset(&cd, 0, sizeof(cd));
						
#ifdef USE_METAMOD
						MDLL_UpdateClientData( bots[i]->pEdict, 1, &cd );
#else /* not USE_METAMOD */
						UpdateClientData( bots[i]->pEdict, 1, &cd );
#endif /* USE_METAMOD */
						
						// see if a weapon was dropped...
						if (bots[i]->bot_weapons != cd.weapons)
						{
							bots[i]->bot_weapons = cd.weapons;
						}
					}
				}
			}
			/* end all .2s code*/
		}
		else{
			g_b5th=false;
		}
		
		CWG.Think();		// let the gnomes be alive :D
		
		count = 0;
		
		// TheFatal - START from Advanced Bot Framework (Thanks Rich!)
		
		// adjust the millisecond delay based on the frame rate interval...
		if (g_msecdel <= gpGlobals->time)
		{
			g_msecdel = gpGlobals->time + 0.5;
			if (g_msecnum > 0)
				g_msecval = 450.0/g_msecnum;
			g_msecnum = 0;
		}
		else
			g_msecnum++;
		
		if (g_msecval < 5)    // don't allow msec to be less than 5...
			g_msecval = 5;
		
		if (g_msecval > 100)  // ...or greater than 100
			g_msecval = 100;
		
		// TheFatal - END
		
		for (bot_index = 0; bot_index < gpGlobals->maxClients; bot_index++)
		{
			if ((bots[bot_index]) &&  // is this slot used AND
				(SBInfo[bot_index].respawn_state == RESPAWN_IDLE))  // not respawning
			{
				//try{
				if (!bool(jb_jointeam->value) &&
					bots[bot_index]->bot_team != 6 &&
					bots[bot_index]->bot_team > 0 &&
					!UTIL_HumansInGame())
				{
					sprintf(szTemp, "kick \"%s\"\n", STRING(bots[bot_index]->pEdict->v.netname));
					SERVER_COMMAND(szTemp);
				}

				bots[bot_index]->Think();
				
				count++;
				/*}
				catch(...){
				FILE *fhd = fopen("scheisse.txt","a");fprintf(fhd,"scheisse in think\n");fclose(fhd);
			}*/
			}
		}
		
		if (count > num_bots)
			num_bots = count;
		
		for (player_index = 1; player_index <= gpGlobals->maxClients; player_index++)
		{
			pPlayer = INDEXENT(player_index);
			
			if (pPlayer && !pPlayer->free)
			{
				if (bool(jb_wp->value) && FBitSet(pPlayer->v.flags, FL_CLIENT))
				{
					WaypointThink(pPlayer);
				}
				
				/*if ((mod_id == FRONTLINE_DLL) && (flf_bug_check == 0))
				{
				edict_t *pent = NULL;
				int fix_flag = 0;
				
				 flf_bug_check = 1;
				 
				  while ((pent = UTIL_FindEntityByClassname( pent, "capture_point" )) != NULL)
				  {
				  if (pent->v.skin != 0)  // not blue skin?
				  {
				  flf_bug_fix = 1;  // need to use bug fix code
				  }
				  }
            }*/
			}
		}
		
		if (g_GameRules)
		{
			if (need_to_open_cfg)  // have we open bot.cfg file yet?
			{
				char filename[256];
				char mapname[64];
				
				need_to_open_cfg = FALSE;  // only do this once!!!
				
				// check if mapname_bot.cfg file exists...
				
				//strcpy(mapname, STRING(gpGlobals->mapname));
				strcpy(mapname, "bot.cfg");
				UTIL_BuildFileName(filename,"joebot","bot.cfg");
				
				//UTIL_BuildFileName(filename, "maps", mapname);
				
				if ((bot_cfg_fp = fopen(filename, "r")) != NULL)
				{
					sprintf(msg, "JoeBOT: Executing %s\n", filename);
					ALERT( at_console, msg );
					printf(msg);
				}
				else
				{
					UTIL_BuildFileName(filename, "bot.cfg", NULL);
					
					sprintf(msg, "JoeBOT: Executing %s\n", filename);
					ALERT( at_console, msg );
					
					bot_cfg_fp = fopen(filename, "r");
					
					if (bot_cfg_fp == NULL)
						ALERT( at_console, "bot.cfg file not found\n" );
				}
				
				if (IS_DEDICATED_SERVER())
					bot_cfg_pause_time = gpGlobals->time + 2.0;
				else
					bot_cfg_pause_time = gpGlobals->time + 20.0;
			}
			
			if (!IS_DEDICATED_SERVER() && !spawn_time_reset)
			{
				if (listenserver_edict != NULL)
				{
					if (IsAlive(listenserver_edict))
					{
						spawn_time_reset = TRUE;
						
						if (respawn_time >= 1.0)
							respawn_time = min(respawn_time, gpGlobals->time + (float)1.0);
						
						if (bot_cfg_pause_time >= 1.0)
							bot_cfg_pause_time = min(bot_cfg_pause_time, gpGlobals->time + (float)1.0);
					}
				}
			}
			
			if ((bot_cfg_fp) &&
				(bot_cfg_pause_time >= 1.0) && (bot_cfg_pause_time <= gpGlobals->time))
			{
				// process bot.cfg file options...
				ProcessBotCfgFile();
			}
			
		}    
		
		// are we currently respawning bots and is it time to spawn one yet?
		if(g_b5th){
			if (/*(respawn_time > 1.0) && */(respawn_time <= gpGlobals->time))
			{
				int index = 0;
				
				// find bot needing to be respawned...
				while ((index < 32) &&
					(SBInfo[index].respawn_state != RESPAWN_NEED_TO_RESPAWN))
					index++;
				
				if (index < 32)
				{
					if(bool(jb_jointeam->value) || (!bool(jb_jointeam->value) && UTIL_HumansInGame() != 0)){
						SBInfo[index].respawn_state = RESPAWN_IS_RESPAWNING;
						if(bots[index]) delete bots[index];      // free up this slot
						bots[index] = 0;
						
						// respawn 1 bot then wait a while (otherwise engine crashes)
						if ((mod_id == VALVE_DLL) ||
							((mod_id == GEARBOX_DLL) && (pent_info_ctfdetect == NULL)))
						{
							char c_skill[2],c_team[2];
							
							sprintf(c_skill, "%d", SBInfo[index].bot_skill);
							sprintf(c_team, "%d", SBInfo[index].bot_team);
							
							BotCreate(NULL, c_team,SBInfo[index].skin, SBInfo[index].name, c_skill);
						}
						else
						{
							char c_skill[5];
							char c_team[5];
							char c_class[5];
							
							//cout << " ------------------- respawning after map change - wanting to respawn" << endl;
							
							sprintf(c_skill, "%i", SBInfo[index].bot_skill);
							sprintf(c_team, "%i", SBInfo[index].bot_team);
							sprintf(c_class, "%i", SBInfo[index].bot_class);
							
							if ((mod_id == TFC_DLL) || (mod_id == GEARBOX_DLL))
								BotCreate(NULL, NULL, NULL, SBInfo[index].name, c_skill);
							else
								BotCreate(NULL, c_team, c_class, SBInfo[index].name, c_skill);
						}
						
						respawn_time = gpGlobals->time + .5;  // set next respawn time
						
						bot_check_time = gpGlobals->time + _PAUSE_TIME - .1;
					}
					else
						respawn_time = gpGlobals->time + .5;
				}
				else
				{
					respawn_time = 0.0;
				}
				
				// check if time to see if a bot needs to be created...
				if (bot_check_time < gpGlobals->time)
				{
					int count = 0;
					//int counthumans = UTIL_HumansInGame();
					
					bot_check_time = gpGlobals->time + 1.5;		//hackhack
					
					for (i = 0; i < 32; i++)
					{
						if (clients[i] != NULL)
							count++;
					}
					
					// if there are currently less than the maximum number of "players"
					// then add another bot using the default skill level...
					if ((count < int(jb_botsmax->value)) && (int(jb_botsmax->value) != -1) && (count < gpGlobals->maxClients))
					{
						//cout << " ------------------- creating bot due to max_bots" << endl;
						// enter the game if jb_entergame is set or if humans are in the game
						if(bool(jb_entergame->value || UTIL_HumansInGame())){
							BotCreate( NULL, NULL, NULL, NULL, NULL);
						}
					}
					// if there are currently more than the minimum number of bots running
					// then kick one of the bots off the server...
					/*if ((count > int(jb_botsmin->value)) && (int(jb_botsmin->value) != -1))
					{
						for (i=0; i < 32; i++){
							if (bots[i]){  // is this slot used?
								sprintf(szTemp, "kick \"%s\"\n", STRING(bots[i]->pEdict->v.netname));
								
								SERVER_COMMAND(szTemp);  // kick the bot using (kick "name")
								
								break;
							}
						}
					}*/
				}
				
			}  
			
			/*// check if time to see if a bot needs to be created...
			if (bot_check_time < gpGlobals->time)
			{
			int count = 0;
			
			 bot_check_time = gpGlobals->time + 1.5;		//hackhack
			 
			  for (i = 0; i < 32; i++)
			  {
			  if (clients[i] != NULL)
			  count++;
			  }
			  
			   // if there are currently less than the maximum number of "players"
			   // then add another bot using the default skill level...
			   if ((count < int(jb_botsmax->value)) && (int(jb_botsmax->value) != -1))
			   {
			   cout << " ------------------- creating bot due to max_bots" << endl;
			   BotCreate( NULL, NULL, NULL, NULL, NULL);
			   }
      }*/
	  }
	  
      previous_time = gpGlobals->time;
   }
   /* }
   catch(...){
   FILE *fhd = fopen("scheisse.txt","a");fprintf(fhd,"scheisse in startframe\n");fclose(fhd);
   }*/
   
#ifdef USE_METAMOD
   RETURN_META(MRES_IGNORED);
#else /* not USE_METAMOD */
   (*other_gFunctionTable.pfnStartFrame)();
#endif /* USE_METAMOD */
}

#ifndef USE_METAMOD
void ParmsNewLevel( void )
{
	(*other_gFunctionTable.pfnParmsNewLevel)();
}

void ParmsChangeLevel( void )
{
	(*other_gFunctionTable.pfnParmsChangeLevel)();
}

const char *GetGameDescription( void )
{
	return (*other_gFunctionTable.pfnGetGameDescription)();
}

void PlayerCustomization( edict_t *pEntity, customization_t *pCust )
{
	BOT_LOG("PlayerCustomization", "pEntity=%x", pEntity);
	(*other_gFunctionTable.pfnPlayerCustomization)(pEntity, pCust);
}

void SpectatorConnect( edict_t *pEntity )
{
	(*other_gFunctionTable.pfnSpectatorConnect)(pEntity);
}

void SpectatorDisconnect( edict_t *pEntity )
{
	(*other_gFunctionTable.pfnSpectatorDisconnect)(pEntity);
}

void SpectatorThink( edict_t *pEntity )
{
	(*other_gFunctionTable.pfnSpectatorThink)(pEntity);
}

void Sys_Error( const char *error_string )
{
	(*other_gFunctionTable.pfnSys_Error)(error_string);
}

void PM_Move ( struct playermove_s *ppmove, int server )
{
	(*other_gFunctionTable.pfnPM_Move)(ppmove, server);
}

void PM_Init ( struct playermove_s *ppmove )
{
	(*other_gFunctionTable.pfnPM_Init)(ppmove);
}

char PM_FindTextureType( char *name )
{
	return (*other_gFunctionTable.pfnPM_FindTextureType)(name);
}

void SetupVisibility( edict_t *pViewEntity, edict_t *pClient, unsigned char **pvs, unsigned char **pas )
{
	(*other_gFunctionTable.pfnSetupVisibility)(pViewEntity, pClient, pvs, pas);
}

void UpdateClientData ( const struct edict_s *ent, int sendweapons, struct clientdata_s *cd )
{
	(*other_gFunctionTable.pfnUpdateClientData)(ent, sendweapons, cd);
}

int AddToFullPack( struct entity_state_s *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, unsigned char *pSet )
{
	return (*other_gFunctionTable.pfnAddToFullPack)(state, e, ent, host, hostflags, player, pSet);
}

void CreateBaseline( int player, int eindex, struct entity_state_s *baseline, struct edict_s *entity, int playermodelindex, vec3_t player_mins, vec3_t player_maxs )
{
	(*other_gFunctionTable.pfnCreateBaseline)(player, eindex, baseline, entity, playermodelindex, player_mins, player_maxs);
}

void RegisterEncoders( void )
{
	(*other_gFunctionTable.pfnRegisterEncoders)();
}

int GetWeaponData( struct edict_s *player, struct weapon_data_s *info )
{
	return (*other_gFunctionTable.pfnGetWeaponData)(player, info);
}

void CmdStart( const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed )
{
	(*other_gFunctionTable.pfnCmdStart)(player, cmd, random_seed);
}

void CmdEnd ( const edict_t *player )
{
	(*other_gFunctionTable.pfnCmdEnd)(player);
}

int ConnectionlessPacket( const struct netadr_s *net_from, const char *args, char *response_buffer, int *response_buffer_size )
{
	return (*other_gFunctionTable.pfnConnectionlessPacket)(net_from, args, response_buffer, response_buffer_size);
}

int GetHullBounds( int hullnumber, float *mins, float *maxs )
{
	return (*other_gFunctionTable.pfnGetHullBounds)(hullnumber, mins, maxs);
}

void CreateInstancedBaselines( void )
{
	(*other_gFunctionTable.pfnCreateInstancedBaselines)();
}

int InconsistentFile( const edict_t *player, const char *filename, char *disconnect_message )
{
	BOT_LOG("InconsistentFile", "player=%x, filename=%s", player, filename);
	return (*other_gFunctionTable.pfnInconsistentFile)(player, filename, disconnect_message);
}

int AllowLagCompensation( void )
{
	return (*other_gFunctionTable.pfnAllowLagCompensation)();
}
#endif /* not USE_METAMOD */

#ifdef USE_METAMOD
DLL_FUNCTIONS gFunctionTable =
{
	GameDLLInit,               //pfnGameInit
	DispatchSpawn,             //pfnSpawn
	NULL,                      //pfnThink
	NULL,                      //pfnUse
	NULL,                      //pfnTouch
	NULL,                      //pfnBlocked
	NULL,                      //pfnKeyValue
	NULL,                      //pfnSave
	NULL,                      //pfnRestore
	NULL,                      //pfnAbsBox

	NULL,                      //pfnSaveWriteFields
	NULL,                      //pfnSaveReadFields

	NULL,                      //pfnSaveGlobalState
	NULL,                      //pfnRestoreGlobalState
	NULL,                      //pfnResetGlobalState

	ClientConnect,             //pfnClientConnect
	ClientDisconnect,          //pfnClientDisconnect
	ClientKill,                //pfnClientKill
	ClientPutInServer,         //pfnClientPutInServer
	ClientCommand,             //pfnClientCommand
	NULL,                      //pfnClientUserInfoChanged
	NULL,                      //pfnServerActivate
	NULL,                      //pfnServerDeactivate

	NULL,                      //pfnPlayerPreThink
	NULL,                      //pfnPlayerPostThink

	StartFrame,                //pfnStartFrame
	NULL,                      //pfnParmsNewLevel
	NULL,                      //pfnParmsChangeLevel

	NULL,                      //pfnGetGameDescription    Returns string describing current .dll game.
	NULL,                      //pfnPlayerCustomization   Notifies .dll of new customization for player.

	NULL,                      //pfnSpectatorConnect      Called when spectator joins server
	NULL,                      //pfnSpectatorDisconnect   Called when spectator leaves the server
	NULL,                      //pfnSpectatorThink        Called when spectator sends a command packet (usercmd_t)

	NULL,                      //pfnSys_Error          Called when engine has encountered an error

	NULL,                      //pfnPM_Move
	NULL,                      //pfnPM_Init            Server version of player movement initialization
	NULL,                      //pfnPM_FindTextureType

	NULL,                      //pfnSetupVisibility        Set up PVS and PAS for networking for this client
	NULL,                      //pfnUpdateClientData       Set up data sent only to specific client
	NULL,                      //pfnAddToFullPack
	NULL,                      //pfnCreateBaseline        Tweak entity baseline for network encoding, allows setup of player baselines, too.
	NULL,                      //pfnRegisterEncoders      Callbacks for network encoding
	NULL,                      //pfnGetWeaponData
	NULL,                      //pfnCmdStart
	NULL,                      //pfnCmdEnd
	NULL,                      //pfnConnectionlessPacket
	NULL,                      //pfnGetHullBounds
	NULL,                      //pfnCreateInstancedBaselines
	NULL,                      //pfnInconsistentFile
	NULL,                      //pfnAllowLagCompensation
};
#else /* not USE_METAMOD */
DLL_FUNCTIONS gFunctionTable =
{
	GameDLLInit,               //pfnGameInit
	DispatchSpawn,             //pfnSpawn
	DispatchThink,             //pfnThink
	DispatchUse,               //pfnUse
	DispatchTouch,             //pfnTouch
	DispatchBlocked,           //pfnBlocked
	DispatchKeyValue,          //pfnKeyValue
	DispatchSave,              //pfnSave
	DispatchRestore,           //pfnRestore
	DispatchObjectCollsionBox, //pfnAbsBox

	SaveWriteFields,           //pfnSaveWriteFields
	SaveReadFields,            //pfnSaveReadFields
	
	SaveGlobalState,           //pfnSaveGlobalState
	RestoreGlobalState,        //pfnRestoreGlobalState
	ResetGlobalState,          //pfnResetGlobalState
	
	ClientConnect,             //pfnClientConnect
	ClientDisconnect,          //pfnClientDisconnect
	ClientKill,                //pfnClientKill
	ClientPutInServer,         //pfnClientPutInServer
	ClientCommand,             //pfnClientCommand
	ClientUserInfoChanged,     //pfnClientUserInfoChanged
	ServerActivate,            //pfnServerActivate
	ServerDeactivate,          //pfnServerDeactivate
	
	PlayerPreThink,            //pfnPlayerPreThink
	PlayerPostThink,           //pfnPlayerPostThink
	
	StartFrame,                //pfnStartFrame
	ParmsNewLevel,             //pfnParmsNewLevel
	ParmsChangeLevel,          //pfnParmsChangeLevel
	
	GetGameDescription,        //pfnGetGameDescription    Returns string describing current .dll game.
	PlayerCustomization,       //pfnPlayerCustomization   Notifies .dll of new customization for player.
	
	SpectatorConnect,          //pfnSpectatorConnect      Called when spectator joins server
	SpectatorDisconnect,       //pfnSpectatorDisconnect   Called when spectator leaves the server
	SpectatorThink,            //pfnSpectatorThink        Called when spectator sends a command packet (usercmd_t)
	
	Sys_Error,                 //pfnSys_Error          Called when engine has encountered an error
	
	PM_Move,                   //pfnPM_Move
	PM_Init,                   //pfnPM_Init            Server version of player movement initialization
	PM_FindTextureType,        //pfnPM_FindTextureType
	
	SetupVisibility,           //pfnSetupVisibility        Set up PVS and PAS for networking for this client
	UpdateClientData,          //pfnUpdateClientData       Set up data sent only to specific client
	AddToFullPack,             //pfnAddToFullPack
	CreateBaseline,            //pfnCreateBaseline        Tweak entity baseline for network encoding, allows setup of player baselines, too.
	RegisterEncoders,          //pfnRegisterEncoders      Callbacks for network encoding
	GetWeaponData,             //pfnGetWeaponData
	CmdStart,                  //pfnCmdStart
	CmdEnd,                    //pfnCmdEnd
	ConnectionlessPacket,      //pfnConnectionlessPacket
	GetHullBounds,             //pfnGetHullBounds
	CreateInstancedBaselines,  //pfnCreateInstancedBaselines
	InconsistentFile,          //pfnInconsistentFile
	AllowLagCompensation,      //pfnAllowLagCompensation
};
#endif /* USE_METAMOD */

#ifdef __BORLANDC__
int EXPORT GetEntityAPI( DLL_FUNCTIONS *pFunctionTable, int interfaceVersion )
#else
extern "C" EXPORT int GetEntityAPI( DLL_FUNCTIONS *pFunctionTable, int interfaceVersion )
#endif
{
	// check if engine's pointer is valid and version is correct...
	
	if ( !pFunctionTable || interfaceVersion != INTERFACE_VERSION )
		return FALSE;
	
	// pass engine callback function table to engine...
	memcpy( pFunctionTable, &gFunctionTable, sizeof( DLL_FUNCTIONS ) );
	
#ifndef USE_METAMOD
	// pass other DLLs engine callbacks to function table...
	if (!(*other_GetEntityAPI)(&other_gFunctionTable, INTERFACE_VERSION))
	{
		return FALSE;  // error initializing function table!!!
	}
#endif /* not USE_METAMOD */
	
	return TRUE;
}


#ifndef USE_METAMOD
#ifdef __BORLANDC__
int EXPORT GetNewDLLFunctions( NEW_DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
#else
extern "C" EXPORT int GetNewDLLFunctions( NEW_DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
#endif
{
	if (other_GetNewDLLFunctions == NULL)
		return FALSE;
	
	// pass other DLLs engine callbacks to function table...
	if (!(*other_GetNewDLLFunctions)(pFunctionTable, interfaceVersion)) {
		return FALSE;  // error initializing function table!!!
	}
	
	return TRUE;
}
#endif /* not USE_METAMOD */

void FakeClientCommand (edict_t *pFakeClient, const char *fmt, ...)
{
   // this function is from Pierre-Marie Baty's RACC 

   // the purpose of this function is to provide fakeclients (bots) with the same client
   // command-scripting advantages (putting multiple commands in one line between semicolons)
   // as real players. It is an improved version of botman's FakeClientCommand, in which you
   // supply directly the whole string as if you were typing it in the bot's "console". It
   // is supposed to work exactly like the pfnClientCommand (server-sided client command).

   va_list argptr;
   static char command[256];
   int length, fieldstart, fieldstop, i, index, stringindex = 0;

   // concatenate all the arguments in one string
   va_start (argptr, fmt);
   vsprintf (command, fmt, argptr);
   va_end (argptr);

   if ((command == NULL) || (*command == 0))
      return; // if nothing in the command buffer, return

   isFakeClientCommand = TRUE; // set the "fakeclient command" flag
   length = strlen (command); // get the total length of the command string

   // process all individual commands (separated by a semicolon) one each a time
   while (stringindex < length)
   {
      fieldstart = stringindex; // save field start position (first character)
      while ((stringindex < length) && (command[stringindex] != ';'))
         stringindex++; // reach end of field
      if (command[stringindex - 1] == '\n')
         fieldstop = stringindex - 2; // discard any trailing '\n' if needed
      else
         fieldstop = stringindex - 1; // save field stop position (last character before semicolon or end)
      for (i = fieldstart; i <= fieldstop; i++)
         g_argv[i - fieldstart] = command[i]; // store the field value in the g_argv global string
      g_argv[i - fieldstart] = 0; // terminate the string
      stringindex++; // move the overall string index one step further to bypass the semicolon

      index = 0;
      fake_arg_count = 0; // let's now parse that command and count the different arguments

      // count the number of arguments
      while (index < i - fieldstart)
      {
         while ((index < i - fieldstart) && (g_argv[index] == ' '))
            index++; // ignore spaces

         // is this field a group of words between quotes or a single word ?
         if (g_argv[index] == '"')
         {
            index++; // move one step further to bypass the quote
            while ((index < i - fieldstart) && (g_argv[index] != '"'))
               index++; // reach end of field
            index++; // move one step further to bypass the quote
         }
         else
            while ((index < i - fieldstart) && (g_argv[index] != ' '))
               index++; // this is a single word, so reach the end of field

         fake_arg_count++; // we have processed one argument more
      }

#ifdef USE_METAMOD
      MDLL_ClientCommand(pFakeClient);
#else /* not USE_METAMOD */
      ClientCommand (pFakeClient); // tell now the MOD DLL to execute this ClientCommand...
#endif /* USE_METAMOD */
   }

   g_argv[0] = 0; // when it's done, reset the g_argv field
   isFakeClientCommand = FALSE; // reset the "fakeclient command" flag
   fake_arg_count = 0; // and the argument count
}

const char *GetField (const char *string, int field_number)
{
   // this function is from Pierre-Marie Baty's RACC 

   // This function gets and returns a particuliar field in a string where several fields are
   // concatenated. Fields can be words, or groups of words between quotes ; separators may be
   // white space or tabs. A purpose of this function is to provide bots with the same Cmd_Argv
   // convenience the engine provides to real clients. This way the handling of real client
   // commands and bot client commands is exactly the same, just have a look in engine.cpp
   // for the hooking of pfnCmd_Argc, pfnCmd_Args and pfnCmd_Argv, which redirects the call
   // either to the actual engine functions (when the caller is a real client), either on
   // our function here, which does the same thing, when the caller is a bot.

   static char field[256];
   int length, i, index = 0, field_count = 0, fieldstart, fieldstop;

   field[0] = 0; // reset field
   length = strlen (string); // get length of string

   // while we have not reached end of line
   while ((index < length) && (field_count <= field_number))
   {
      while ((index < length) && ((string[index] == ' ') || (string[index] == '\t')))
         index++; // ignore spaces or tabs

      // is this field multi-word between quotes or single word ?
      if (string[index] == '"')
      {
         index++; // move one step further to bypass the quote
         fieldstart = index; // save field start position
         while ((index < length) && (string[index] != '"'))
            index++; // reach end of field
         fieldstop = index - 1; // save field stop position
         index++; // move one step further to bypass the quote
      }
      else
      {
         fieldstart = index; // save field start position
         while ((index < length) && ((string[index] != ' ') && (string[index] != '\t')))
            index++; // reach end of field
         fieldstop = index - 1; // save field stop position
      }

      // is this field we just processed the wanted one ?
      if (field_count == field_number)
      {
         for (i = fieldstart; i <= fieldstop; i++)
            field[i - fieldstart] = string[i]; // store the field value in a string
         field[i - fieldstart] = 0; // terminate the string
         break; // and stop parsing
      }

      field_count++; // we have parsed one field more
   }

   return (&field[0]); // returns the wanted field
}

void ProcessBotCfgFile(void)
{
	int iLPF;
	int ch;
	char cmd_line[256];
	int cmd_index;
	static char server_cmd[256];
	char *cmd, *arg1, *arg2, *arg3, *arg4,*arg5, *arg6;
	//	char msg[80];
	
	if (bot_cfg_pause_time > gpGlobals->time)
		return;
	
	for(iLPF = 0;iLPF < _MAXCFGLINESPERFRAME;iLPF++){
		if (bot_cfg_fp == NULL)
			return;
		
		cmd_index = 0;
		cmd_line[cmd_index] = 0;
		
		ch = fgetc(bot_cfg_fp);
		
		// skip any leading blanks
		while (ch == ' ')
			ch = fgetc(bot_cfg_fp);
		
		while ((ch != EOF) && (ch != '\r') && (ch != '\n'))
		{
			if (ch == '\t')  // convert tabs to spaces
				ch = ' ';
			
			cmd_line[cmd_index] = ch;
			
			ch = fgetc(bot_cfg_fp);
			
			// skip multiple spaces in input file
			while ((cmd_line[cmd_index] == ' ') &&
				(ch == ' '))      
				ch = fgetc(bot_cfg_fp);
			
			cmd_index++;
		}
		
		if (ch == '\r')  // is it a carriage return?
		{
			ch = fgetc(bot_cfg_fp);  // skip the linefeed
		}
		
		// if reached end of file, then close it
		if (ch == EOF)
		{
			fclose(bot_cfg_fp);
			
			bot_cfg_fp = NULL;
			
			bot_cfg_pause_time = 0.0;
		}
		
		cmd_line[cmd_index] = 0;  // terminate the command line
		
		// copy the command line to a server command buffer...
		strcpy(server_cmd, cmd_line);
		strcat(server_cmd, "\n");
		
		cmd_index = 0;
		cmd = cmd_line;
		arg1 = arg2 = arg3 = arg4 = arg5 = arg6 = NULL;
		
		// skip to blank or end of string...
		while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
			cmd_index++;
		
		if (cmd_line[cmd_index] == ' ')
		{
			cmd_line[cmd_index++] = 0;
			arg1 = &cmd_line[cmd_index];
			
			// skip to blank or end of string...
			while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
				cmd_index++;
			
			if (cmd_line[cmd_index] == ' ')
			{
				cmd_line[cmd_index++] = 0;
				arg2 = &cmd_line[cmd_index];
				
				// skip to blank or end of string...
				while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
					cmd_index++;
				
				if (cmd_line[cmd_index] == ' ')
				{
					cmd_line[cmd_index++] = 0;
					arg3 = &cmd_line[cmd_index];
					
					// skip to blank or end of string...
					while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
						cmd_index++;
					
					if (cmd_line[cmd_index] == ' ')
					{
						cmd_line[cmd_index++] = 0;
						arg4 = &cmd_line[cmd_index];
						
						// skip to blank or end of string...
						while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
							cmd_index++;
						
						if (cmd_line[cmd_index] == ' ')
						{
							cmd_line[cmd_index++] = 0;
							arg5 = &cmd_line[cmd_index];
							
							// skip to blank or end of string...
							while ((cmd_line[cmd_index] != ' ') && (cmd_line[cmd_index] != 0))
								cmd_index++;
							
							if (cmd_line[cmd_index] == ' ')
							{
								cmd_line[cmd_index++] = 0;
								arg6 = &cmd_line[cmd_index];
							}
						}
					}
				}
			}
		}
		
		//printf("%s-%s-%s-%s-%s\n",cmd,arg1,arg2,arg3,arg4);
		if ((cmd_line[0] == '#') || (cmd_line[0] == 0))
			continue;  // return if comment or blank line
		
		if(Commands.Exec(0,CM_SCRIPT,cmd,arg1,arg2,arg3,arg4))
			continue;
		
		//sprintf(msg, "JoeBOT : executing server command: %s - is the syntax right ?\n", server_cmd);
		//ALERT( at_console, msg );
		
		/*if (IS_DEDICATED_SERVER())
		printf(msg);*/
		
		SERVER_COMMAND(server_cmd);
	}
}
