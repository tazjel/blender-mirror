/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file SCA_JoystickManager.h
 *  \ingroup gamelogic
 */

#ifndef __JOYSTICKMANAGER_H_
#define __JOYSTICKMANAGER_H_

#include "SCA_EventManager.h"
#include "Joystick/SCA_Joystick.h"
#include <vector>

using namespace std;
class SCA_JoystickManager : public SCA_EventManager
{
	/**
	 * SDL Joystick Class Instance
	 */
	SCA_Joystick *m_joystick[JOYINDEX_MAX];
public:
	SCA_JoystickManager(class SCA_LogicManager* logicmgr);
	virtual ~SCA_JoystickManager();
	virtual void NextFrame(double curtime,double deltatime);
	SCA_Joystick* GetJoystickDevice(short int joyindex);


#ifdef WITH_CXX_GUARDEDALLOC
public:
	void *operator new(size_t num_bytes) { return MEM_mallocN(num_bytes, "GE:SCA_JoystickManager"); }
	void operator delete( void *mem ) { MEM_freeN(mem); }
#endif
};

#endif

