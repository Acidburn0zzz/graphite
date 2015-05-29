/*  GRAPHITE2 LICENSING

    Copyright 2010, SIL International
    All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should also have received a copy of the GNU Lesser General Public
    License along with this library in the file named "LICENSE".
    If not, write to the Free Software Foundation, 51 Franklin Street, 
    Suite 500, Boston, MA 02110-1335, USA or visit their web page on the 
    internet at http://www.fsf.org/licenses/lgpl.html.

Alternatively, the contents of this file may be used under the terms of the
Mozilla Public License (http://mozilla.org/MPL) or the GNU General Public
License, as published by the Free Software Foundation, either version 2
of the License or (at your option) any later version.
*/
#include <algorithm>
#include <limits>
#include <math.h>
#include <string>
#include "inc/Collider.h"
#include "inc/Segment.h"
#include "inc/Slot.h"
#include "inc/GlyphCache.h"

#define ISQRT2 0.707106781f

// Possible rounding error for subbox boundaries: 0.016 = 1/64 = 1/256 * 4 
// (values in font range from 0..256)
#define SUBBOX_RND_ERR 0.016

using namespace graphite2;

////    SHIFT-COLLIDER    ////

// Initialize the Collider to hold the basic movement limits for the
// target slot, the one we are focusing on fixing.
void ShiftCollider::initSlot(Segment *seg, Slot *aSlot, const Rect &limit, float margin, float marginWeight,
    const Position &currShift, const Position &currOffset, int dir, GR_MAYBE_UNUSED json * const dbgout)
{
    int i;
    float max, min;
    float shift, oshift;
    const GlyphCache &gc = seg->getFace()->glyphs();
    unsigned short gid = aSlot->gid();
    const BBox &bb = gc.getBoundingBBox(gid);
    const SlantBox &sb = gc.getBoundingSlantBox(gid);
    //float sx = aSlot->origin().x + currShift.x;
    //float sy = aSlot->origin().y + currShift.y;
    if (currOffset.x != 0. || currOffset.y != 0.)
        _limit = Rect(limit.bl - currOffset, limit.tr - currOffset);
    else
        _limit = limit;
    // For a ShiftCollider, these indices indicate which vector we are moving by:
    // each _ranges represents absolute space with respect to the origin of the slot. Thus take into account true origins but subtract the vmin for the slot
    for (i = 0; i < 4; ++i)
    {
        switch (i) {
            case 0 :	// x direction
                min = _limit.bl.x + aSlot->origin().x;
                max = _limit.tr.x + aSlot->origin().x;
                _len[i] = bb.xa - bb.xi;
                shift = currOffset.x;
                oshift = currOffset.y;
                _ranges[i].initialise<XY>(min, max - min, margin, marginWeight, shift, oshift, oshift);
                break;
            case 1 :	// y direction
                min = _limit.bl.y + aSlot->origin().y;
                max = _limit.tr.y + aSlot->origin().y;
                _len[i] = bb.ya - bb.yi;
                shift = currOffset.y;
                oshift = currOffset.x;
                _ranges[i].initialise<XY>(min, max - min, margin, marginWeight, shift, oshift, oshift);
                break;
            case 2 :	// sum (negatively sloped diagonal boundaries)
                min = -2 * std::min(currShift.x - _limit.bl.x, currShift.y - _limit.bl.y) + aSlot->origin().x + aSlot->origin().y + currShift.x + currShift.y;
                max = 2 * std::min(_limit.tr.x - currShift.x, _limit.tr.y - currShift.y) + aSlot->origin().x + aSlot->origin().y + currShift.x + currShift.y;
                _len[i] = sb.sa - sb.si;
                shift = currOffset.x + currOffset.y;
                oshift = currOffset.x - currOffset.y;
                _ranges[i].initialise<SD>(min, max - min, margin / ISQRT2, marginWeight, shift, oshift, oshift);
                //min = 2.f * std::max(limit.bl.x, -limit.tr.y) + aSlot->origin().x + aSlot->origin().y + sb.si;
                //max = 2.f * std::min(limit.tr.x, -limit.bl.y) + aSlot->origin().x + aSlot->origin().y + sb.sa;
                break;
            case 3 :	// diff (positively sloped diagonal boundaries)
                min = -2 * std::min(currShift.x - _limit.bl.x, _limit.tr.y - currShift.y) + aSlot->origin().x - aSlot->origin().y + currShift.x - currShift.y;
                max = 2 * std::min(_limit.tr.x - currShift.x, currShift.y - _limit.bl.y) + aSlot->origin().x - aSlot->origin().y + currShift.x - currShift.y;
                _len[i] = sb.da - sb.di;
                shift = currOffset.x - currOffset.y;
                oshift = currOffset.x + currOffset.y;
                _ranges[i].initialise<SD>(min, max - min, margin / ISQRT2, marginWeight, shift, oshift, oshift);
                // min = 2.f * std::max(limit.bl.x, limit.bl.y) + aSlot->origin().x - aSlot->origin().y + sb.di;
                // max = 2.f * std::min(limit.tr.x, limit.tr.y) + aSlot->origin().x - aSlot->origin().y + sb.da;
                break;
        }

#if !defined GRAPHITE2_NTRACING
        // Debugging:
        _rawRanges[i].clear();
        _rawRanges[i].add(min, max - _len[i]);
        _rawRanges[i].len(_len[i]);
        _removals[i].clear();
        _slotNear[i].clear();
        _subNear[i].clear();
#endif
    }
#if !defined GRAPHITE2_NTRACING
    _seg = seg; // debugging
#endif
    _target = aSlot;
    if ((dir & 1) == 0)
    {
        // For LTR, switch and negate x limits.
        _limit.bl.x = -1 * limit.tr.x;
        _limit.tr.x = -1 * limit.bl.x;
    }
    _currOffset = currOffset;
    _currShift = currShift;

	_margin = margin;
	_marginWt = marginWeight;
    
    SlotCollision *c = seg->collisionInfo(aSlot);
    _seqClass = c->seqClass();
    _seqOrder = c->seqOrder();
	// For now we just get these out of the SlotCollision object.
	//_seqAboveXoff = c->seqAboveXoff();
	//_seqAboveWt = c->seqAboveWt();
	//_seqBelowXlim = c->seqBelowXlim();
	//_seqBelowWt = c->seqBelowWt();
	//_seqValignHt = c->seqValignHt();
	//_seqValignWt = c->seqValignWt();

	//_scraping[0] = _scraping[1] = _scraping[2] = _scraping[3] = false;
    
}   // end of ShiftCollider::initSlot


// Mark an area with a cost that can vary along the x-axis.
inline void ShiftCollider::addBox_slopex(const Rect &box, const Rect &org, float weight, float m, bool minright, int axis)
{
    float a;
    switch (axis) {
        case 0 :
            if (box.bl.y < org.tr.y && box.tr.y > org.bl.y && box.width() > 0)
            {
                a = org.bl.y - box.bl.y;
                _ranges[axis].weighted<XY>(box.bl.x, box.width(), weight, _currShift.x, _currShift.y, a, m, minright ? box.tr.x : box.bl.x, 0);
            }
            break;
        case 1 :
            if (box.bl.x < org.tr.x && box.tr.x > org.bl.x && box.height() > 0)
            {
                a = org.bl.x - box.bl.x;
                _ranges[axis].weighted<XY>(box.bl.y, box.height(), weight, _currShift.y, _currShift.x, a, 0, 0, m * a * a);
            }
            break;
        case 2 :
            if (box.bl.x - box.tr.y < org.tr.x - org.bl.y && box.tr.x - box.bl.y > org.bl.x - org.tr.y)
            {
                float smin = std::max(std::max(box.bl.x + box.bl.y, 2 * (box.bl.y - org.tr.y) + org.tr.x + org.tr.y),
                                      2 * (box.bl.x - org.tr.x) + org.tr.x + org.tr.y);
                float smax = std::min(std::min(box.tr.x + box.tr.y, 2 * (box.tr.y - org.bl.y) + org.bl.x + org.bl.y),
                                      2 * (box.tr.x - org.bl.x) + org.bl.x + org.bl.y);
                if (smin > smax) return;
                a = minright ? (2 * org.tr.x - smax) : (2 * org.bl.x - smin);
                _ranges[axis].weighted<SD>(smin, smax - smin, weight / 2, _currShift.x + _currShift.y, _currShift.x - _currShift.y, a, m / 2, minright ? smax : smin, 0);
            }
            break;
        case 3 :
            if (box.bl.x + box.bl.y < org.tr.x + org.tr.y && box.tr.x + box.tr.y > org.bl.x + org.bl.y)
            {
                float dmin = std::max(std::max(box.bl.x - box.tr.y, 2 * (box.bl.x - org.tr.x) + org.tr.x - org.bl.y),
                                      org.tr.x - org.bl.y - 2 * (box.tr.y - org.bl.y));
                float dmax = std::min(std::min(box.tr.x - box.bl.y, 2 * (box.tr.x - org.bl.x) + org.bl.x - org.tr.y),
                                      org.bl.x - org.tr.y - 2 * (box.bl.y - org.tr.y));
                if (dmin > dmax) return;
                a = minright ? (2 * org.tr.x - dmax) : (2 * org.bl.x - dmin);
                _ranges[axis].weighted<SD>(dmin, dmax - dmin, weight / 2, _currShift.x - _currShift.y, _currShift.x + _currShift.y, a, m / 2, minright ? dmax : dmin, 0);
            }
            break;
        default :
            break;
    }
    return;
}

// Mark an area with a cost that can vary along the y-axis.
inline void ShiftCollider::addBox_slopey(const Rect &box, const Rect &org, float weight, float m, bool mintop, int axis)
{
    float a;
    switch (axis) {
        case 0 :
            if (box.bl.y < org.tr.y && box.tr.y > org.bl.y && box.width() > 0)
            {
                a = org.bl.y - box.bl.y;
                _ranges[axis].weighted<XY>(box.bl.x, box.width(), weight, _currShift.x, _currShift.y, a, 0, 0, m * a * a);
            }
            break;
        case 1 :
            if (box.bl.x < org.tr.x && box.tr.x > org.bl.x && box.height() > 0)
            {
                a = org.bl.x - box.bl.x;
                _ranges[axis].weighted<XY>(box.bl.y, box.height(), weight, _currShift.y, _currShift.x, a, m, mintop ? box.tr.y : box.bl.y, 0);
            }
            break;
        case 2 :
            if (box.bl.x - box.tr.y < org.tr.x - org.bl.y && box.tr.x - box.bl.y > org.bl.x - org.tr.y)
            {
                float smin = std::max(std::max(box.bl.x + box.bl.y, 2 * (box.bl.y - org.tr.y) + org.tr.x + org.tr.y),
                                      2 * (box.bl.x - org.tr.x) + org.tr.x + org.tr.y);
                float smax = std::min(std::min(box.tr.x + box.tr.y, 2 * (box.tr.y - org.bl.y) + org.bl.x + org.bl.y),
                                      2 * (box.tr.x - org.bl.x) + org.bl.x + org.bl.y);
                if (smin > smax) return;
                a = mintop ? smax - 2 * box.tr.y : smin - 2 * box.bl.y;
                _ranges[axis].weighted<SD>(smin, smax - smin, weight / 2, _currShift.x + _currShift.y, _currShift.x - _currShift.y, a, m / 2, mintop ? smax : smin, 0);
            }
            break;
        case 3 :
            if (box.bl.x + box.bl.y < org.tr.x + org.tr.y && box.tr.x + box.tr.y > org.bl.x + org.bl.y)
            {
                float dmin = std::max(std::max(box.bl.x - box.tr.y, 2 * (box.bl.x - org.tr.x) + org.tr.x - org.bl.y),
                                      org.tr.x - org.bl.y - 2 * (box.tr.y - org.bl.y));
                float dmax = std::min(std::min(box.tr.x - box.bl.y, 2 * (box.tr.x - org.bl.x) + org.bl.x - org.tr.y),
                                      org.bl.x - org.tr.y - 2 * (box.bl.y - org.tr.y));
                if (dmin > dmax) return;
                a = mintop ? dmin + 2 * box.tr.y : dmax + 2 * box.bl.y;     // swap max min for d
                _ranges[axis].weighted<SD>(dmin, dmax - dmin, weight / 2, _currShift.x - _currShift.y, _currShift.x + _currShift.y, a, m / 2, mintop ? dmin : dmax, 0);
            }
            break;
        default :
            break;
    }
    return;
}

// Mark an area with an absolute cost, making it completely inaccessible.
inline void ShiftCollider::removeBox(const Rect &box, const Rect &org, int axis)
{
    switch (axis) {
        case 0 :
            if (box.bl.y < org.tr.y && box.tr.y > org.bl.y && box.width() > 0)
                _ranges[axis].exclude(box.bl.x , box.width() + _len[axis]);
            break;
        case 1 :
            if (box.bl.x < org.tr.x && box.tr.x > org.bl.x && box.height() > 0)
                _ranges[axis].exclude(box.bl.y , box.height() + _len[axis]);
            break;
        case 2 :
            if (box.bl.x - box.tr.y < org.tr.x - org.bl.y && box.tr.x - box.bl.y > org.bl.x - org.tr.y && box.width() > 0 && box.height() > 0)
                _ranges[axis].exclude(box.bl.x + box.bl.y , box.height() + box.width() + _len[axis]);
            break;
        case 3 :
            if (box.bl.x + box.bl.y < org.tr.x + org.tr.y && box.tr.x + box.tr.y > org.bl.x + org.bl.y && box.width() > 0 && box.height() > 0)
                _ranges[axis].exclude(box.bl.x - box.bl.y , box.height() + box.width() + _len[axis]);
            break;
        default :
            break;
    }
    return;
}

// Adjust the movement limits for the target to avoid having it collide
// with the given neighbor slot. Also determine if there is in fact a collision
// between the target and the given slot.
bool ShiftCollider::mergeSlot(Segment *seg, Slot *slot, const Position &currShift,
		bool isAfter,  // slot is logically after _target
		bool sameCluster,
        GR_MAYBE_UNUSED json * const dbgout )
{
    bool isCol = false;
    const float tx = _target->origin().x + _currShift.x;
    const float ty = _target->origin().y + _currShift.y;
    const float td = tx - ty;
    const float ts = tx + ty;
    const float sx = slot->origin().x + currShift.x;
    const float sy = slot->origin().y + currShift.y;
    const float sd = sx - sy;
    const float ss = sx + sy;
    float vmin, vmax;
    float omin, omax, otmin, otmax;
    float cmin, cmax;   // target limits
    float cdiff;        // difference between centres
    float vcmin, vcmax, tempv;
    const GlyphCache &gc = seg->getFace()->glyphs();
    const unsigned short gid = slot->gid();
    const unsigned short tgid = _target->gid();
    const BBox &bb = gc.getBoundingBBox(gid);
    const SlantBox &sb = gc.getBoundingSlantBox(gid);
    const BBox &tbb = gc.getBoundingBBox(tgid);
    const SlantBox &tsb = gc.getBoundingSlantBox(tgid);

    SlotCollision * cslot = seg->collisionInfo(slot);
    int orderFlags = 0;
    float orderMargin = 0.;     // max of slot and _target's overlapMargins
    if (sameCluster && _seqClass && _seqClass == cslot->seqClass())
		// Force the target glyph to be in the specified direction from the slot we're testing.
        orderFlags = _seqOrder;
    float seq_above_wt = cslot->seqAboveWt();
    float seq_below_wt = cslot->seqBelowWt();
    float seq_valign_wt = cslot->seqValignWt();
	// TODO: add these
	//float seq_above_xoff = cslot->seqAboveXoff();
	//float seq_below_xlim = cslot->seqBelowXlim();
	//float seq_valign_ht = cslot->seqValignHt();

    // if isAfter, invert orderFlags
#define COLL_ORDER_X (SlotCollision::COLL_ORDER_LEFT | SlotCollision::COLL_ORDER_RIGHT)
#define COLL_ORDER_Y (SlotCollision::COLL_ORDER_DOWN | SlotCollision::COLL_ORDER_UP)
    if (isAfter)        // _target isAfter slot
    {
        if (orderFlags & COLL_ORDER_X)
            orderFlags = orderFlags ^ COLL_ORDER_X;
        if (orderFlags & COLL_ORDER_Y)
            orderFlags = orderFlags ^ COLL_ORDER_Y;
    }

    // Process main bounding octabox.
    for (int i = 0; i < 4; ++i)
    {
		//uint16 mMin = (uint16)(_marginMin / (i > 1 ? ISQRT2 : 1.));
        int enforceOrder = 0;
        switch (i) {
            case 0 :	// x direction
                enforceOrder = ((orderFlags & SlotCollision::COLL_ORDER_LEFT) ? -1 : 0) // -1 = force left, 1 = force right
                        + ((orderFlags & SlotCollision::COLL_ORDER_RIGHT) ? 1 : 0);
                vmin = std::max(std::max(bb.xi + sx, sb.di + sd + tbb.xa + tx - tsb.da - td), sb.si + ss + tbb.xa + tx - tsb.sa - ts) - tbb.xi;
                vmax = std::min(std::min(bb.xa + sx, sb.da + sd + tbb.xi + tx - tsb.di - td), sb.sa + ss + tbb.xi + tx - tsb.si - ts) - tbb.xi;
                otmin = tbb.yi + ty;
                otmax = tbb.ya + ty;
                omin = bb.yi + sy;
                omax = bb.ya + sy;
                cmin = _limit.bl.x + _target->origin().x;
                cmax = _limit.tr.x + _target->origin().x + tbb.xa - tbb.xi;
                cdiff = tx + 0.5 * (tbb.xi + tbb.xa) - sx - 0.5 * (bb.xi + bb.xa);
                tempv = sx + 0.5 * (bb.xi + bb.xa);
                vcmin = (float)-1e38;
                vcmax = (float)1e38;
                if (orderFlags & SlotCollision::COLL_ORDER_LEFT)
                    vcmax = tempv + 0.5 * (tbb.xa - tbb.xi) + orderMargin;
                else if (orderFlags & SlotCollision::COLL_ORDER_RIGHT)
                    vcmin = tempv - 0.5 * (tbb.xa - tbb.xi) - orderMargin;
                break;
            case 1 :	// y direction
                enforceOrder = ((orderFlags & SlotCollision::COLL_ORDER_DOWN) ? -1 : 0) // -1 = force down, 1 = force up
                        + ((orderFlags & SlotCollision::COLL_ORDER_UP) ? 1 : 0);
                vmin = std::max(std::max(bb.yi + sy, tbb.ya + ty - sb.da - sd + tsb.di + td), sb.si + ss + tbb.ya + ty - tsb.sa - ts) - tbb.yi;
                vmax = std::min(std::min(bb.ya + sy, tbb.yi + ty - sb.di - sd + tsb.da + td), sb.sa + ss + tbb.yi + ty - tsb.si - ts) - tbb.yi;
                otmin = tbb.xi + tx;
                otmax = tbb.xa + tx;
                omin = bb.xi + sx;
                omax = bb.xa + sx;
                cmin = _limit.bl.y + _target->origin().y;
                cmax = _limit.tr.y + _target->origin().y + tbb.ya - tbb.yi;
                cdiff = ty + 0.5 * (tbb.yi + tbb.ya) - sy - 0.5 * (bb.yi + bb.ya);
                tempv = sy + 0.5 * (bb.yi + bb.ya);
                vcmin = (float)-1e38;
                vcmax = (float)1e38;
                if (orderFlags & SlotCollision::COLL_ORDER_UP)
                    vcmin = tempv - 0.5 * (tbb.ya - tbb.yi) - orderMargin;
                else if (orderFlags & SlotCollision::COLL_ORDER_DOWN)
                    vcmax = tempv + 0.5 * (tbb.ya - tbb.yi) + orderMargin;
                break;
            case 2 :    // sum - moving along the positively-sloped vector, so the boundaries are the
                        // negatively-sloped boundaries.
                enforceOrder = orderFlags;
                vmin = std::max(std::max(sb.si + ss, 2 * (bb.yi + sy - tbb.ya - ty) + tsb.sa + ts), 2 * (bb.xi + sx - tbb.xa - tx) + tsb.sa + ts) - tsb.si;
                vmax = std::min(std::min(sb.sa + ss, 2 * (bb.ya + sy - tbb.yi - ty) + tsb.si + ts), 2 * (bb.xa + sx - tbb.xi - tx) + tsb.si + ts) - tsb.si;
                otmin = tsb.di + td;
                otmax = tsb.da + td;
                omin = sb.di + sd;
                omax = sb.da + sd;
                cmin = _limit.bl.x + _limit.bl.y + _target->origin().x + _target->origin().y; 
                cmax = _limit.tr.x + _limit.tr.y + _target->origin().x + _target->origin().y + tsb.sa - tsb.si;
                cdiff = ts + 0.5 * (tsb.si + tsb.sa) - ss - 0.5 * (sb.si + sb.sa);
                vcmin = (float)-1e38;
                vcmax = (float)1e38;
                if (orderFlags & SlotCollision::COLL_ORDER_LEFT)
                    vcmax = std::min(ts - (tx - sx) / ISQRT2 - (tbb.xi + tbb.xa - bb.xi - bb.xa) * ISQRT2 - orderMargin,
                                     (orderFlags & SlotCollision::COLL_ORDER_XOVERY) ? ts - (tx - sx + tbb.xi - bb.xa) / ISQRT2 : (float)1e38);
                else if (orderFlags & SlotCollision::COLL_ORDER_RIGHT)
                    vcmin = std::max(ts - (tx - sx) / ISQRT2 - (tbb.xi + tbb.xa - bb.xi - bb.xa) * ISQRT2 - orderMargin,
                                     (orderFlags & SlotCollision::COLL_ORDER_XOVERY) ? ts - (tx - sx + tbb.xa - bb.xi) / ISQRT2 : (float)-1e38);
                if (orderFlags & SlotCollision::COLL_ORDER_DOWN)
                    vcmax = std::min(vcmax, ts - (ty - sy) / ISQRT2 + (tbb.yi + tbb.ya - bb.yi - bb.ya) * ISQRT2) + orderMargin;
                else if (orderFlags & SlotCollision::COLL_ORDER_UP)
                    vcmin = std::max(vcmin, ts - (ty - sy) / ISQRT2 + (tbb.yi + tbb.ya - bb.yi - bb.ya) * ISQRT2) + orderMargin;
                break;
            case 3 :    // diff - moving along the negatively-sloped vector, so the boundaries are the
                        // positively-sloped boundaries.
                enforceOrder = orderFlags;
                vmin = std::max(std::max(sb.di + sd, 2 * (bb.xi + sx - tbb.xa - tx) + tsb.da + td), tsb.da + td - 2 * (bb.ya + sy - tbb.yi - ty)) - tsb.di;
                vmax = std::min(std::min(sb.da + sd, 2 * (bb.xa + sx - tbb.xi - tx) + tsb.di + td), tsb.di + td - 2 * (bb.yi + sy - tbb.ya - ty)) - tsb.di;
                otmin = tsb.si + ts;
                otmax = tsb.sa + ts;
                omin = sb.si + ss;
                omax = sb.sa + ss;
                cmin = _limit.bl.x - _limit.tr.y + _target->origin().x - _target->origin().y;
                cmax = _limit.tr.x - _limit.bl.y + _target->origin().x - _target->origin().y + tsb.da - tsb.di;
                cdiff = td + 0.5 * (tsb.di + tsb.da) - sd - 0.5 * (sb.di + sb.da);
                vcmin = (float)-1e38;
                vcmax = (float)1e38;
                if (orderFlags & SlotCollision::COLL_ORDER_LEFT)
                    vcmax = std::min(td - (tx - sx) / ISQRT2 - (tbb.xi + tbb.xa - bb.xi - bb.xa) * ISQRT2 - orderMargin,
                                (orderFlags & SlotCollision::COLL_ORDER_XOVERY) ? ts - (tx - sx + tbb.xi - bb.xa) / ISQRT2 : (float)1e38);
                else if (orderFlags & SlotCollision::COLL_ORDER_RIGHT)
                    vcmin = std::max(td - (tx - sx) / ISQRT2 - (tbb.xi + tbb.xa - bb.xi - bb.xa) * ISQRT2 - orderMargin,
                                (orderFlags & SlotCollision::COLL_ORDER_XOVERY) ? ts - (tx - sx + tbb.xa - bb.xi) / ISQRT2 : (float)-1e38);
                if (orderFlags & SlotCollision::COLL_ORDER_DOWN)
                    vcmin = std::max(vcmin, td + (ty - sy) / ISQRT2 + (tbb.yi + tbb.ya - bb.yi - bb.ya) * ISQRT2) + orderMargin;
                else if (orderFlags & SlotCollision::COLL_ORDER_UP)
                    vcmax = std::min(vcmax, td + (ty - sy) / ISQRT2 + (tbb.yi + tbb.ya - bb.yi - bb.ya) * ISQRT2) + orderMargin;
                break;
            default :
                continue;
        }
        
		SeqRegions seqReg;
        if (enforceOrder > 0) // enforce neighboring glyph being left /down (diagram 1)
        {
            float xminf = _limit.bl.x + _target->origin().x;
            float xpinf = _limit.tr.x + _target->origin().x;
            float ypinf = _limit.tr.y + _target->origin().y;
            float yminf = _limit.bl.y + _target->origin().y;
            float r1Xedge = sx + bb.xa + cslot->seqAboveXoff();
            float r3Xedge = sx + bb.xa + cslot->seqBelowXlim();
            float r2Yedge = sy + bb.yi + 0.5 * cslot->seqValignHt();
            Rect org(Position(tx + tbb.xi, ty + tbb.yi), Position(tx + tbb.xa, ty + tbb.ya));
            // region 1
            addBox_slopex(Rect(Position(xminf, r2Yedge), Position(r1Xedge, ypinf)), org, 0, seq_above_wt, true, i);
            // region 2
            removeBox(Rect(Position(xminf, yminf), Position(r3Xedge, r2Yedge)), org, i);
            // region 3
            addBox_slopex(Rect(Position(r3Xedge, yminf), Position(xpinf, r2Yedge)), org, seq_below_wt, 0, true, i);
            // region 4
            addBox_slopey(Rect(Position(sx + bb.xi, sy + bb.yi), Position(xpinf, r2Yedge)), org, 0, seq_valign_wt, true, i);
            // region 5
            addBox_slopey(Rect(Position(sx + bb.xi, sy + bb.yi), Position(xpinf, sy + bb.yi - 0.5 * cslot->seqValignHt())),
                            org, 0, seq_valign_wt, false, i);
#if !defined GRAPHITE2_NTRACING
			seqReg.r1Xedge = r1Xedge;
			seqReg.r2Yedge = r2Yedge;
			seqReg.r3Xedge = r3Xedge;
			seqReg.r45Mid  = sy + bb.yi;
#endif
        }
        else if (enforceOrder < 0)  // enforce neighboring glyph being right/up (diagram 2)
        {
            float xminf = _limit.bl.x + _target->origin().x;
            float xpinf = _limit.tr.x + _target->origin().x;
            float ypinf = _limit.tr.y + _target->origin().y;
            float yminf = _limit.bl.y + _target->origin().y;
            float r1Xedge = sx + bb.xi - cslot->seqAboveXoff();
            float r3Xedge = sx + bb.xi - cslot->seqBelowXlim();
            float r2Yedge = sy + bb.yi + 0.5 * cslot->seqValignHt();
            Rect org(Position(tx + tbb.xi, ty + tbb.yi), Position(tx + tbb.xa, ty + tbb.ya));
            // region 1
            addBox_slopex(Rect(Position(r1Xedge, yminf), Position(xminf, r2Yedge)), org, 0, seq_above_wt, false, i);
            // region 2
            removeBox(Rect(Position(r3Xedge, r2Yedge), Position(xpinf, ypinf)), org, i);
            // region 3
            addBox_slopex(Rect(Position(xminf, r2Yedge), Position(r3Xedge, ypinf)), org, seq_below_wt, 0, true, i);
            // region 4
            addBox_slopey(Rect(Position(xminf, r2Yedge), Position(sx + bb.xa, sy + bb.yi)),
                            org, 0, seq_valign_wt, true, i);
            // region 5
            addBox_slopey(Rect(Position(xminf, sy + bb.yi - 0.5 * cslot->seqValignHt()),
                            Position(sx + bb.xa, sy + bb.yi)), org, 0, seq_valign_wt, false, i);
#if !defined GRAPHITE2_NTRACING
			seqReg.r1Xedge = r1Xedge;
			seqReg.r2Yedge = r2Yedge;
			seqReg.r3Xedge = r3Xedge;
			seqReg.r45Mid  = sy + bb.yi;
		}
		else
		{
			seqReg.r1Xedge = seqReg.r2Yedge = seqReg.r3Xedge = seqReg.r45Mid = 0.0;
#endif
        }

        // if ((vmin < cmin - m && vmax < cmin - m) || (vmin > cmax + m && vmax > cmax + m)
        //    // or it is offset in the opposite dimension:
        //    || (omin < otmin - m && omax < otmin - m) || (omin > otmax + m && omax > otmax + m))
        if (vmax < cmin - _margin || vmin > cmax + _margin || omax < otmin - _margin || omin > otmax + _margin)
            continue;
#if 0
		if (seg->collisionInfo(_target)->canScrape(i) && (omax < otmin + _margin || omin > otmax - _margin))
		{
			_scraping[i] = true;
			continue;
		}
#endif

        // Process sub-boxes that are defined for this glyph.
        // We only need to do this if there was in fact a collision with the main octabox.
        uint8 numsub = gc.numSubBounds(gid);
        if (numsub > 0)
        {
            bool anyhits = false;
            for (int j = 0; j < numsub; ++j)
            {
                const BBox &sbb = gc.getSubBoundingBBox(gid, j);
                const SlantBox &ssb = gc.getSubBoundingSlantBox(gid, j);
                switch (i) {
                    case 0 :    // x
                        vmin = std::max(std::max(sbb.xi + sx, ssb.di + sd + tbb.xa + tx - tsb.da - td), ssb.si + ss + tbb.xa + tx - tsb.sa - ts) - tbb.xi;
                        vmax = std::min(std::min(sbb.xa + sx, ssb.da + sd + tbb.xi + tx - tsb.di - td), ssb.sa + ss + tbb.xi + tx - tsb.si - ts) - tbb.xi;
                        omin = sbb.yi + sy;
                        omax = sbb.ya + sy;
                        break;
                    case 1 :    // y
                        vmin = std::max(std::max(sbb.yi + sy, tbb.ya + ty - ssb.da - sd + tsb.di + td), ssb.si + ss + tbb.ya + ty - tsb.sa - ts) - tbb.yi;
                        vmax = std::min(std::min(sbb.ya + sy, tbb.yi + ty - ssb.di - sd + tsb.da + td), ssb.sa + ss + tbb.yi + ty - tsb.si - ts) - tbb.yi;
                        omin = sbb.xi + sx;
                        omax = sbb.xa + sx;
                        break;
                    case 2 :    // sum
                        vmin = std::max(std::max(ssb.si + ss, 2 * (sbb.yi + sy - tbb.ya - ty) + tsb.sa + ts), 2 * (sbb.xi + sx - tbb.xa - tx) + tsb.sa + ts) - tsb.si;
                        vmax = std::min(std::min(ssb.sa + ss, 2 * (sbb.ya + sy - tbb.yi - ty) + tsb.si + ts), 2 * (sbb.xa + sx - tbb.xi - tx) + tsb.si + ts) - tsb.si;
                        omin = ssb.di + sd;
                        omax = ssb.da + sd;
                        break;
                    case 3 :    // diff
                        vmin = std::max(std::max(ssb.di + sd, 2 * (sbb.xi + sx - tbb.xa - tx) + tsb.da + td), tsb.da + td - 2 * (sbb.ya + sy - tbb.yi - ty)) - tsb.di;
                        vmax = std::min(std::min(ssb.da + sd, 2 * (sbb.xa + sx - tbb.xi - tx) + tsb.di + td), tsb.di + td - 2 * (sbb.yi + sy - tbb.ya - ty)) - tsb.di;
                        omin = ssb.si + ss;
                        omax = ssb.sa + ss;
                        break;
                }
//                if (vmin > vmax)
//                {
//                    float t = vmin;
//                    vmin = vmax;
//                    vmax = t;
//                }
                
                // if ((vmin < cmin - m && vmax < cmin - m) || (vmin > cmax + m && vmax > cmax + m)
                //     		|| (omin < otmin - m && omax < otmin - m) || (omin > otmax + m && omax > otmax + m))
                if (vmax < cmin - _margin || vmin > cmax + _margin || omax < otmin - _margin || omin > otmax + _margin)
                    continue;
#if 0
				if (seg->collisionInfo(_target)->canScrape(i) && (omax < otmin + _margin || omin > otmax - _margin))
				{
					_scraping[i] = true;
					continue;
				}
#endif
                _ranges[i].exclude_with_margins(vmin - _len[i], vmax - vmin + _len[i]);
                anyhits = true;
                
#if !defined GRAPHITE2_NTRACING
				SeqRegions seqRegJ;  // bogus
				seqRegJ.r1Xedge = seqRegJ.r2Yedge = seqRegJ.r3Xedge = seqRegJ.r45Mid = 0.0;
                IntervalSet::tpair dbg(vmin, vmax); // debugging
                _removals[i].append(dbg);           // debugging
				_seqRegions[i].push_back(seqRegJ);  // debugging
                _slotNear[i].push_back(slot);       // debugging
                _subNear[i].push_back(j);           // debugging
#endif
            }
            if (anyhits)
                isCol = true;
        }
        else // no sub-boxes
        {
            isCol = true;
            _ranges[i].exclude_with_margins(vmin - _len[i], vmax - vmin + _len[i]);

#if !defined GRAPHITE2_NTRACING
            IntervalSet::tpair dbg(vmin, vmax); // debugging
            _removals[i].append(dbg);           // debugging
			_seqRegions[i].push_back(seqReg);   // debugging
            _slotNear[i].push_back(slot);       // debugging
            _subNear[i].push_back(-1);          // debugging
#endif
        }
    }
    
    if (cslot && cslot->exclGlyph() > 0)
    {
        // Set up the bogus slot representing the exclusion glyph.
        exclSlot->setGlyph(seg, cslot->exclGlyph());
        Position exclOrigin(slot->origin() + cslot->exclOffset());
        exclSlot->origin(exclOrigin);
        isCol |= mergeSlot(seg, exclSlot, currShift, isAfter, sameCluster, dbgout );
    }
        
    return isCol;
    
}   // end of ShiftCollider::mergeSlot


// Figure out where to move the target glyph to, and return the amount to shift by.
Position ShiftCollider::resolve(Segment *seg, bool &isCol, GR_MAYBE_UNUSED json * const dbgout)
{
    const GlyphCache &gc = seg->getFace()->glyphs();
    int gid = _target->gid();
    const BBox &bb = gc.getBoundingBBox(gid);
    const SlantBox &sb = gc.getBoundingSlantBox(gid);
    float tlen, tleft, tbase, tval;
    float totalCost = (float)(std::numeric_limits<float>::max() / 2.);
    Position resultPos = Position(0, 0);
	int bestAxis = -1;
    // float cmax, cmin;
    IntervalSet aFit;
    // int flags = seg->collisionInfo(_target)->flags();
    Position currOffset = seg->collisionInfo(_target)->offset();
#if !defined GRAPHITE2_NTRACING
    if (dbgout)
    {
		outputJsonDbgStartSlot(dbgout, seg);
        *dbgout << "vectors" << json::array;
    }
#endif
    isCol = true;
    for (int i = 0; i < 4; ++i)
    {
        float bestCost = -1;
        float bestPos;
        // Calculate the margin depending on whether we are moving diagonally or not:
        switch (i) {
            case 0 :	// x direction
                tlen = bb.xa - bb.xi;
                tleft = _target->origin().x + _currShift.x + bb.xi;
                tbase = _target->origin().x;    // The best place to be for the glyph, its anchor
                tval = -currOffset.x;
                break;
            case 1 :	// y direction
                tlen = bb.ya - bb.yi;
                tleft = _target->origin().y + _currShift.y + bb.yi;
                tbase = _target->origin().y;
                tval = -currOffset.y;
                break;
            case 2 :	// sum (negatively-sloped diagonals)
                tlen = sb.sa - sb.si;
                tleft = _target->origin().x + _target->origin().y + _currShift.x + _currShift.y + sb.si;
                tbase = _target->origin().x + _target->origin().y;
                tval = -currOffset.x - currOffset.y;
                break;
            case 3 :	// diff (positively-sloped diagonals)
                tlen = sb.da - sb.di;
                tleft = _target->origin().x - _target->origin().y + _currShift.x - _currShift.y + sb.di;
                tbase = _target->origin().x - _target->origin().y;
                tval = currOffset.y - currOffset.x;
                break;
        }
        Position testp;
        bestPos = _ranges[i].closest(tbase + tval, tlen, bestCost) - tbase;     // returns absolute, convert to shift.
#if !defined GRAPHITE2_NTRACING
        if (dbgout)
        {
            outputJsonDbgOneVector(dbgout, seg, i, tbase, tlen, bestCost, bestPos) ;
        }
#endif
        if (bestCost >= 0.0)
        {
            isCol = false;
            switch (i) {
                case 0 : testp = Position(bestPos, _currShift.y); break;
                case 1 : testp = Position(_currShift.x, bestPos); break;
                case 2 : testp = Position(0.5 * (bestPos + _currShift.x - _currShift.y), 0.5 * (bestPos - _currShift.x + _currShift.y)); break;
                case 3 : testp = Position(0.5 * (bestPos + _currShift.x + _currShift.y), 0.5 * (_currShift.x + _currShift.y - bestPos)); break;
            }
            if (bestCost < totalCost)
            {
                totalCost = bestCost;
                resultPos = testp;
                bestAxis = i;
            }
        }
    }  // end of loop over 4 directions

#if !defined GRAPHITE2_NTRACING
    if (dbgout)
    {
        outputJsonDbgEndSlot(dbgout, seg, resultPos, bestAxis, isCol);
    }
#endif

    return resultPos;

}   // end of ShiftCollider::resolve


#if !defined GRAPHITE2_NTRACING

void ShiftCollider::outputJsonDbg(json * const dbgout, Segment *seg, int axis)
{
//    if (!dbgout) return;

    int axisMax = axis;
    if (axis < 0) // output all axes
    {
        *dbgout << "gid" << _target->gid()
            << "limit" << _limit
            << "target" << json::object
                << "origin" << _target->origin()
                << "margin" << _margin
                << "bbox" << seg->theGlyphBBoxTemporary(_target->gid())
                << "slantbox" << seg->getFace()->glyphs().slant(_target->gid())
                << json::close; // target object
        *dbgout << "ranges" << json::array;
        axis = 0;
        axisMax = 3;
    }
    for (int iAxis = axis; iAxis <= axisMax; ++iAxis)
    {
        *dbgout << json::flat << json::array;
        for (Zones::const_eiter_t s = _ranges[iAxis].begin(), e = _ranges[iAxis].end(); s != e; ++s)
            *dbgout << Position(s->x, s->xm) << s->c << s->sm << s->smx;
        *dbgout << json::close;
    }
    if (axis < axisMax) // looped through the _ranges array for all axes
        *dbgout << json::close; // ranges array
}

void ShiftCollider::outputJsonDbgStartSlot(GR_MAYBE_UNUSED json * const dbgout, Segment *seg)
{
        *dbgout << json::object // slot - not closed till the end of the caller method
                << "slot" << objectid(dslot(seg, _target))
				<< "gid" << _target->gid()
                << "limit" << _limit
                << "target" << json::object
                    << "origin" << _target->origin()
                    << "currShift" << _currShift
                    << "bbox" << seg->theGlyphBBoxTemporary(_target->gid())
                    << "slantBox" << seg->getFace()->glyphs().slant(_target->gid())
                    << "fix" << "shift";
        *dbgout     << json::close; // target object
}

void ShiftCollider::outputJsonDbgEndSlot(GR_MAYBE_UNUSED json * const dbgout, Segment *seg,
	 Position resultPos, int bestAxis, bool isCol)
{
    *dbgout << json::close // vectors array
    << "result" << resultPos
	//<< "scraping" << _scraping[bestAxis]
	<< "bestAxis" << bestAxis
    << "stillBad" << isCol
    << json::close; // slot object
}

void ShiftCollider::outputJsonDbgOneVector(GR_MAYBE_UNUSED json * const dbgout, Segment *seg, int axis,
	float tleft, float tlen, float bestCost, float bestVal) 
{
	const char * label;
	switch (axis)
	{
		case 0:	label = "x";			break;
		case 1:	label = "y";			break;
		case 2:	label = "sum (NE-SW)";	break;
		case 3:	label = "diff (NW-SE)";	break;
		default: label = "???";			break;
	}

	*dbgout << json::object // vector
		<< "direction" << label
		<< "targetMin" << tleft
		<< "targetSize" << tlen;
            
	outputJsonDbgRawRanges(dbgout, axis);
	outputJsonDbgRemovals(dbgout, axis);
    	
    *dbgout << "ranges";
    outputJsonDbg(dbgout, seg, axis);

    //*dbgout << "fits" << json::flat << json::array;
    //for (IntervalSet::ivtpair s = aFit.begin(), e = aFit.end(); s != e; ++s)
    //    *dbgout << Position(s->first, s->second);
    //*dbgout << json::close // fits array
    *dbgout << "bestCost" << bestCost
        << "bestVal" << bestVal
        << json::close; // vectors object
}

void ShiftCollider::outputJsonDbgRawRanges(GR_MAYBE_UNUSED json * const dbgout, int axis)
{
    *dbgout << "rawRanges" << json::flat << json::array;
    for (IntervalSet::ivtpair s = _rawRanges[axis].begin(), e = _rawRanges[axis].end(); s != e; ++s)
        *dbgout << Position(s->first, s->second);
    *dbgout << _rawRanges[axis].len() << json::close; // rawRanges array
}

void ShiftCollider::outputJsonDbgRemovals(GR_MAYBE_UNUSED json * const dbgout, int axis)
{
    *dbgout << "removals" << json::array;  
            						
    int gi = 0;
    for (IntervalSet::ivtpair s = _removals[axis].begin(), e = _removals[axis].end(); s != e; ++s, ++gi)
    {   //Slot & slotNear = *(_slotNear[axis][gi]);
        if (_slotNear[axis][gi] == exclSlot)
        {
            *dbgout << json::flat << json::array 
                << "exclude" << _subNear[axis][gi] << Position(s->first, s->second) << json::close;
        }
        else
        {
            *dbgout << json::flat << json::array 
                << objectid(dslot(_seg,_slotNear[axis][gi])) 
                << _subNear[axis][gi] << Position(s->first, s->second);
			if (_seqRegions[axis][gi].isValid())
			{
				Rect t(Position(_seqRegions[axis][gi].r1Xedge, _seqRegions[axis][gi].r2Yedge),
					Position(_seqRegions[axis][gi].r3Xedge, _seqRegions[axis][gi].r45Mid));
				*dbgout << "seq:" << t;
			}
			else
				*dbgout << "no-seq";
				
			*dbgout << json::close;
        }
    }
    *dbgout << json::close; // removals array
}

#endif // !defined GRAPHITE2_NTRACING


////    KERN-COLLIDER    ////

// Return the left edge of the glyph at height y, taking any slant box into account.
static float get_left(Segment *seg, const Slot *s, const Position &shift, float y, float width)
{
    const GlyphCache &gc = seg->getFace()->glyphs();
    unsigned short gid = s->gid();
    float sx = s->origin().x + shift.x;
    float sy = s->origin().y + shift.y;
    uint8 numsub = gc.numSubBounds(gid);
    float res = (float)1e38;

    if (numsub > 0)
    {
        for (int i = 0; i < numsub; ++i)
        {
            const BBox &sbb = gc.getSubBoundingBBox(gid, i);
            const SlantBox &ssb = gc.getSubBoundingSlantBox(gid, i);
            if (sy + sbb.yi > y + width / 2 || sy + sbb.ya < y - width / 2)
                continue;
            float x = sx + sbb.xi;
            if (x < res)
            {
                x = std::max(sx - sy + ssb.di + y, std::max(sx + sy + ssb.si - y, x));
                if (x < res)
                    res = x;
            }
        }
    }
    else
    {
        const BBox &bb = gc.getBoundingBBox(gid);
        const SlantBox &sb = gc.getBoundingSlantBox(gid);
        res = std::max(sx + bb.xi, std::max(sx - sy + sb.di + y, sx + sy + sb.si - y));
    }
    return res;
}

// Return the right edge of the glyph at height y, taking any slant boxes into account.
static float get_right(Segment *seg, const Slot *s, const Position &shift, float y, float width)
{
    const GlyphCache &gc = seg->getFace()->glyphs();
    unsigned short gid = s->gid();
    float sx = s->origin().x + shift.x;
    float sy = s->origin().y + shift.y;
    uint8 numsub = gc.numSubBounds(gid);
    float res = (float)-1e38;

    if (numsub > 0)
    {
        for (int i = 0; i < numsub; ++i)
        {
            const BBox &sbb = gc.getSubBoundingBBox(gid, i);
            const SlantBox &ssb = gc.getSubBoundingSlantBox(gid, i);
            if (sy + sbb.yi > y + width / 2 || sy + sbb.ya < y - width / 2)
                continue;
            float x = sx + sbb.xa;
            if (x > res)
            {
                x = std::min(sx - sy + ssb.da + y, std::min(sx + sy + ssb.sa - y, x));
                if (x > res)
                    res = x;
            }
        }
    }
    else
    {
        const BBox &bb = gc.getBoundingBBox(gid);
        const SlantBox &sb = gc.getBoundingSlantBox(gid);
        res = std::min(sx + bb.xa, std::min(sx - sy + sb.da + y, sx + sy + sb.sa - y));
    }
    return res;
}

     

void KernCollider::initSlot(Segment *seg, Slot *aSlot, const Rect &limit, float margin,  float marginMin,
    const Position &currShift, const Position &offsetPrev, int dir, GR_MAYBE_UNUSED json * const dbgout)
{
    const GlyphCache &gc = seg->getFace()->glyphs();
    // unsigned short gid = aSlot->gid();
    // const BBox &bb = gc.getBoundingBBox(gid);
    const Slot *base = aSlot;
    // const Slot *last = aSlot;
    const Slot *s;
    unsigned int maxid = aSlot->index();
    float sliceWidth;
    while (base->attachedTo())
        base = base->attachedTo();
    if (margin < 10) margin = 10;

    _limit = limit;
    _offsetPrev = offsetPrev; // kern from a previous pass
    
    // Calculate the height of the glyph and how many horizontal slices to use.
    _maxy = (float)-1e38;
    _miny = (float)1e38;
    _xbound = (dir & 1) ? (float)1e38 : (float)-1e38;
    _othermax = -_xbound;
    for (s = base; s; s = s->nextInCluster(s))
    {
        SlotCollision *c = seg->collisionInfo(s);
        const BBox &bs = gc.getBoundingBBox(s->gid());
        if (s->index() > maxid)
        {
            // last = s;
            maxid = s->index();
        }
        float y = s->origin().y + c->shift().y;
        _maxy = std::max(_maxy, y + bs.ya);
        _miny = std::min(_miny, y + bs.yi);
    }
    _numSlices = int((_maxy - _miny + 2) / margin + 1.);  // +2 helps with rounding errors
    sliceWidth = (_maxy - _miny + 2) / _numSlices;
    _edges.clear();
    _edges.insert(_edges.begin(), _numSlices, (dir & 1) ? 1e38 : -1e38);
        
#if !defined GRAPHITE2_NTRACING
    // Debugging
    _seg = seg;
    _slotNear.clear();
    _slotNear.insert(_slotNear.begin(), _numSlices, NULL);
    _nearEdges.clear();
    _nearEdges.insert(_nearEdges.begin(), _numSlices, (dir & 1) ? -1e38 : +1e38);
#endif
    
    // Determine the trailing edge of each slice (ie, left edge for a RTL glyph).
    for (s = base; s; s = s->nextInCluster(s))
    {
        SlotCollision *c = seg->collisionInfo(s);
        const BBox &bs = gc.getBoundingBBox(s->gid());
        float x = s->origin().x + c->shift().x + (dir & 1 ? bs.xi : bs.xa);
        // Loop over slices.
        // Note smin might not be zero if glyph s is not at the bottom of the cluster; similarly for smax.
        int smin = std::max(0, int((s->origin().y + c->shift().y + bs.yi - _miny + 1) / (_maxy - _miny + 2) * _numSlices));
        int smax = std::min(_numSlices - 1, int((s->origin().y + c->shift().y + bs.ya - _miny + 1) / (_maxy - _miny + 2) * _numSlices + 1));
        for (int i = smin; i <= smax; ++i)
        {
            float t;
            float y = _miny - 1 + (i + .5) * sliceWidth; // vertical center of slice
            if ((dir & 1) && x < _edges[i])
            {
                t = get_left(seg, s, currShift, y, sliceWidth);
                if (t < _edges[i])
                {
                    _edges[i] = t;
                    if (t < _xbound)
                        _xbound = t;
                }
            }
            else if (!(dir & 1) && x > _edges[i])
            {
                t = get_right(seg, s, currShift, y, sliceWidth);
                if (t > _edges[i])
                {
                    _edges[i] = t;
                    if (t > _xbound)
                        _xbound = t;
                }
            }
        }
    }
    _mingap = (float)1e38;
    _target = aSlot;
    _margin = margin;
    _currShift = currShift;
    
}   // end of KernCollider::initSlot


// Determine how much the target slot needs to kern away from the given slot.
// In other words, merge information from given slot's position with what the target slot knows
// about how it can kern.
// Return false if we know there is no collision, true if we think there might be one.
bool KernCollider::mergeSlot(Segment *seg, Slot *slot, const Position &currShift, float currSpace, int dir, GR_MAYBE_UNUSED json * const dbgout)
{
    const Rect &bb = seg->theGlyphBBoxTemporary(slot->gid());
    const float sx = slot->origin().x + currShift.x;
    const float sy = slot->origin().y + currShift.y;
    int smin = std::max(0, int((sy + bb.bl.y - _miny + 1) / (_maxy - _miny + 2) * _numSlices));
    int smax = std::min(_numSlices - 1, int((sy + bb.tr.y - _miny + 1) / (_maxy - _miny + 2) * _numSlices + 1));
    float sliceWidth = (_maxy - _miny + 2) / _numSlices;
    bool collides = false;

    if (dir & 1)
    {
        float x = sx + bb.tr.x;
        if (x < _xbound - _mingap) // this isn't going to reduce _mingap so skip
            return false;
        for (int i = smin; i <= smax; ++i)
        {
            float t;
            float y = (float)(_miny - 1 + (i + .5) * sliceWidth);  // vertical center of slice
            if (x > _edges[i] - _mingap)
            {
                float m = get_right(seg, slot, currShift, y, sliceWidth) + currSpace;
                if (_othermax < m) _othermax = m;
                t = _edges[i] - m;
                // Check slices above and below (if any).
                if (i < _numSlices - 1) t = std::min(t, _edges[i+1] - m);
                if (i > 0) t = std::min(t, _edges[i-1] - m);
                if (t < _mingap)
                {
                    _mingap = t;
                    collides = true;
                }
#if !defined GRAPHITE2_NTRACING
                // Debugging - remember the closest neighboring edge for this slice.
                if (m > _nearEdges[i])
                {
                    _slotNear[i] = slot;
                    _nearEdges[i] = m;
                }
#endif
            }
        }
    }
    else
    {
        float x = sx + bb.bl.x;
        if (x > _xbound + _mingap + currSpace)
            return false;
        for (int i = smin; i < smax; ++i)
        {
            float t;
            float y = (float)(_miny - 1 + (i + .5) * sliceWidth);  // vertical center of slice
            if (x < _edges[i] + _mingap)
            {
                float m = get_left(seg, slot, currShift, y, sliceWidth) + currSpace;
                if (m > _othermax) _othermax = m;
                t = m - _edges[i];
                if (i < _numSlices - 1) t = std::min(t, m - _edges[i+1]);
                if (i > 0) t = std::min(t, m - _edges[i-1]);
                if (t < _mingap)
                {
                    _mingap = t;
                    collides = true;
                }

#if !defined GRAPHITE2_NTRACING
                // Debugging - remember the closest neighboring edge for this slice.
                if (m < _nearEdges[1])
                {
                    _slotNear[i] = slot;
                    _nearEdges[i] = m;
                }
#endif
            }
        }
    }
    return collides;   // note that true is not a necessarily reliable value
    
}   // end of KernCollider::mergeSlot


// Return the amount to kern by.
// TODO: do we need to make use of marginMin here? Probably not.
Position KernCollider::resolve(GR_MAYBE_UNUSED Segment *seg, GR_MAYBE_UNUSED Slot *slot, int dir, float margin,
        GR_MAYBE_UNUSED json * const dbgout)
{
    float resultNeeded = (1 - 2 * (dir & 1)) * (_mingap - margin);
    float result = min(_limit.tr.x - _offsetPrev.x, max(resultNeeded, _limit.bl.x - _offsetPrev.x));
//    const SlotCollision *cslot = seg->collisionInfo(slot);
    // PUT SOMETHING IN HERE TO HANDLE ORDER-ENFORCEMENT
//    if (cslot->flags() & SlotCollision::COLL_OVERLAP && _othermax - _xbound - _mingap > -1 * cslot->maxOverlap())
//        resultNeeded = (1 - 2 * (dir & 1)) * (_xbound - _othermax + cslot->maxOverlap() + margin);
    

#if !defined GRAPHITE2_NTRACING
    float sliceWidth = (_maxy - _miny + 2) / _numSlices; // copied from above
    if (dbgout)
    {
        *dbgout << json::object // slot
                << "slot" << objectid(dslot(seg, _target))
				<< "gid" << _target->gid()
                << "margin" << _margin
//              << "marginmin" << _marginMin -- not really used
                << "limit" << _limit
                << "target" << json::object
                    << "origin" << _target->origin()
                    //<< "currShift" << _currShift
                    << "offsetPrev" << _offsetPrev
                    << "bbox" << seg->theGlyphBBoxTemporary(_target->gid())
                    << "slantBox" << seg->getFace()->glyphs().slant(_target->gid())
                    << "fix" << "kern"
                    << "slices" << _numSlices
                    << "sliceWidth" << sliceWidth
                    << json::close; // target object
        
        *dbgout << "slices" << json::array;
        for (int is = 0; is < _numSlices; is++)
        {
            *dbgout << json::flat << json::object 
                << "i" << is 
                << "targetEdge" << _edges[is]
                << "neighbor" << objectid(dslot(seg, _slotNear[is]))
                << "nearEdge" << _nearEdges[is] 
                << json::close;
        }
        *dbgout << json::close; // slices array
            
        *dbgout
            << "xbound" << _xbound
            << "minGap" << _mingap
            << "needed" << resultNeeded
            << "result" << result
            << "stillBad" << (result != resultNeeded)
            << json::close; // slot object
    }
#endif

    return Position(result, 0.);
    
}   // end of KernCollider::resolve


////    SLOT-COLLISION    ////

// Initialize the collision attributes for the given slot.
SlotCollision::SlotCollision(Segment *seg, Slot *slot)
{
    initFromSlot(seg, slot);
}

void SlotCollision::initFromSlot(Segment *seg, Slot *slot)
{
    // Initialize slot attributes from glyph attributes.
	// The order here must match the order in the grcompiler code, 
	// GrcSymbolTable::AssignInternalGlyphAttrIDs.
    uint16 gid = slot->gid();
    uint16 aCol = seg->silf()->aCollision(); // flags attr ID
    _flags = seg->glyphAttr(gid, aCol);
    _status = _flags;
    _limit = Rect(Position(seg->glyphAttr(gid, aCol+1), seg->glyphAttr(gid, aCol+2)),
                  Position(seg->glyphAttr(gid, aCol+3), seg->glyphAttr(gid, aCol+4)));
    _margin = seg->glyphAttr(gid, aCol+5);
    _marginWt = seg->glyphAttr(gid, aCol+6);

    // TODO: do we want to initialize collision.exclude stuff from the glyph attributes,
    // or make GDL do it explicitly?
//  _exclGlyph = seg->glyphAttr(gid, aCol+7);
//  _exclOffset = Position(seg->glyphAttr(gid, aCol+8), seg->glyphAttr(gid, aCol+9));
    _exclGlyph = 0;
    _exclOffset = Position(0, 0);

    _seqClass = seg->glyphAttr(gid, aCol+10);
    _seqOrder = seg->glyphAttr(gid, aCol+11);
	_seqAboveXoff = seg->glyphAttr(gid, aCol+12);
	_seqAboveWt = seg->glyphAttr(gid, aCol+13);
	_seqBelowXlim = seg->glyphAttr(gid, aCol+14);
	_seqBelowWt = seg->glyphAttr(gid, aCol+15);
	_seqValignHt = seg->glyphAttr(gid, aCol+16);
	_seqValignWt = seg->glyphAttr(gid, aCol+17);    

	//_canScrape[0] = _canScrape[1] = _canScrape[2] = _canScrape[3] = true;
}

float SlotCollision::getKern(int dir) const
{
    if ((_flags & SlotCollision::COLL_KERN) != 0)
        return float(_shift.x * ((dir & 1) ? -1 : 1));
    else
    	return 0;
}
