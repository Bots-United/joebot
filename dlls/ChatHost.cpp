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
// ChatHost.cpp: implementation of the CChatHost class.
//
//////////////////////////////////////////////////////////////////////

#include "ChatHost.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CChatHost::CChatHost()
{
	pData = 0;
}

CChatHost::~CChatHost()
{
	CChatHostData *p;

	p = pData;
	if(p){
		while(p->next){
			delete p->next;
		}
		delete p;
		pData = 0;
	}
}

CChat *CChatHost :: GetChat(const char *szNameP){
	char szName[256];

#ifndef __linux__
		UTIL_BuildFileName(szName,"joebot\\chat",szNameP);
#else
		UTIL_BuildFileName(szName,"joebot/chat",szNameP);
#endif

	//cout << "wanting "<<szName<<endl;
	CChatHostData *p = 0,*pp = 0;

	p = pData;
	while(p){
		if(!strcmp(p->szFile,szName)){
			p->iUsed ++;
			return p->pChat;
		}
		pp = p;
		p = p->next;
	}
	// p is 0 ... we need to make a new data elem and load the file
	p = new CChatHostData;
	if(!pData)
		pData = p;
	if(pp){
		pp->next = p;
		pp->next->prev = pp;
	}

	if(!(p->pChat->LoadFile(szName))){
		if(p == pData)
			pData = 0;
		delete p;
		return 0;
	}
	strcpy(p->szFile,szName);

	p->iUsed ++;
	return p->pChat;
}

int CChatHost :: DisconnectChat(const char *szNameP){
	char szName[80];

#ifndef __linux__
		UTIL_BuildFileName(szName,"joebot\\chat",szNameP);
#else
		UTIL_BuildFileName(szName,"joebot/chat",szNameP);
#endif

	CChatHostData 
		*p = 0,
		*pp = 0,
		*pF=0;

	p = pData;
	while(p){
		if(!strcmp(p->szFile,szName)){
			pF = p;
			break;
		}
		pp = p;
		p = p->next;
	}
	if(pF){
		pF->iUsed --;		// decrement the counter
		if(!pF->iUsed){
			cout << "JoeBOT : Unloading chat file : " << szName << endl;
			delete pF;
			if(pF == pData)
				pData = 0;
			return 1;
		}
	}
	return 0;
}