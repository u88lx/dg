#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include <map>
#include <unordered_map>
#include <set>
#include <cassert>

#include "ADT/Bitvector.h"
#include "analysis/Offset.h"

namespace dg {
namespace analysis {
namespace pta {

// declare PSNode
class PSNode;

struct MemoryObject;
struct Pointer;

extern const Pointer PointerUnknown;
extern const Pointer PointerNull;
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;
extern PSNode *INVALIDATED;

struct Pointer
{
    Pointer(PSNode *n, Offset off = 0) : target(n), offset(off)
    {
        assert(n && "Cannot have a pointer with nullptr as target");
    }

    // PSNode that allocated the memory this pointer points-to
    PSNode *target;
    // offset into the memory it points to
    Offset offset;

    bool operator<(const Pointer& oth) const
    {
        return target == oth.target ? offset < oth.offset : target < oth.target;
    }

    bool operator==(const Pointer& oth) const
    {
        return target == oth.target && offset == oth.offset;
    }

    bool isNull() const { return target == NULLPTR; }
    bool isUnknown() const { return target == UNKNOWN_MEMORY; };
    bool isValid() const { return !isNull() && !isUnknown(); }
    bool isInvalidated() const { return target == INVALIDATED; }
};

class PointsToSet {
    // each pointer is a pair (PSNode *, {offsets}),
    // so we represent them coinciesly this way
    using ContainerT = std::unordered_map<PSNode *, ADT::SparseBitvector>;
    ContainerT pointers;

public:
    bool add(PSNode *target, Offset off) {
        return pointers[target].set(*off);
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, *ptr.offset);
    }

    bool addWithUnknownOffset(PSNode *target) {
        auto it = pointers.find(target);
        if (it != pointers.end()) {
            if (it->second.get(Offset::UNKNOWN)) {
                if (it->second.size() > 1) {
                    it->second.reset();
                    it->second.set(Offset::UNKNOWN);
                }
                return false; // we already had that offset
            } else
                return it->second.set(Offset::UNKNOWN);
        }

        return add(target, Offset::UNKNOWN);
    }

    // make union of the two sets and store it
    // into 'this' set (i.e. merge rhs to this set)
    bool merge(const PointsToSet& rhs) {
        bool changed = false;
        for (auto& it : rhs.pointers) {
            auto &ourS = pointers[it.first];
            changed |= ourS.merge(it.second);
        }

        return changed;
    }

    bool empty() const { return pointers.empty(); }

    size_t count(const Pointer& ptr) {
        auto it = pointers.find(ptr.target);
        if (it != pointers.end()) {
            return it->second.get(*ptr.offset);
        }

        return 0;
    }

    size_t size() {
        size_t num = 0;
        for (auto& it : pointers) {
            num += it.second.size();
        }

        return num;
    }

    void swap(PointsToSet& rhs) { pointers.swap(rhs.pointers); }

    class const_iterator {
        typename ContainerT::const_iterator container_it;
        typename ContainerT::const_iterator container_end;
        typename ADT::SparseBitvector::const_iterator innerIt;

        const_iterator(const ContainerT& cont, bool end = false)
        : container_it(end ? cont.end() : cont.begin()), container_end(cont.end()) {
            if (container_it != container_end) {
                innerIt = container_it->second.begin();
            }
        }
    public:
        const_iterator& operator++() {
            ++innerIt;
            if (innerIt == container_it->second.end()) {
                ++container_it;
                if (container_it != container_end)
                    innerIt = container_it->second.begin();
                else
                    innerIt = ADT::SparseBitvector::const_iterator();
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            return Pointer(container_it->first, *innerIt);
        }

        bool operator==(const const_iterator& rhs) const {
            return container_it == rhs.container_it && innerIt == rhs.innerIt;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class PointsToSet;
    };

    const_iterator begin() const { return const_iterator(pointers); }
    const_iterator end() const { return const_iterator(pointers, true /* end */); }

    friend class const_iterator;
};

using PointsToSetT = PointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

struct MemoryObject
{
    MemoryObject(/*uint64_t s = 0, bool isheap = false, */PSNode *n = nullptr)
        : node(n) /*, is_heap(isheap), size(s)*/ {}

    // where was this memory allocated? for debugging
    PSNode *node;
    // possible pointers stored in this memory object
    PointsToMapT pointsTo;

    PointsToSetT& getPointsTo(const Offset off) { return pointsTo[off]; }

    PointsToMapT::iterator find(const Offset off) {
        return pointsTo.find(off);
    }

    PointsToMapT::const_iterator find(const Offset off) const {
        return pointsTo.find(off);
    }

    PointsToMapT::iterator begin() { return pointsTo.begin(); }
    PointsToMapT::iterator end() { return pointsTo.end(); }
    PointsToMapT::const_iterator begin() const { return pointsTo.begin(); }
    PointsToMapT::const_iterator end() const { return pointsTo.end(); }

    bool addPointsTo(const Offset& off, const Pointer& ptr)
    {
        /*
        if (isUnknown())
            return false;
            */

        assert(ptr.target != nullptr
               && "Cannot have NULL target, use unknown instead");

        return pointsTo[off].add(ptr);
    }

    bool addPointsTo(const Offset& off, const PointsToSetT& pointers)
    {
        /*
        if (isUnknown())
            return false;
            */

        bool changed = false;

        for (const Pointer& ptr : pointers)
            changed |= addPointsTo(off, ptr);

        return changed;
    }


#if 0
    // some analyses need to know if this is heap or stack
    // allocated object
    bool is_heap;
    // if the object is allocated via malloc or
    // similar, we can not infer the size from type,
    // because it is recast to (usually) i8*. Store the
    // size information here, if applicable and available
    uint64_t size;

    bool isUnknown() const;
    bool isNull() const;
    bool isHeapAllocated() const { return is_heap; }
    bool hasSize() const { return size != 0; }
#endif
};

#if 0
extern MemoryObject UnknownMemoryObject;
extern MemoryObject NullMemoryObject;
extern Pointer UnknownMemoryLocation;
extern Pointer NullPointer;
#endif

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
