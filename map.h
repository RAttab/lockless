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
    typename Hash = std::hash<K>
    typename MKey = MagicValue<Key>,
    typename MValue = MagicValue<Value> >
struct Map
{

private:

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

    Table* moveBucket(Table* dest, Bucket& src)
    {
        // tombstone the bucket so nothing can be inserted there.
        KeyAtom keyAtom = bucket.exchange(MKey::mask1);
        ValueAtom valueAtom = bucket.load();

        if (isMagicValue(keyAtom) || isMagicValue(valueAtom))
            return;

        Key key = KeyAtomizer::load(keyAtom);
        size_t hash = hashFn(key);

        // The insert may fail if there's a move going into the dest table. In
        // this case load the dest's next table and try to move into that table.
        while(!insert(dest, hash, keyAtom, valueAtom)) {
            Table* next = dest->next.load();

            // Resizing during a resize... What could possibly go wrong?
            dest = next ? next : resizeImpl(dest->capacity * 2);
        }

        return dest;
    }

    Table* resizeImpl(size_t newCapacity, bool force = false)
    {
        RcuGuard guard(rcu);

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

    bool insert(Table* t, size_t hash, KeyAtom keyAtom, ValueAtom valueAtom)
    {
    }

    Hash hashFn;
    Rcu rcu;
    std::atomic<uint64_t> size;
    std::atomic<Table*> table;
};

} // namespace lockless

#endif // __lockless_map_h__
