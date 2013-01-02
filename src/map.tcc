/** map.tcc                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 22 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Implementation details of the Map template

    A bucket operates a bit like a state machine with 4 states for the key and
    the same 4 states for the value:

    - isEmpty: Nothing has been placed in this bucket's key/value.
    - isValue: A user's key/value has been placed in this bucket.
    - isMoving: The key/value is currently being moved and modifications to it
      are currently locked.
    - isTombstone: When applied to either the key or the value, signifies that
      the bucket is dead and nothing can be moved to it anymore.

    The transition between these states are as follow:


        isEmpty *------------  Move
                |            \
                |            |
                | Insert     |
                |            |
                V            |
        isValue *----------- | Delete
                |           \|
                |            |
                | Move       |
                |            |
                V            V
       isMoving *----------> * isTombstone
                     Move

    Important thing to not is that there are no transitions out of tombstone.
    This ensures that we can support the remove operation without having to
    rehash the values that are in our probe window. The downside is that we
    eventually have to clean up the tombstoned values. This is accomplished by
    by triggering a resize of the same capacity which will ignore the tombstoned
    buckets when transfering the values.

    Now the state machine gets quite a bit more complicated if we combine the
    state of both the key and the value. I won't even try to make an ASCII
    diagram of that but the involved operations below (moveBucket, insertImpl,
    findImpl, replaceImpl, removeImpl) should document all the possibilities
    fairly well.

    The doc for moveBucket and insertImpl also introduce the idea behind the
    probe window and how it allows us to determine in constant time whether a
    value is in a table or not. Note that there is no linear worst-case time for
    this hash table. The worst-case is actually determined by how many chained
    table we could potentially probe and, theorically, this would be log n. On
    the practical side, the number of chained table will depend on how stable
    the size is and the frequency of remove operations. I have yet to test this
    but chances are that it will rarely go beyond 2-3 chained tables.
 */

namespace lockless {

/******************************************************************************/
/* UTILITIES                                                                  */
/******************************************************************************/

namespace details {

template<typename Magic, typename Atom>
bool isValue(Atom atom)
{
    return (atom & ~(Magic::mask0 | Magic::mask1)) == atom;
}

template<typename Magic, typename Atom>
bool isEmpty(Atom atom)
{
    return (atom & (Magic::mask0 | Magic::mask1)) == Magic::mask0;
}

template<typename Magic, typename Atom>
bool isTombstone(Atom atom)
{
    return (atom & (Magic::mask0 | Magic::mask1)) == Magic::mask1;
}

template<typename Magic, typename Atom>
Atom setTombstone(Atom)
{
    return Magic::mask1;
}

template<typename Magic, typename Atom>
bool isMoving(Atom atom)
{
    Atom mask = Magic::mask0 | Magic::mask1;
    return (atom & mask) == mask;
}

template<typename Magic, typename Atom>
Atom setMoving(Atom atom)
{
    return atom | Magic::mask0 | Magic::mask1;
}

template<typename Magic, typename Atom>
Atom clearMarks(Atom atom)
{
    return atom & ~(Magic::mask0 | Magic::mask1);
}

enum Policy
{
    ProbeWindow = 8,
    TombstoneThreshold = 4,
};

} // namespace details

/******************************************************************************/
/* MAP                                                                        */
/******************************************************************************/

template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
bool
Map<Key, Value, Hash, MKey, MValue>::
doMoveBucket(Table* t, Bucket& bucket)
{
    if (!t->isResizing()) return false;
    moveBucket(t->next.load(), bucket);
    return true;
}


/* This op works in 3 phases to work around these 2 problems:

   1. An inserted KV pair must be in at least one table at all time.
   2. An inserted KV pair must only be modifiable in one table.

   If for example we were only able to tombstone then we'd have 2 options (all
   other options are a variation of these 2 options):

   1. tombstone the KV in src and copy in dest. If a find occurs in between
      those 2 steps then the KV pair will effectively not be in the table and
      the call would fail.

   2. copy the KV in dest and tombstone in src. If a replace op occurs in
      between those two steps then the move op would be unaware of it and would
      discard the modification when it tombstones the KV pair.

   Because of this, we need to introduce a new state, isMoving, which allows us
   to lock a KV in src while we copy it to dest. Once the copy is done we can
   safely tombstone the KV pair.

   If a read or write op encounters an isMoving KV or detects an ongoing resize,
   it switches into move mode where it transfers the buckets within its probing
   window to the new table before trying the op again on the new table.

   The probing window is the limit of buckets that an op can probe before giving
   up and triggering a resize. This effectively means that if we move all the
   KVs in the probing window, then we're sure that the key we're looking for has
   been moved if it was present. It is therefor safe to probe the next table
   once the probing window for the current table has been fully moved.

   Fun!
 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
moveBucket(Table* dest, Bucket& src)
{
    using namespace details;

    // 1. Read the KV pair in src and prepare them for moving.

    KeyAtom oldKeyAtom = src.keyAtom.load();
    KeyAtom keyAtom;
    do {
        if (isTombstone<MKey>(oldKeyAtom)) return;
        if (isMoving<MKey>(oldKeyAtom)) {
            keyAtom = oldKeyAtom;
            break;
        }

        if (isEmpty<MKey>(oldKeyAtom))
            keyAtom = setTombstone<MKey>(oldKeyAtom);
        else keyAtom = setMoving<MKey>(oldKeyAtom);

    } while(!src.keyAtom.compare_exchange_weak(oldKeyAtom, keyAtom));


    ValueAtom oldValueAtom = src.valueAtom.load();
    ValueAtom valueAtom;
    do {
        if (isTombstone<MValue>(oldValueAtom)) return;
        if (isMoving<MValue>(oldValueAtom)) {
            valueAtom = oldValueAtom;
            break;
        }

        if (isEmpty<MValue>(oldValueAtom))
                valueAtom = setTombstone<MValue>(oldValueAtom);
        else keyAtom = setMoving<MValue>(oldValueAtom);

    } while (!src.valueAtom.compare_exchange_weak(oldValueAtom, valueAtom));


    // Move is already done, go somewhere else.
    if (isTombstone<MKey>(keyAtom) && isTombstone<MValue>(valueAtom))
        return;


    // 2. Move the value to the dest table.
    if (isMoving<MKey>(keyAtom) && isMoving<MValue>(valueAtom)) {

        keyAtom = clearMarks<MKey>(keyAtom);
        valueAtom = clearMarks<MValue>(valueAtom);

        Key key = KeyAtomizer::load(keyAtom);
        size_t hash = hashFn(key);

        // Regardless of the return value of this function, the KV will have
        // been moved to a new table. Either because we did it or because
        // another thread beat us to it. In either cases, the KV was moved so
        // we're happy.
        insertImpl(dest, hash, key, keyAtom, valueAtom, DeallocNone);
    }


    // 3. Kill the values in the src table.

    // The only possible transition out of the moving state is to tombstone so
    // we don't need any RMW ops here.
    src.keyAtom.store(setTombstone<MKey>(keyAtom));
    src.valueAtom.store(setTombstone<MValue>(valueAtom));

    return;
}


/* The op can be either in insert or move mode.

   Move mode is enabled when there's an ongoing resize on the table. When in
   resize mode we first move all the KVs within our probing window to the newest
   table before we restart the operation on the new table.

   Otherwise we proceed to insert our KV pair in 2 phases: we first try to
   insert the key and then the value. The key isn't officially inserted until
   both these operations are completed which means that even if the key is set,
   the operation could still fail.

   If we exhaust our probing window then one of 3 scenarios could have occured:

   1. The table's load factor is getting too high. Lower it by resizing with the
      twice the current size.

   2. The table is getting clogged up with tombstones. Clean them up by copying
      all the non-tombstone entries to a fresh table of the same size.

   3. There's an ongoing resize. In this case we've just finished moving our
      probing window to the new table.

   In all 3 cases, we need to restart the insert operation so we do a recursive
   on the next table. Note that we can't skip straight to newest or we would
   break the invariants of our table chain.

   Also note that by the time we recurse, the key we're looking for is
   guaranteed to have been moved down the table chain if it was present in this
   table. This holds because we've just exhausted our probing window which means
   that the key can't be in it because it either got moved to the next table in
   the chain or it was never in there to begin with.

 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
bool
Map<Key, Value, Hash, MKey, MValue>::
insertImpl(
        Table* t,
        const size_t hash,
        const Key& key,
        const KeyAtom keyAtom,
        const ValueAtom valueAtom,
        DeallocAtom dealloc)
{
    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Set the key.

        KeyAtom bucketKeyAtom = bucket.keyAtom.load();

        if (isTombstone<MKey>(bucketKeyAtom)) {
            tombstones++;
            continue;
        }

        // Ongoing move, switch to move mode
        if (isMoving<MKey>(bucketKeyAtom)) {
            --i;
            continue;
        }

        if (!isEmpty<MKey>(bucketKeyAtom)) {
            if (key != KeyAtomizer::load(bucketKeyAtom))
                continue;
        }

        // If the key in the bucket is a match we can skip this step. The
        // idea is if we have 2+ concurrent inserts for the same key then
        // the winner is determined when writing the value.
        else if (!bucket.keyAtom.compare_exchange_weak(bucketKeyAtom, keyAtom)){
            // Someone beat us; recheck the bucket to see if our key wasn't
            // involved.
            --i;
            continue;
        }

        // If we inserted our key in the table then don't dealloc it. Even if
        // the call fails!
        else dealloc = clearDeallocFlag(dealloc, DeallocKey);


        // 2. Set the value.

        ValueAtom bucketValueAtom = bucket.valueAtom.load();

        bool dbg_once = false;

        // This loop can only start over once and the case only executed
        // once. The first pass is to avoid a pointless cas (an
        // optimization). The second go around is to redo the same checks after
        // we fail.
        while (true) {

            // Beaten by a move or a delete operation. Retry the bucket.
            if (isMoving<MValue>(bucketValueAtom) ||
                    isTombstone<MValue>(bucketValueAtom))
            {
                --i;
                break;
            }

            // Another insert beat us to it.
            if (!isEmpty<MValue>(bucketValueAtom)) {
                deallocAtomNow(dealloc, keyAtom, valueAtom);
                return false;
            }

            assert(!dbg_once);
            dbg_once = true;

            if (bucket.keyAtom.compare_exchange_strong(bucketValueAtom, valueAtom)){
                DeallocAtom newFlag = clearDeallocFlag(dealloc, DeallocValue);
                deallocAtomNow(newFlag, keyAtom, valueAtom);
                return true;
            }
        }
    }

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return insertImpl(t->next.load(), hash, key, keyAtom, valueAtom, dealloc);
}


/* Follows the same basic principles as insert: probe windows and move mode. The
   action for each bucket state are fairly straightforward and explained in the
   function itself.

   Like insert this operate in two phases: read the key and read the value.
   Since we may compete with an insert for the very same key, we only consider
   that a KV pair has been inserted if both the key and the value are set in the
   bucket. Same will be true for all ops that must first find the key before
   modifying it.
 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
auto
Map<Key, Value, Hash, MKey, MValue>::
findImpl(Table* t, const size_t hash, const Key& key)
    -> std::pair<bool, Value>
{
    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Check the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        if (isTombstone<MKey>(keyAtom)) {
            tombstones++;
            continue;
        }

        if (isEmpty<MKey>(keyAtom))
            return std::make_pair(false, Value());

        // Retry the bucket to help out with the move.
        if (isMoving<MKey>(keyAtom)) {
            --i;
            continue;
        }

        if (key != KeyAtomizer::load(keyAtom)) continue;


        // 2. Check the value.

        ValueAtom valueAtom = bucket.valueAtom.load();

        // We might be in the middle of a move op so try the bucket again so
        // that we'll eventually prob the next table if there's one.
        if (isTombstone<MValue>(valueAtom)) {
            --i;
            continue;
        }

        // The insert op isn't finished yet so we consider that the key isn't in
        // the table.
        if (isEmpty<MValue>(valueAtom))
            return std::make_pair(false, Value());

        // In the middle of a move but since we don't need to modify the value
        // we can just play it greedy and return right away.
        if (isMoving<MValue>(valueAtom))
            valueAtom = clearMarks<MValue>(valueAtom);

        return std::make_pair(true, ValueAtomizer::load(valueAtom));
    }


    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return findImpl(t->next.load(), hash, key);
}


/* Straight forward implementation. Find the key, compare and exchange the
   value.
 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
bool
Map<Key, Value, Hash, MKey, MValue>::
compareExchangeImpl(
        Table* t,
        const size_t hash,
        const Key& key,
        Value& expected,
        const ValueAtom desired)
{
    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Find the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        if (isTombstone<MKey>(keyAtom)) {
            tombstones++;
            continue;
        }
        if (isMoving<MKey>(keyAtom)) {
            --i;
            continue;
        }
        if (isEmpty<MKey>(keyAtom)) {
            ValueAtomizer::dealloc(desired);
            return false;
        }

        if (key != KeyAtomizer::load(keyAtom)) continue;


        // 2. Replace the value.

        ValueAtom valueAtom = bucket.valueAtom.load();

        while (true) {

            // We may be in the middle of a move so try the bucket again.
            if (isTombstone<MValue>(valueAtom) || isMoving<MValue>(valueAtom)) {
                --i;
                break;
            }

            // Value not fully inserted yet; just bail.
            if (isEmpty<MValue>(valueAtom)) {
                ValueAtomizer::dealloc(desired);
                return false;
            }

            // make the comparaison.
            Value bucketValue = ValueAtomizer::load(valueAtom);
            if (expected != bucketValue) {
                ValueAtomizer::dealloc(desired);
                expected = bucketValue;
                return false;
            }

            // make the exchange.
            if (bucket.valueAtom.compare_exchange_weak(valueAtom, desired)) {
                // The value comes from the table so defer the dealloc.
                rcu.defer([=] { ValueAtomizer::dealloc(valueAtom); });
                return true;
            }
        }
    }

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return compareExchangeImpl(t->next.load(), hash, key, expected, desired);
}

/* This op is a little more tricky and proceeds in 3 phases: find the key, make
   sure the value is also present and tombstone the KV pair starting with the
   key.

   The tombstone process has to start with the key but we can't tombstone before
   we're sure that the KV pair was fully inserted. We also have to be careful
   with replace op that may change the value after we've tombstoned the key.
 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
auto
Map<Key, Value, Hash, MKey, MValue>::
removeImpl(Table* t, const size_t hash, const Key& key)
    -> std::pair<bool, Value>
{
    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Find the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        if (isTombstone<MKey>(keyAtom)) {
            tombstones++;
            continue;
        }
        if (isMoving<MKey>(keyAtom)) {
            --i;
            continue;
        }
        if (isEmpty<MKey>(keyAtom)) return std::make_pair(false, Value());

        if (key != KeyAtomizer::load(keyAtom)) continue;


        // 2. Check the value

        ValueAtom valueAtom = bucket.valueAtom.load();

        // We may be in the middle of a move so try the bucket again.
        if (isTombstone<MValue>(valueAtom) || isMoving<MValue>(valueAtom)) {
            --i;
            continue;
        }
        // Value not fully inserted yet; just bail.
        if (isEmpty<MValue>(valueAtom)) return std::make_pair(false, Value());


        // 3. Tombstone the bucket

        // Tombstone the key. If we're in a race with another op then just try
        // the bucket again.
        KeyAtom newKeyAtom = setTombstone<MKey>(keyAtom);
        if (!bucket.keyAtom.compare_exchange_strong(keyAtom, newKeyAtom)) {
            --i;
            continue;
        }

        // Reload the value from the bucket in case there's a lagging replace
        // op. Also prevent any further replace op by tombstoning the value.
        ValueAtom newValueAtom = setTombstone<MValue>(valueAtom);
        valueAtom = bucket.valueAtom.exchange(newValueAtom);

        // The atoms come from the table so defer the delete in case someone's
        // still them.
        deallocAtomDefer(DeallocBoth, keyAtom, valueAtom);

        return std::make_pair(true, ValueAtomizer::load(valueAtom));
    }

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return removeImpl(t->next.load(), hash, key);
}


/* Implements the resize policy for the table. */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
doResize(Table* t, size_t tombstones)
{
    if (!t->isResizing()) {
        if (tombstones >= details::TombstoneThreshold)
            resizeImpl(t->capacity, true);
        else resizeImpl(t->capacity * 2, false);
    }
}


/* Pretty straight forward and no really funky logic involved here. */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
resizeImpl(size_t newCapacity, bool force)
{
    Table* oldTable = table.load();

    // 1. Insert the new table in the chain.

    std::unique_ptr<Table, MallocDeleter> safeNewTable;
    bool done;

    do {
        if (oldTable) {
            if (newCapacity < oldTable->capacity) return;
            if (newCapacity == oldTable->capacity) {
                if (!force) return;
                // Looking do to a cleanup but someone else is already doing it.
                if (oldTable->isResizing()) return;
            }

            if (oldTable->isResizing()) {
                oldTable = oldTable->next.load();
                continue;
            }
        }

        if (!safeNewTable)
            safeNewTable.reset(Table::alloc(newCapacity));

        safeNewTable->prev = oldTable ? &oldTable->next : &table;

        Table* prevTable = nullptr;
        done = safeNewTable->prev->compare_exchange_weak(
                prevTable, safeNewTable.get());

    } while(!done);

    Table* newTable = safeNewTable.release();


    // 2. Exhaustively move all the elements of the old table to the new table.
    // Note that we'll receive help from the other threads as well.

    for (size_t i = 0; i < newCapacity; ++i)
        moveBucket(newTable, oldTable->buckets[i]);


    // 3. Get rid of oldTable.

    assert(oldTable->prev);
    assert(oldTable->prev->load() == oldTable);

    // Remove oldTable from the list. We can't use newTable to do this because
    // it to might have been resized and removed from the list.
    oldTable->prev->store(oldTable->next.load());
    rcu.defer([=] { std::free(oldTable); });
}

} // lockless
