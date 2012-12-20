/** map.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree linear probing hash table with chaining resizes.
*/

#ifndef __lockless_map_h__
#define __lockless_map_h__

#include "atomizer.h"

#include <atomic>

namespace lockless {

/******************************************************************************/
/* MAP                                                                        */
/******************************************************************************/

template<typename K, typename V>
struct Map
{
private:

    Container<K>::type Key;
    Container<V>::type Value;

    struct Table
    {
        size_t capacity;

        std::atomic<uint64_t> refCount;
        Table* child;

        std::pair<Key, Value> buckets[];
    };

public:

    Map(size_t initialSize = 0) :
        size(0), table(0),
    {
        resize(initialSize);
    }

    size_t size() const { return size.load(); }
    size_t capacity() const { return table.load()->capacity; }

    void resize(size_t capacity)
    {
        resizeImpl(adjustCapacity(capacity));
    }

private:

    // \todo could do better with a nlz bit op.
    size_t adjustCapacity(size_t newCapacity)
    {
        uint64_t capacity = 1ULL << 8;
        while(capacity < newCapacity)
            capacity *= 2;
    }

    void resizeImpl(size_t newCapacity)
    {
        while(true) {
            size_t tableSize = newCapacity;
            tableSize *= sizeof(std::pair<Key, Value>);
            tableSize += sizeof(Table);

            std::unique_ptr<Table*> newTable(operator new(tableSize));
            newTable->capacity = newCapacity;
            newTable->refCount = 1;

            Table* oldTable = table.load();
            newTable->child = oldTable;

            if (!oldTable.compare_exchange_strong(oldTable, newTable))
                continue;

            exitTable(oldTable);
            return;
        }
    }

    Table* enterTable()
    {
        while(true) {
            Table* oldTable = table.load();
            auto& refCount = table->refCount;

            uint64_t oldCount = refCount.load();
            if (!oldCount) continue;

            if (refCount.compare_exchange_weak(oldCount, oldCount + 1))
                return oldTable;
        }
    }

    void exitTable(Table* table)
    {
        auto& refCount = table->refCount;

        uint64_t oldCount = refCount.fetch_sub(1);
        assert(oldCount >= 0);

        if (!oldCount) delete(table);
    }

    std::atomic<uint64_t> size;
    std::atomic<Table*> table;
};

} // namespace lockless

#endif // __lockless_map_h__
