/** map.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree linear probing hash table with chaining resizes.
*/

#ifndef __lockless_map_h__
#define __lockless_map_h__

#include "rcu.h"
#include "atomizer.h"
#include "magic.h"
#include "utils.h"

#include <atomic>
#include <memory>
#include <cassert>
#include <cstdlib>

namespace lockless {

/******************************************************************************/
/* MAP                                                                        */
/******************************************************************************/

/* Blah

 */
template<
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename MKey = MagicValue<Key>,
    typename MValue = MagicValue<Value> >
struct Map
{

private:

    typedef details::Atomizer<Key> KeyAtomizer;
    typedef typename KeyAtomizer::type KeyAtom;

    typedef details::Atomizer<Value> ValueAtomizer;
    typedef typename ValueAtomizer::type ValueAtom;

    enum DeallocAtom
    {
        DeallocKey = 1,
        DeallocValue = 2,

        DeallocNone = 0,
        DeallocBoth = DeallocKey | DeallocValue,
    };

    DeallocAtom clearDeallocFlag(DeallocAtom dealloc, DeallocAtom flag)
    {
        return static_cast<DeallocAtom>(dealloc & ~flag);
    }

public:

    /* Blah

       Exception Safety: Only throws on calls to new.
     */
    Map(size_t initialSize = 0, const Hash& hashFn = Hash()) :
        hashFn(hashFn), elem(0), table(0)
    {
        resize(adjustCapacity(initialSize));
    }

    /* Blah

       Thread Safety: Completely lock-free and wait-free.

       Exception Safety: Does not throw.
     */
    size_t size() const { return elem.load(); }
    size_t capacity() const { return newestTable()->capacity; }

    /* Blah

       Thread safety: Issues a single call to malloc which could lock.
           Everything else is lock-free and wait-free.

       Exception Safety: Can only throw if malloc, new or delete throws.
     */
    void resize(size_t capacity)
    {
        log.log(LogMap, "resize", "capacity=%ld", capacity);

        RcuGuard guard(rcu);
        resizeImpl(adjustCapacity(capacity));
    }

    /* Blah

       Thread safety: Issues calls to malloc, new and delete which could lock.
           Everything else is lock-free and wait-free.

       Exception Safety: Can only throw if new throws.
     */
    std::pair<bool, Value> find(const Key& key)
    {
        log.log(LogMap, "find", "key=%s", std::to_string(key).c_str());

        RcuGuard guard(rcu);
        return findImpl(table.load(), hashFn(key), key);
    }

    /* Blah

       Thread safety: Issues calls to malloc, new and delete which could lock.
           Everything else is lock-free and wait-free.

       Exception Safety: Can only throw if malloc, new or delete throws.
     */
    bool insert(const Key& key, const Value& value)
    {
        log.log(LogMap, "insert", "key=%s, value=%s",
                std::to_string(key).c_str(), std::to_string(value).c_str());

        RcuGuard guard(rcu);

        size_t hash = hashFn(key);
        KeyAtom keyAtom = KeyAtomizer::alloc(key);
        ValueAtom valueAtom = ValueAtomizer::alloc(value);

        bool success = insertImpl(
                table.load(), hash, key, keyAtom, valueAtom, DeallocBoth);
        if (success) elem++;

        return success;
    }

    /* Blah. Same interface as atomic<T>.compare_exchange()

       Thread safety: Issues calls to malloc, new and delete which could lock.
           Everything else is lock-free and wait-free.

       Exception Safety: Can only throw if malloc, new or delete throws.
     */
    bool compareExchange(const Key& key, Value& expected, const Value& desired)
    {
        log.log(LogMap, "xchg", "key=%s, exp=%s, value=%s",
                std::to_string(key).c_str(),
                std::to_string(expected).c_str(),
                std::to_string(desired).c_str());

        RcuGuard guard(rcu);

        size_t hash = hashFn(key);
        ValueAtom valueAtom = ValueAtomizer::alloc(desired);

        return compareExchangeImpl(table.load(), hash, key, expected, valueAtom);
    }


    /* Blah.

       Thread safety: Issues calls to malloc, new and delete which could lock.
           Everything else is lock-free and wait-free.

       Exception Safety: Can only throw if malloc, new or delete throws.
     */
    std::pair<bool, Value> remove(const Key& key)
    {
        log.log(LogMap, "remove", "key=%s", std::to_string(key).c_str());

        RcuGuard guard(rcu);

        auto result = removeImpl(table.load(), hashFn(key), key);
        if (result.first) elem--;

        return result;
    }

private:

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

        Bucket buckets[1];

        bool isResizing() const { return next.load(); }

        static Table* alloc(size_t capacity)
        {
            size_t size = sizeof(capacity) + sizeof(next);
            size += sizeof(Bucket) * capacity;

            Table* table = static_cast<Table*>(std::malloc(size));
            table->capacity = capacity;
            table->next.store(nullptr);

            for (size_t i = 0; i < capacity; ++i)
                table->buckets[i].init();

            return table;
        }
    };

    // \todo could do better with a nlz bit op.
    size_t adjustCapacity(size_t newCapacity)
    {
        size_t capacity = 1ULL << 5;
        while(capacity < newCapacity)
            capacity *= 2;

        return capacity;
    }

    Table* newestTable() const
    {
        Table* newest = table.load();
        while (newest && newest->next.load())
            newest = newest->next.load();

        return newest;
    }

    size_t bucket(size_t hash, size_t i, size_t capacity)
    {
        // Capacity is an exponent of 2 so this will work just fine.
        return (hash + i) & (capacity - 1);
    }

    void deallocAtomNow(DeallocAtom state, KeyAtom keyAtom, ValueAtom valueAtom)
    {
        if (state & DeallocKey) KeyAtomizer::dealloc(keyAtom);
        if (state & DeallocValue) ValueAtomizer::dealloc(valueAtom);
    }

    void deallocAtomDefer(DeallocAtom state, KeyAtom keyAtom, ValueAtom valueAtom)
    {
        // Don't bother with defer work if it's a no-op
        if (details::IsAtomic<Key>::value && details::IsAtomic<Value>::value)
            return;

        rcu.defer([=] { this->deallocAtomNow(state, keyAtom, valueAtom); });
    }


    // map.tcc functions.

    bool doMoveBucket(Table* t, Bucket& bucket);
    void moveBucket(Table* dest, Bucket& src);

    void doResize(Table* t, size_t tombstones);
    void resizeImpl(size_t newCapacity, bool force = false);

    std::pair<bool, Value> findImpl(
            Table* t, const size_t hash, const Key& key);

    bool insertImpl(
            Table* t,
            const size_t hash,
            const Key& key,
            KeyAtom keyAtom,
            ValueAtom valueAtom,
            DeallocAtom dealloc);

    bool compareExchangeImpl(
            Table* t,
            const size_t hash,
            const Key& key,
            Value& expected,
            ValueAtom desired);

    std::pair<bool, Value> removeImpl(
            Table* t, const size_t hash, const Key& key);


    Hash hashFn;
    Rcu rcu;
    std::atomic<size_t> elem;
    std::atomic<Table*> table;

public:

    DebuggingLog<1024, DebugMap>::type log;

};

} // namespace lockless

// Implementation details.
#include "map.tcc"

#endif // __lockless_map_h__

