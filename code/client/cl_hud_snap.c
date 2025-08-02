/*
===========================================================================
This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
===========================================================================
*/
// cl_hud.c -- additional head-up display tools

#include "client.h"

cvar_t	*scr_hud_snap_draw;
cvar_t	*scr_hud_snap_rgba1;
cvar_t	*scr_hud_snap_rgba2;
cvar_t	*scr_hud_snap_y;
cvar_t	*scr_hud_snap_h;
cvar_t	*scr_hud_snap_auto;
cvar_t	*scr_hud_snap_def;
cvar_t	*scr_hud_snap_speed;

cvar_t	*scr_hud_pitch;
cvar_t	*scr_hud_pitch_rgba;
cvar_t	*scr_hud_pitch_thickness;
cvar_t	*scr_hud_pitch_width;
cvar_t	*scr_hud_pitch_x;

//=============================================================================

static int QDECL sortzones( const void *a, const void *b ) {
  return *(float *)a - *(float *)b;
}

void HUD_UpdateSnappingSettings (float speed) {
  float	step;
  const char	*info;

  cl.snappinghud.speed=speed;
  speed/=125;
  cl.snappinghud.count = 0;

  for(step=floor(speed+0.5)-0.5;step>0 && cl.snappinghud.count<SNAPHUD_MAXZONES-2;step--){
    cl.snappinghud.zones[cl.snappinghud.count]=RAD2DEG(acos(step/speed));
    cl.snappinghud.count++;
    cl.snappinghud.zones[cl.snappinghud.count]=RAD2DEG(asin(step/speed));
    cl.snappinghud.count++;
  }

  qsort(cl.snappinghud.zones,cl.snappinghud.count,sizeof(cl.snappinghud.zones[0]),sortzones);
  cl.snappinghud.zones[cl.snappinghud.count]=cl.snappinghud.zones[0]+90;

  info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
  cl.snappinghud.promode = atoi(Info_ValueForKey(info, "df_promode"));
}

/*
==============
HUD_DrawSnapping
==============
*/
void HUD_DrawSnapping ( float yaw ) {
  int i,y,h;
  char *t;
  vec4_t	color[3];
  float speed;
  int colorid = 0;

  if (cl.snap.ps.pm_flags & PMF_FOLLOW || clc.demoplaying) {
    cl.snappinghud.m[0]=(cl.snap.ps.stats[13] & 1) - (cl.snap.ps.stats[13] & 2);
    cl.snappinghud.m[1]=(cl.snap.ps.stats[13] & 8) - (cl.snap.ps.stats[13] & 16);
  }

  if (!scr_hud_snap_draw->integer) {
    return;
  }

  speed = scr_hud_snap_speed->integer ? scr_hud_snap_speed->integer : cl.snap.ps.speed;
  if (speed!=cl.snappinghud.speed)
    HUD_UpdateSnappingSettings(speed);

  y = scr_hud_snap_y->value;
  h = scr_hud_snap_h->value;

  switch (scr_hud_snap_auto->integer) {
    case 0:
      yaw+=scr_hud_snap_def->value;
      break;
    case 1:
      if (cl.snappinghud.promode || (cl.snappinghud.m[0]!=0 && cl.snappinghud.m[1]!=0)){
        yaw+=45;
      } else if (cl.snappinghud.m[0]==0 && cl.snappinghud.m[1]==0){
        yaw+=scr_hud_snap_def->value;
      }
      break;
    case 2:
      if (cl.snappinghud.m[0]!=0 && cl.snappinghud.m[1]!=0){
        yaw+=45;
      } else if (cl.snappinghud.m[0]==0 && cl.snappinghud.m[1]==0){
        yaw+=scr_hud_snap_def->value;
      }
      break;
  }

  t = scr_hud_snap_rgba2->string;
  color[1][0] = atof(COM_Parse(&t));
  color[1][1] = atof(COM_Parse(&t));
  color[1][2] = atof(COM_Parse(&t));
  color[1][3] = atof(COM_Parse(&t));

  t = scr_hud_snap_rgba1->string;
  color[0][0] = atof(COM_Parse(&t));
  color[0][1] = atof(COM_Parse(&t));
  color[0][2] = atof(COM_Parse(&t));
  color[0][3] = atof(COM_Parse(&t));

  for(i=0;i<cl.snappinghud.count;i++){
    SCR_FillAngleYaw( cl.snappinghud.zones[i],cl.snappinghud.zones[i+1], yaw, y, h, color[colorid]);
    SCR_FillAngleYaw( cl.snappinghud.zones[i]+90,cl.snappinghud.zones[i+1]+90, yaw, y, h, color[colorid]);
    colorid^=1;
  }
}

/*
==============
HUD_DrawPitch
==============
*/
void HUD_DrawPitch ( float pitch ) {
  char *t;
  vec4_t	color[3];
  float mark;

  t = scr_hud_pitch_rgba->string;
  color[2][0] = atof(COM_Parse(&t));
  color[2][1] = atof(COM_Parse(&t));
  color[2][2] = atof(COM_Parse(&t));
  color[2][3] = atof(COM_Parse(&t));

  t = scr_hud_pitch->string;
  mark = atof(COM_Parse(&t));
  while (mark){
    SCR_MarkAnglePitch( mark, scr_hud_pitch_thickness->value, pitch, scr_hud_pitch_x->value, scr_hud_pitch_width->value, color[2] );
    mark = atof(COM_Parse(&t));
  }
}

/*
==============
HUD_Draw
==============
*/
void HUD_Draw (void) {
  vec2_t va;

  if (!Cvar_VariableIntegerValue("cg_draw2D")) {
    return;
  }

  if (cl.snap.ps.pm_flags & PMF_FOLLOW || clc.demoplaying){
    va[YAW] = cl.snap.ps.viewangles[YAW];
    va[PITCH] = -cl.snap.ps.viewangles[PITCH];
  } else if (cl.snap.ps.pm_type==0){
    va[YAW] = cl.viewangles[YAW]+SHORT2ANGLE(cl.snap.ps.delta_angles[YAW]);
    va[PITCH] = -(cl.viewangles[PITCH]+SHORT2ANGLE(cl.snap.ps.delta_angles[PITCH]));
  } else {
    return;
  }

  if (scr_hud_snap_draw->integer) {
    HUD_DrawSnapping ( va[YAW] );
  }

  HUD_DrawPitch ( va[PITCH] );
}

/*
==================
HUD_Init
==================
*/
void HUD_Init( void ) {
  scr_hud_snap_draw = Cvar_Get ("scr_hud_snap_draw", "0", CVAR_ARCHIVE);
  scr_hud_snap_rgba1 = Cvar_Get ("scr_hud_snap_rgba1", ".02 .1 .02 .4", CVAR_ARCHIVE);
  scr_hud_snap_rgba2 = Cvar_Get ("scr_hud_snap_rgba2", ".05 .05 .05 .1", CVAR_ARCHIVE);
  scr_hud_snap_y = Cvar_Get ("scr_hud_snap_y", "248", CVAR_ARCHIVE);
  scr_hud_snap_h = Cvar_Get ("scr_hud_snap_h", "8", CVAR_ARCHIVE);
  scr_hud_snap_auto = Cvar_Get ("scr_hud_snap_auto", "1", CVAR_ARCHIVE);
  scr_hud_snap_def = Cvar_Get ("scr_hud_snap_def", "45", CVAR_ARCHIVE);
  scr_hud_snap_speed = Cvar_Get ("scr_hud_snap_speed", "0", CVAR_ARCHIVE);
  scr_hud_pitch = Cvar_Get ("scr_hud_pitch", "", CVAR_ARCHIVE);
  scr_hud_pitch_thickness = Cvar_Get ("scr_hud_pitch_thickness", "2", CVAR_ARCHIVE);
  scr_hud_pitch_x = Cvar_Get ("scr_hud_pitch_x", "320", CVAR_ARCHIVE);
  scr_hud_pitch_width = Cvar_Get ("scr_hud_pitch_width", "10", CVAR_ARCHIVE);
  scr_hud_pitch_rgba = Cvar_Get ("scr_hud_pitch_rgba", ".8 .8 .8 .8", CVAR_ARCHIVE);
}
