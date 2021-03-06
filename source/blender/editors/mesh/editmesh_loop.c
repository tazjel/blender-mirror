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
 * The Original Code is Copyright (C) 2004 by Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/mesh/editmesh_loop.c
 *  \ingroup edmesh
 */


/*

editmesh_loop: tools with own drawing subloops, select, knife, subdiv

*/

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MEM_guardedalloc.h"


#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_editVert.h"
#include "BLI_ghash.h"

#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_mesh.h"

#include "PIL_time.h"

#include "BIF_gl.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_mesh.h"
#include "ED_view3d.h"

#include "mesh_intern.h"

/* **** XXX ******** */
static void error(const char *UNUSED(arg)) {}
/* **** XXX ******** */

/*   ***************** TRAIL ************************

Read a trail of mouse coords and return them as an array of CutCurve structs
len returns number of mouse coords read before commiting with RETKEY   
It is up to the caller to free the block when done with it,

XXX Is only used here, so local inside this file (ton)
 */

#define TRAIL_POLYLINE 1 /* For future use, They don't do anything yet */
#define TRAIL_FREEHAND 2
#define TRAIL_MIXED    3 /* (1|2) */
#define TRAIL_AUTO     4 
#define	TRAIL_MIDPOINTS 8

typedef struct CutCurve {
	float  x; 
	float  y;
} CutCurve;


/* ******************************************************************** */
/* Knife Subdivide Tool.  Subdivides edges intersected by a mouse trail
	drawn by user.
	
	Currently mapped to KKey when in MeshEdit mode.
	Usage:
		Hit Shift K, Select Centers or Exact
		Hold LMB down to draw path, hit RETKEY.
		ESC cancels as expected.
   
	Contributed by Robert Wenzlaff (Det. Thorn).

	2.5 revamp:
	- non modal (no menu before cutting)
	- exit on mouse release
	- polygon/segment drawing can become handled by WM cb later

*/

#define KNIFE_EXACT		1
#define KNIFE_MIDPOINT	2
#define KNIFE_MULTICUT	3

static EnumPropertyItem knife_items[]= {
	{KNIFE_EXACT, "EXACT", 0, "Exact", ""},
	{KNIFE_MIDPOINT, "MIDPOINTS", 0, "Midpoints", ""},
	{KNIFE_MULTICUT, "MULTICUT", 0, "Multicut", ""},
	{0, NULL, 0, NULL, NULL}
};

/* seg_intersect() Determines if and where a mouse trail intersects an EditEdge */

static float seg_intersect(EditEdge *e, CutCurve *c, int len, char mode, struct GHash *gh)
{
#define MAXSLOPE 100000
	float  x11, y11, x12=0, y12=0, x2max, x2min, y2max;
	float  y2min, dist, lastdist=0, xdiff2, xdiff1;
	float  m1, b1, m2, b2, x21, x22, y21, y22, xi;
	float  yi, x1min, x1max, y1max, y1min, perc=0; 
	float  *scr;
	float  threshold;
	int  i;
	
	threshold = 0.000001; /*tolerance for vertex intersection*/
	// XXX	threshold = scene->toolsettings->select_thresh / 100;
	
	/* Get screen coords of verts */
	scr = BLI_ghash_lookup(gh, e->v1);
	x21=scr[0];
	y21=scr[1];
	
	scr = BLI_ghash_lookup(gh, e->v2);
	x22=scr[0];
	y22=scr[1];
	
	xdiff2=(x22-x21);  
	if (xdiff2) {
		m2=(y22-y21)/xdiff2;
		b2= ((x22*y21)-(x21*y22))/xdiff2;
	}
	else {
		m2=MAXSLOPE;  /* Verticle slope  */
		b2=x22;      
	}
	
	/*check for *exact* vertex intersection first*/
	if(mode!=KNIFE_MULTICUT){
		for (i=0; i<len; i++){
			if (i>0){
				x11=x12;
				y11=y12;
			}
			else {
				x11=c[i].x;
				y11=c[i].y;
			}
			x12=c[i].x;
			y12=c[i].y;
			
			/*test e->v1*/
			if((x11 == x21 && y11 == y21) || (x12 == x21 && y12 == y21)){
				e->v1->f1 = 1;
				perc = 0;
				return(perc);
			}
			/*test e->v2*/
			else if((x11 == x22 && y11 == y22) || (x12 == x22 && y12 == y22)){
				e->v2->f1 = 1;
				perc = 0;
				return(perc);
			}
		}
	}
	
	/*now check for edge interesect (may produce vertex intersection as well)*/
	for (i=0; i<len; i++){
		if (i>0){
			x11=x12;
			y11=y12;
		}
		else {
			x11=c[i].x;
			y11=c[i].y;
		}
		x12=c[i].x;
		y12=c[i].y;
		
		/* Perp. Distance from point to line */
		if (m2!=MAXSLOPE) dist=(y12-m2*x12-b2);/* /sqrt(m2*m2+1); Only looking for */
			/* change in sign.  Skip extra math */	
		else dist=x22-x12;	
		
		if (i==0) lastdist=dist;
		
		/* if dist changes sign, and intersect point in edge's Bound Box*/
		if ((lastdist*dist)<=0){
			xdiff1=(x12-x11); /* Equation of line between last 2 points */
			if (xdiff1){
				m1=(y12-y11)/xdiff1;
				b1= ((x12*y11)-(x11*y12))/xdiff1;
			}
			else{
				m1=MAXSLOPE;
				b1=x12;
			}
			x2max=MAX2(x21,x22)+0.001f; /* prevent missed edges   */
			x2min=MIN2(x21,x22)-0.001f; /* due to round off error */
			y2max=MAX2(y21,y22)+0.001f;
			y2min=MIN2(y21,y22)-0.001f;
			
			/* Found an intersect,  calc intersect point */
			if (m1==m2){ /* co-incident lines */
				/* cut at 50% of overlap area*/
				x1max=MAX2(x11, x12);
				x1min=MIN2(x11, x12);
				xi= (MIN2(x2max,x1max)+MAX2(x2min,x1min))/2.0f;
				
				y1max=MAX2(y11, y12);
				y1min=MIN2(y11, y12);
				yi= (MIN2(y2max,y1max)+MAX2(y2min,y1min))/2.0f;
			}			
			else if (m2==MAXSLOPE){ 
				xi=x22;
				yi=m1*x22+b1;
			}
			else if (m1==MAXSLOPE){ 
				xi=x12;
				yi=m2*x12+b2;
			}
			else {
				xi=(b1-b2)/(m2-m1);
				yi=(b1*m2-m1*b2)/(m2-m1);
			}
			
			/* Intersect inside bounding box of edge?*/
			if ((xi>=x2min)&&(xi<=x2max)&&(yi<=y2max)&&(yi>=y2min)){
				/*test for vertex intersect that may be 'close enough'*/
				if(mode!=KNIFE_MULTICUT){
					if(xi <= (x21 + threshold) && xi >= (x21 - threshold)){
						if(yi <= (y21 + threshold) && yi >= (y21 - threshold)){
							e->v1->f1 = 1;
							perc = 0;
							break;
						}
					}
					if(xi <= (x22 + threshold) && xi >= (x22 - threshold)){
						if(yi <= (y22 + threshold) && yi >= (y22 - threshold)){
							e->v2->f1 = 1;
							perc = 0;
							break;
						}
					}
				}
				if ((m2 <= 1.0f) && (m2 >= -1.0f)) perc = (xi-x21)/(x22-x21);
				else perc=(yi-y21)/(y22-y21); /*lower slope more accurate*/
				//isect=32768.0*(perc+0.0000153); /* Percentage in 1/32768ths */
				
				break;
			}
		}	
		lastdist=dist;
	}
	return(perc);
} 

/* for multicut */
#define MAX_CUTS 256

/* for amount of edges */
#define MAX_CUT_EDGES 1024

static int knife_cut_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	ED_view3d_operator_properties_viewmat_set(C, op);

	return WM_gesture_lines_invoke(C, op, event);
}

static int knife_cut_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh(((Mesh *)obedit->data));
	EditEdge *eed;
	EditVert *eve;
	CutCurve curve[MAX_CUT_EDGES];
	struct GHash *gh;
	float isect=0.0;
	float  *scr, co[4];
	int len=0;
	short numcuts= RNA_int_get(op->ptr, "num_cuts"); 
	short mode= RNA_enum_get(op->ptr, "type");
	int winx, winy;
	float persmat[4][4];
//	int corner_cut_pattern= RNA_enum_get(op->ptr,"corner_cut_pattern");
	
	/* edit-object needed for matrix, and ar->regiondata for projections to work */
	if (obedit == NULL)
		return OPERATOR_CANCELLED;
	
	if (EM_nvertices_selected(em) < 2) {
		error("No edges are selected to operate on");
		BKE_mesh_end_editmesh(obedit->data, em);
		return OPERATOR_CANCELLED;
	}

	/* get the cut curve */
	RNA_BEGIN(op->ptr, itemptr, "path") {
		
		RNA_float_get_array(&itemptr, "loc", (float *)&curve[len]);
		len++;
		if(len>= MAX_CUT_EDGES) break;
	}
	RNA_END;
	
	if(len<2) {
		BKE_mesh_end_editmesh(obedit->data, em);
		return OPERATOR_CANCELLED;
	}

	ED_view3d_operator_properties_viewmat_get(op, &winx, &winy, persmat);

	/*store percentage of edge cut for KNIFE_EXACT here.*/
	for(eed=em->edges.first; eed; eed= eed->next) 
		eed->tmp.fp = 0.0; 
	
	/*the floating point coordinates of verts in screen space will be stored in a hash table according to the vertices pointer*/
	gh = BLI_ghash_new(BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp, "knife_cut_exec gh");
	for(eve=em->verts.first; eve; eve=eve->next){
		scr = MEM_mallocN(sizeof(float)*2, "Vertex Screen Coordinates");
		VECCOPY(co, eve->co);
		co[3]= 1.0;
		mul_m4_v4(obedit->obmat, co);
		apply_project_float(persmat, winx, winy, co, scr);
		BLI_ghash_insert(gh, eve, scr);
		eve->f1 = 0; /*store vertex intersection flag here*/
	
	}
	
	eed= em->edges.first;		
	while(eed) {	
		if( eed->v1->f & eed->v2->f & SELECT ){		// NOTE: uses vertex select, subdiv doesnt do edges yet
			isect= seg_intersect(eed, curve, len, mode, gh);
			if (isect!=0.0f) eed->f2= 1;
			else eed->f2=0;
			eed->tmp.fp= isect;
		}
		else {
			eed->f2=0;
			eed->f1=0;
		}
		eed= eed->next;
	}
	
	if (mode==KNIFE_MIDPOINT) esubdivideflag(obedit, em, SELECT, 0, 0, B_KNIFE, 1, SUBDIV_CORNER_INNERVERT, SUBDIV_SELECT_INNER);
	else if (mode==KNIFE_MULTICUT) esubdivideflag(obedit, em, SELECT, 0, 0, B_KNIFE, numcuts, SUBDIV_CORNER_INNERVERT, SUBDIV_SELECT_INNER);
	else esubdivideflag(obedit, em, SELECT, 0, 0, B_KNIFE|B_PERCENTSUBD, 1, SUBDIV_CORNER_INNERVERT, SUBDIV_SELECT_INNER);

	eed=em->edges.first;
	while(eed){
		eed->f2=0;
		eed->f1=0;
		eed=eed->next;
	}	
	
	BLI_ghash_free(gh, NULL, (GHashValFreeFP)MEM_freeN);
	
	BKE_mesh_end_editmesh(obedit->data, em);

	DAG_id_tag_update(obedit->data, 0);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	return OPERATOR_FINISHED;
}


void MESH_OT_knife_cut(wmOperatorType *ot)
{
	PropertyRNA *prop;
	
	ot->name= "Knife Cut";
	ot->description= "Cut selected edges and faces into parts";
	ot->idname= "MESH_OT_knife_cut";
	
	ot->invoke= knife_cut_invoke;
	ot->modal= WM_gesture_lines_modal;
	ot->exec= knife_cut_exec;
	ot->cancel= WM_gesture_lines_cancel;
	
	ot->poll= EM_view3d_poll;
	
	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
	
	RNA_def_enum(ot->srna, "type", knife_items, KNIFE_EXACT, "Type", "");
	prop= RNA_def_property(ot->srna, "path", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_runtime(prop, &RNA_OperatorMousePath);
	RNA_def_int(ot->srna, "num_cuts", 1, 1, MAX_CUTS, "Number of Cuts", "Only for Multi-Cut", 1, MAX_CUTS);
	// doesn't work atm.. RNA_def_enum(ot->srna, "corner_cut_pattern", corner_type_items, SUBDIV_CORNER_INNERVERT, "Corner Cut Pattern", "Topology pattern to use to fill a face after cutting across its corner");
	
	/* internal */
	prop = RNA_def_int(ot->srna, "cursor", BC_KNIFECURSOR, 0, INT_MAX, "Cursor", "", 0, INT_MAX);
	RNA_def_property_flag(prop, PROP_HIDDEN);

	ED_view3d_operator_properties_viewmat(ot);
}

/* ******************************************************* */

