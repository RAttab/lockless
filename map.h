/** map.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree linear probing hash table with chaining resizes.
*/

#ifndef __lockless_map_h__
#define __lockless_map_h__

#include "atomizer.h"
#include "magic.h"
#include "utils.h"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <type_traits>

namespace lockless {

/******************************************************************************/
/* MAP                                                                        */
/******************************************************************************/

/* Blah

   --- Implementation Details ---

   MagicValue::mask0 -> unset key or value.
       Both must be set before a bucket is available.

   MagicValue::mask1 -> tombstoned bucket.
       Only the key has to be set to kill the bucket.

 */
template<
    typename Key,
    typename Value,
    typename Hash = std::hash<K>,
    typename MKey = MagicValue<Key>,
    typename MValue = MagicValue<Value> >
struct Map
{

private:

    enum Policy
    {
        ResizeThreshold = 4,
        CleanupThreshold = 4
    };

    enum InsertResult
    {
        None = 0,
        KeyExists,
        KeyInserted,
        TryAgain,
    };

    typedef details::Atomizer<Key> KeyAtomizer;
    typedef KeyAtomizer::type KeyAtom;

    typedef details::Atomizer<Value> ValueAtomizer;
    typedef ValueAtomizer::type ValueAtom;

    struct Bucket
    {
        std::atomic<KeyAtom> keyAtom;
        std::atomic<ValueAtom> valueAtom;

        void init()
        {
            keyAtom.store(MKey::mask0);
            valueAtom.store(MValue::mask0);
        }
    };

    struct Table
    {
        size_t capacity;
        std::atomic<Table*> next;
        std::atomic<Table*>* prev;

        Bucket buckets[1];

        static Table* alloc(size_t capacity)
        {
            size_t size = sizeof(capacity) + sizeof(next) + sizeof(prev);
            size += sizeof(Bucket) * capacity;

            Table* table = malloc(size);
            table->capacity = capacity;
            table->next.store(nullptr);
            table->prev = nullptr;

            for (size_t i = 0; i < newCapacity; ++i)
                table->buckets[i].init();

            return table;
        }
    };

public:

    Map(size_t initialSize = 0, const Hash& hashFn = Hash()) :
        hashFn(hashFn), size(0), table(0),
    {
        resize(adjustCapacity(initialSize));
    }

    size_t size() const { return size.load(); }
    size_t capacity() const { return newestTable()->capacity; }

    void resize(size_t capacity)
    {
        RcuGuard guard(rcu);
        resizeImpl(adjustCapacity(capacity));
    }

    std::pair<bool, Value> find(const Key& key)
    {
        RcuGuard guard(rcu);
    }

    bool insert(const Key& key, const Value& value)
    {
        RcuGuard guard(rcu);

        size_t hash = hashFn(key);
        KeyAtom keyAtom = KeyAtomizer::alloc(key);
        ValueAtom valueAtom = ValueAtomizer::alloc(value);

        // ...
    }

private:

    // \todo could do better with a nlz bit op.
    size_t adjustCapacity(size_t newCapacity)
    {
        uint64_t capacity = 1ULL << 8;
        while(capacity < newCapacity)
            capacity *= 2;
    }

    Table* newestTable() const
    {
        Table* newest = table.load();
        while (newest && newest->next.load())
            newest = newest->next.load();

        return newest;
    }


    bool isKeyMarked(KeyAtom atom) const
    {
        return isEmpty(atom) || isTomstone(atom);
    }

    bool isKeyEmpty(KeyAtom atom) const
    {
        return atom & MKey::mask0;
    }

    bool isKeyTombstone(KeyAtom atom) const
    {
        return atom & MKey::mask1;
    }

    bool isValueMarked(ValueAtom atom) const
    {
        return isEmpty(atom) || isTomstone(atom);
    }

    bool isValueEmpty(ValueAtom atom) const
    {
        return atom & MKey::mask0;
    }

    bool isValueTombstone(ValueAtom atom) const
    {
        return atom & MKey::mask1;
    }

    Table* moveBucket(Table* dest, Bucket& src)
    {
        // tombstone the bucket so nothing can be inserted there.
        KeyAtom keyAtom = bucket.exchange(MKey::mask1);
        ValueAtom valueAtom = bucket.exchange(MValue::mask1);

        if (isKeyMarked(keyAtom) || isValueMarked(valueAtom))
            return;

        Key key = KeyAtomizer::load(keyAtom);
        size_t hash = hashFn(key);

        // The insert may fail if there's a move going into the dest table. In
        // this case load the dest's next table and try to move into that table.
        while(!insert(dest, hash, key, keyAtom, valueAtom)) {
            Table* next = dest->next.load();

            // Resizing during a resize... What could possibly go wrong?
            dest = next ? next : resizeImpl(dest->capacity * 2);
        }

        return dest;
    }

    Table* resizeImpl(size_t newCapacity, bool force = false)
    {
        Table* oldTable;

        /* Insert the new table in the chain. */

        std::unique_ptr<Table, MallocDeleter> safeNewTable;
        bool done;

        do {
            oldTable = newestTable();

            if (oldTable) {
                if (newCapacity < oldTable->capacity) return;
                if (!force && newCapacity == oldTable->capacity) return;
            }

            if (!safeNewTable)
                safeNewTable.reset(Table::alloc(newCapacity));

            safeNewTable->prev = oldTable ? &oldTable->next : &table;

            // Use a strong cas here to avoid calling newestTable unecessarily.
            done = safeNewTable->prev->compare_and_exchange_strong(
                    nullptr, safeNewTable.get())
        } while(!done);


        Table* newTable = safeNewTable.release();


        /* Move all the elements of the old table to the new table. */

        Table* dest = newTable;
        for (size_t i = 0; i < newCapacity; ++i)
            dest = moveBucket(dest, oldTable->buckets[i]);


        /* Get rid of oldTable */

        std::assert(oldTable->prev);
        std::assert(oldTable->prev->load() == oldTable);

        // Don't store newTable because it might have been resized and removed
        // from the list.
        oldTable->prev.store(oldTable->next.load());
        rcu.defer([=] { std::free(oldTable); });

        return newTable;
    }

    size_t bucket(size_t hash, size_t i, size_t capacity)
    {
        // Capacity is an exponent of 2 so this will work just fine.
        return (hash + i) & (capacity - 1);
    }

    void checkPolicies(size_t capacity, size_t probes, size_t tombstones)
    {
        if (probes >= RESIZE_THRESHOLD)
            resizeImpl(capacity * 2);

#if 0 // Could trigger a double resizize. Disable for now.
        if (tombstones >= TOMBSTONE_THRESHOLD)
            resizeImpl(capacity, true);
#endif
    }

    /*

       Horribly complicated explanation of why this won't race with moveBucket:

       So moveBucket moves items by tombstombing both K & V then moves them to
       the new table. Now if an insert or find or whatever for that key shows up
       in between the move then it has to go through the table being moved.

       This leads to the following scenarios:

       - The op is there before the tombstoning. At which point it wins and the
         move op moves the value. Nothing too complicated there.

       - The op is there during the tombstoning and wins. See above.

       - The op is there during the tombstoning and losses. At which point it
         has to try again on the same table. This turns out to be the same as
         the scenario below.

         \todo Blargh bug...
     */
    InsertResult
    insertImpl(
            Table* t, size_t hash,
            Key key, KeyAtom keyAtom,
            ValueAtom valueAtom)
    {
        InsertResult result = None;
        size_t i;
        size_t tombstones = 0;

        for (i = 0; i < t->capacity; ++i) {
            Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];

            KeyAtom bucketKeyAtom = bucket.key.load();
            if (isKeyTomstone(bucketKeyAtom)) {
                tombstones++;
                continue;
            }

            if (!isKeyEmpty(bucketKeyAtom)) {
                Key bucketKey = KeyAtomizer::load(bucketKeyAtom);

                if (bucketKey != key) {
                    // \todo if t->isResizing then move(newest(), bucket);
                    continue;
                }
            }

            // If the key in the bucket is a match we can skip this step. The
            // idea is if we have 2+ concurrent inserts for the same key then
            // the winner is determined when writing the value.
            else if (!bucket.key.compare_exchange_weak(bucketKeyAtom, keyAtom)) {
                // Someone beat us; try again the same bucket again.
                --i;
                continue;
            }


            ValueAtom bucketValueAtom = bucket.value.load();

            size_t dbg_loops = 0;

            // This loop can only start over once. The first pass is to avoid a
            // pointless cas (an optimization). The second go around is to redo
            // the same checks after we fail. This means that the cas is only
            // executed once.
            do {
                // Another insert beat us to it.
                if (!isValueEmpty(bucketValueAtom)) {
                    result = KeyExists;
                    break;
                }

                // Beaten by a move operation. Since we're interfering with a
                // move, no point to keep on trying.
                if (isValueTombstone(bucketValueAtom)) {
                    result = TryAgain;
                    break;
                }

                assert(!dbg_loops);
                dbg_loops++;

            } while (!bucket.key.compare_exchange_strong(bucketValueAtom, valueAtom));

            if (!result) result = KeyInserted;
            break;
        }

        checkPolicies(t->capacity, i, tombstones);
        return result;
    }

    Hash hashFn;
    Rcu rcu;
    std::atomic<uint64_t> size;
    std::atomic<Table*> table;
};

} // namespace lockless

#endif // __lockless_map_h__
