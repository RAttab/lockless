/** map.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree linear probing hash table with chaining resizes.
*/

#ifndef __lockless_map_h__
#define __lockless_map_h__

#include "atomizer.h"
#include "utils.h"

#include <atomic>
#include <cstdlib>

namespace lockless {

/******************************************************************************/
/* MAP                                                                        */
/******************************************************************************/

template<
    typename Key,
    typename Value,
    typename Hash = std::hash<K> >
struct Map
{

private:

    details::Atomizer<Key>::type KeyAtom;
    details::Atomizer<Value>::type ValueAtom;

    struct Bucket
    {
        std::atomic<KeyAtom> keyAtom;
        std::atomic<ValueAtom> valueAtom;

        void init()
        {
            keyAtom.store(MagicValue<KeyAtom>::mask);
            valueAtom.store(MagicValue<ValueAtom>::mask);
        }

        bool isKeySet() const
        {
            return !isMagicValue(keyAtom.load();
        }

        bool isValueSet() const
        {
            return !isMagicValue(valueAtom.load());
        }

        bool isSet() const
        {
            return isKeySet() && isValueSet();
        }

        Key key() const
        {
            return Atomizer<Key>::load(keyAtom);
        }

        Value value() const
        {
            return Atomizer<Value>::load(valueAtom);
        }

        void unset()
        {
            KeyAtom keyCopy = keyAtom.load();
            ValueAtom valueCopy = valueAtom.load();

            keyAtom.store(MagicValue<KeyAtom>::mask);
            valueAtom.store(MagicValue<ValueAtom>::mask);

            if (!isMagicValue(keyCopy))
                details::Atomizer::dealloc(keyCopy);

            if (!isMagicValue(valueCopy))
                details::Atomizer::dealloc(valueCopy);
        }
    };

    struct Table
    {
        size_t capacity;
        std::atomic<Table*> next;

        Bucket buckets[1];
    };

    struct TableGuard
    {
        TableGuard(Map* instance, Table* table) :
            instance(instance), table(table)
        {}
        ~TableGuard() { instance->exitTable(table); }

        Table& operator* () const { return *table; }
        Table& operator-> () const { return *table; }

        Bucket& operator[] (size_t index)
        {
            return table->buckets[index];
        }

        Map* instance;
        Table* table;
    };

public:

    Map(size_t initialSize = 0, const Hash& hashFn = Hash()) :
        hashFn(hashFn), size(0), table(0),
    {
        resize(adjustCapacity(initialSize));
    }

    size_t size() const { return size.load(); }
    size_t capacity() const { return table.load()->capacity; }

    void resize(size_t capacity)
    {
        resizeImpl(adjustCapacity(capacity));
    }

    std::pair<bool, V> find(const K& key)
    {
        RcuGuard guard(rcu);


    }

    std::pair<bool, V> insert(const K& key, const V& value)
    {
        RcuGuard guard(rcu);

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

    void resizeImpl(size_t newCapacity, bool force = false)
    {
        RcuGuard guard(rcu);

        std::unique_ptr<Table, MallocDeleter> newTable;

        Table* oldTable;
        do {
            oldTable = newestTable();
            if (force)
                newCapacity = std::max(newCapacity, oldTable->capacity);
            else (newest && newest->capacity >= newCapacity)
                return;

            if (!newTable) {
                // + 1: capacity + next
                newTable.reset(calloc(sizeof(Bucket), newCapacity + 1));
                newTable->capacity = newCapacity;

                for (size_t i = 0; i < newCapacity; ++i)
                    newTable->buckets[i].init();
            }
            newTable->next = oldTable;

            std::atomic<Table*>* toChange = oldTable ? &oldTable->next : &table;

            // Use a strong cas here to avoid calling newestTable.
            if (toChange->compare_and_exchange_strong(oldTable, newTable))
                break;
        }

        for (size_t i = 0; i < newCapacity; ++i) {
            Bucket& bucket = oldTable->buckets[i];

            if (!bucket.isSet()) continue;
            // \todo insert(newTable)
            bucket.unset();
        }

        Table* toDelete = newTable.release();
        rcu.defer([=] { std::free(toDelete); });
    }

    Hash hashFn;
    Rcu rcu;
    std::atomic<uint64_t> size;
    std::atomic<Table*> table;
};

} // namespace lockless

#endif // __lockless_map_h__
