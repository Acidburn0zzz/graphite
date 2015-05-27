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
#pragma once

#include <utility>
#include "inc/List.h"
#include "inc/Slot.h"
#include "inc/Position.h"
#include "inc/Intervals.h"
#include "inc/debug.h"
#include "inc/Segment.h"

namespace graphite2 {

class json;
class Slot;

#define SLOTCOLSETUINTPROP(x, y) uint16 x() const { return _ ##x; } void y (uint16 v) { _ ##x = v; }
#define SLOTCOLSETPOSITIONPROP(x, y) const Position &x() const { return _ ##x; } void y (const Position &v) { _ ##x = v; }

// Slot attributes related to collision-fixing
class SlotCollision
{
public:
    enum {
    //  COLL_TESTONLY = 0,  // default - test other glyphs for collision with this one, but don't move this one
        COLL_FIX = 1,       // fix collisions involving this glyph
        COLL_IGNORE = 2,    // ignore this glyph altogether
        COLL_START = 4,     // start of range of possible collisions
        COLL_END = 8,       // end of range of possible collisions
        COLL_KERN = 16,     // collisions with this glyph are fixed by adding kerning space after it
        COLL_ISCOL = 32,    // this glyph has a collision
        COLL_KNOWN = 64,    // we've figured out what's happening with this glyph
        ////COLL_JUMPABLE = 128,    // moving glyphs may jump this stationary glyph in any direction - DELETE
        ////COLL_OVERLAP = 256,    // use maxoverlap to restrict - DELETE
    };
    
    // Behavior for the collision.order attribute:
    enum {
        COLL_ORDER_LEFT = 1,
        COLL_ORDER_RIGHT = 2,
        COLL_ORDER_DOWN = 4,
        COLL_ORDER_UP = 8,
        COLL_ORDER_XOVERY = 16
    };
    
    SlotCollision(Segment *seg, Slot *slot);
    void initFromSlot(Segment *seg, Slot *slot);
    
    const Rect &limit() const { return _limit; }
    void setLimit(const Rect &r) { _limit = r; }
    SLOTCOLSETPOSITIONPROP(shift, setShift)
    SLOTCOLSETPOSITIONPROP(offset, setOffset)
    SLOTCOLSETPOSITIONPROP(exclOffset, setExclOffset)
    SLOTCOLSETUINTPROP(margin, setMargin)
    SLOTCOLSETUINTPROP(marginWt, setMarginWt)
    SLOTCOLSETUINTPROP(flags, setFlags)
    SLOTCOLSETUINTPROP(status, setStatus)
    SLOTCOLSETUINTPROP(exclGlyph, setExclGlyph)
    SLOTCOLSETUINTPROP(seqClass, setSeqClass)
    SLOTCOLSETUINTPROP(seqOrder, setSeqOrder)
    SLOTCOLSETUINTPROP(seqAboveXoff, setSeqAboveXoff)
    SLOTCOLSETUINTPROP(seqAboveWt, setSeqAboveWt)
    SLOTCOLSETUINTPROP(seqBelowXlim, setSeqBelowXlim)
    SLOTCOLSETUINTPROP(seqBelowWt, setSeqBelowWt)
    SLOTCOLSETUINTPROP(seqValignHt, setSeqValignHt)
    SLOTCOLSETUINTPROP(seqValignWt, setSeqValignWt)

    float getKern(int dir) const;

	//bool canScrape(int axis) { return _canScrape[axis]; }
	//void setCanScrape(int axis, bool f) { _canScrape[axis] = f; }
    
private:
    Rect        _limit;
    Position    _shift;     // adjustment within the given pass
    Position    _offset;    // total adjustment for collisions
    Position    _exclOffset;
    uint16		_margin;
    uint16		_marginWt;
    uint16		_flags;
    uint16		_status;
    uint16		_exclGlyph;
    uint16		_seqClass;
    uint16		_seqOrder;
    uint16		_seqAboveXoff;
    uint16		_seqAboveWt;
    uint16		_seqBelowXlim;
    uint16		_seqBelowWt;
    uint16		_seqValignHt;
    uint16		_seqValignWt;

	// For use by algorithm:
	//bool _canScrape[4];
};


class ShiftCollider
{
public:
    typedef std::pair<float, float> fpair;
    typedef Vector<fpair> vfpairs;
    typedef vfpairs::iterator ivfpairs;

    ShiftCollider()
    {
        exclSlot = new Slot();
    }
    ~ShiftCollider() throw()
    {
        if (exclSlot) delete exclSlot;
    }
    void initSlot(GR_MAYBE_UNUSED Segment *seg, GR_MAYBE_UNUSED Slot *aSlot, GR_MAYBE_UNUSED const Rect &constraint,
                GR_MAYBE_UNUSED float margin, GR_MAYBE_UNUSED float marginMin, GR_MAYBE_UNUSED const Position &currShift,
                const Position &currOffset, GR_MAYBE_UNUSED int dir, GR_MAYBE_UNUSED json * const dbgout);
    bool mergeSlot(GR_MAYBE_UNUSED Segment *seg, GR_MAYBE_UNUSED Slot *slot, 
                GR_MAYBE_UNUSED const Position &currShift, bool isAfter, bool sameCluster, GR_MAYBE_UNUSED json * const dbgout);
    Position resolve(GR_MAYBE_UNUSED Segment *seg, GR_MAYBE_UNUSED bool &isCol, GR_MAYBE_UNUSED json * const dbgout);
    void addBox_slopex(const Rect &box, const Rect &org, float weight, float m, bool minright, int mode);
    void addBox_slopey(const Rect &box, const Rect &org, float weight, float m, bool mintop, int mode);
    void removeBox(const Rect &box, const Rect &org, int mode);
//    float len(int i) const { return _len[i]; }
//    void setLen(int i, float v) { _len[i] = v; }

#if !defined GRAPHITE2_NTRACING
    void debug(json * const dbgout, Segment *seg, int i) {
//        if (!dbgout) return;
        int imax = i;
        if (i < 0)
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
            i = 0;
            imax = 3;
        }
        for (int j = i; j <= imax; ++j)
        {
            *dbgout << json::flat << json::array;
            for (Zones::const_eiter_t s = _ranges[j].begin(), e = _ranges[j].end(); s != e; ++s)
                *dbgout << Position(s->x, s->xm);
            *dbgout << json::close;
        }
        if (i < imax) // looped through the _ranges array
            *dbgout << json::close; // ranges array
    }
#endif

    CLASS_NEW_DELETE;

protected:
    Zones _ranges[4];   // possible movements in 4 directions (horizontally, vertically, diagonally);
    Slot *  _target;    // the glyph to fix
    Rect    _limit;
    Position _currShift;
    Position _currOffset;
    float   _margin;
	float	_marginWt;
    float   _len[4];
    uint16  _seqClass;
    uint16  _seqOrder;
	// Do we need to store all these in here?
	// For now we just get them out of the SlotCollision object.
	//uint16	_seqAboveXoff;
	//uint16	_seqAboveWt;
	//uint16	_seqBelowXlim;
	//uint16	_seqBelowWt;
	//uint16	_seqValignHt;
	//uint16	_seqValignWt;
    
    Slot * exclSlot;   // bogus exclude slot

	//bool _scraping[4];
    
#if !defined GRAPHITE2_NTRACING
    // Debugging
    Segment * _seg;
    IntervalSet _rawRanges[4];
    IntervalSet _removals[4];
    Vector<Slot*>_slotNear[4];
    Vector<int> _subNear[4];    // sub-box of the neighboring glyph; -1 if no sub-boxes
#endif

};

class KernCollider
{
public:
    ~KernCollider() throw() { };
    void initSlot(Segment *seg, Slot *aSlot, const Rect &constraint, float margin, float marginMin,
            const Position &currShift, const Position &offsetPrev, int dir, json * const dbgout);
    bool mergeSlot(Segment *seg, Slot *slot, const Position &currShift, float currSpace, int dir, json * const dbgout);
    Position resolve(Segment *seg, Slot *slot, int dir, float margin, json * const dbgout);

    CLASS_NEW_DELETE;

private:
    Slot *  _target;        // the glyph to fix
    Rect    _limit;
    float   _margin;
    Position _offsetPrev;   // kern from a previous pass
    Position _currShift;    // NOT USED??
    float _miny;	        // y-coordinates offset by global slot position
    float _maxy;
    Vector<float> _edges; // edges of horizontal slices
    int   _numSlices;     // number of slices
    float _mingap;
    float _xbound;        // max or min edge
    float _othermax;      // bounding value of tested glyphs

#if !defined GRAPHITE2_NTRACING    
    // Debugging
    Segment * _seg;
    Vector<float> _nearEdges; // closest potential collision in each slice
    Vector<Slot*> _slotNear;
#endif
};



};
