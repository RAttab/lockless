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
    return atom & ~(Magic::mask0 | Magic::mask1) == atom;
}

template<typename Magic, typename Atom>
bool isEmpty(Atom atom)
{
    return atom & (Magic::mask0 | Magic::mask1) == Magic::mask0;
}

template<typename Magic, typename Atom>
bool isTombstone(Atom atom)
{
    return atom & (Magic::mask0 | Magic::mask1) == Magic::mask1;
}

template<typename Magic, typename Atom>
Atom setTombstone(Atom atom)
{
    return Magic::mask1;
}

template<typename Magic, typename Atom>
bool isMoving(Atom atom)
{
    uint64_t mask = Magic:: mask0 | Magic::mask1;
    return atom & mask == mask;
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

template<typename K, typename V, typename H, typename MK, typename MV>
bool
Map<K,V,H,MK,MV>::
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
template<typename K, typename V, typename H, typename MK, typename MV>
auto
Map<K,V,H,MK,MV>::
moveBucket(Table* dest, Bucket& src) -> Table*
{
    // 1. Read the KV pair in src and prepare them for moving.

    KeyAtom oldKeyAtom = src.key.load();
    KeyAtom keyAtom;
    do {
        if (isTombstone<MKey>(oldKeyAtom)) return dest;
        if (isMoving<MKey>(oldKeyAtom)) {
            keyAtom = oldKeyAtom;
            break;
        }

        if (isEmpty<MKey>(oldKeyAtom))
            keyAtom = setTombstone<MKey>(oldKeyAtom);
        else keyAtom = setMoving<MKey>(oldKeyAtom);

    } while(!src.key.compare_exchange_weak(oldKeyAtom, keyAtom));


    ValueAtom oldValueAtom = src.value.load();
    ValueAtom valueAtom;
    do {
        if (isTombstone<MValue>(oldValueAtom)) return dest;
        if (isMoving<MValue>(oldValueAtom)) {
            valueAtom = oldValueAtom;
            break;
        }

        if (isEmpty<MValue>(oldValueAtom))
                valueAtom = setTombstone<MValue>(oldValueAtom);
        else keyAtom = setMoving<MValue>(oldValueAtom);

    } while (!src.value.compare_exchange_weak(oldValueAtom, valueAtom));


    // If we're dealing with a partial insert just tombstone the whole thing.
    if (isMoving<MKey>(keyAtom) && isTombstone<MValue>(valueAtom))
        goto doTombstone; // Let the velociraptor strike me!

    // We only check value because that's the last one to be written.
    if (isTombstone<MValue>(valueAtom)) return dest;

    // The move is done so skip to step 3. to help the cleanup.
    if (isTombstone<MKey>(keyAtom)) goto doTombstome;

    std::assert(isMoving<MKey>(keyAtom) && isMonving<MValue>(valueAtom));


    // 2. Move the value to the dest table.

    keyAtom = clearMarks(keyAtom):
    valueAtom = clearMarks(valueAtom):

    Key key = KeyAtomizer::load(keyAtom);
    size_t hash = hashFn(key);

    // Regardless of the return value of this function, the KV will have been
    // moved to a new table. Either because we did it or because another thread
    // beat us to it. In either cases, the KV was moved so we're happy.
    insertImpl(dest, hash, key, keyAtom, valueAtom, DeallocNone);


    // 3. Kill the values in the src table.

  doTombstone:

    // The only possible transition out of the moving state is to tombstone so
    // we don't need any RMW ops here.
    src.key.store(setTombstone<MKey>(keyAtom));
    src.value.store(setTombstone<MValue>(valueAtom));

    return dest;
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
template<typename K, typename V, typename H, typename MK, typename MV>
bool
Map<K,V,H,MK,MV>::
insertImpl(
        Table* t,
        const Key& key,
        const size_t hash,
        const KeyAtom keyAtom,
        const ValueAtom valueAtom,
        DeallocAtom dealloc)
{
    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Set the key.

        KeyAtom bucketKeyAtom = bucket.key.load();

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
            Key bucketKey = KeyAtomizer::load(bucketKeyAtom);
            if (bucketKey != key) continue;
        }

        // If the key in the bucket is a match we can skip this step. The
        // idea is if we have 2+ concurrent inserts for the same key then
        // the winner is determined when writing the value.
        else if (!bucket.key.compare_exchange_weak(bucketKeyAtom, keyAtom)) {
            // Someone beat us; recheck the bucket to see if our key wasn't
            // involved.
            --i;
            continue;
        }

        // If we inserted our key in the table then don't dealloc it. Even if
        // the call fails!
        else dealloc &= ~DeallocKey;


        // 2. Set the value.

        ValueAtom bucketValueAtom = bucket.value.load();

        bool dbg_once = false;

        // This loop can only start over once and the case only executed
        // once. The first pass is to avoid a pointless cas (an
        // optimization). The second go around is to redo the same checks after
        // we fail.
        while (true) {

            // Beaten by a move or a delete operation. Keep probing.
            if (isMoving<MValue>(bucketValueAtom)) break;
            if (isTombstone<MValue>(bucketValueAtom)) break;

            // Another insert beat us to it.
            if (!isEmpty<mValue>(bucketValueAtom)) {
                deallocAtomNow(dealloc, keyAtom, valueAtom);
                return false;
            }

            std::assert(!dbg_once);
            dbg_once = true;

            if (bucket.key.compare_exchange_strong(bucketValueAtom, valueAtom)){
                deallocAtomNow(dealloc & ~DeallocValue, keyAtom, valueAtom);
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
template<typename K, typename V, typename H, typename MK, typename MV>
auto
Map<K,V,H,MK,MV>::
findImpl(Table* t, const size_t hash, const Key& key)
    -> std::pair<bool, Value>
{
    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Check the key

        KeyAtom keyAtom = bucket.key.load();

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

        Key bucketKey = KeyAtomizer::load(keyAtom);
        if (bucketKey != key) continue;


        // 2. Check the value.

        ValueAtom valueAtom = bucket.value.load();

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
            valueAtom = clearMarks(valueAtom);

        Value value = ValueAtomizer::load(valueAtom);
        return make_pair(true, value);
    }


    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return findImpl(t->next.load(), hash, key);
}


template<typename K, typename V, typename H, typename MK, typename MV>
bool
Map<K,V,H,MK,MV>::
compareReplaceImpl(
        Table* t,
        const size_t hash,
        const Key& key,
        Value& expected,
        const ValueAtom desired)
{
    size_t tombstones = 0;

    auto doDealloc = [&] {  };

    for (size_t i = 0; i < ProbeWindow; ++i) {
        Bucket& bucket = t->buckets[this->bucket(hash, i, t->capacity)];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Find the key

        KeyAtom keyAtom = bucket.key.load();

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

        Key bucketKey = KeyAtomizer::load(keyAtom);
        if (key != bucketKey) continue;


        // 2. Replace the value.

        ValueAtom valueAtom = bucket.value.load();

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
            if (bucket.value.compare_exchange_weak(valueAtom, desired)) {
                // The value comes from the table so defer the dealloc.
                rcu.defer([=] { ValueAtomizer::dealloc(valueAtom); };
                return true;
            }
        }
    }

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    return compareAndReplaceImpl(t->next.load(), hash, key, desired);
}


/* Implements the resize policy for the table. */
template<typename K, typename V, typename H, typename MK, typename MV>
void
Map<K,V,H,MK,MV>::
doResize(Table* t, size_t tombstones)
{
    if (!t->isResizing()) {
        if (tombstones >= TombstoneThreshold)
            resizeImpl(t->capacity, true);
        else resizeImpl(t->capacity * 2, false);
    }
}


/* Pretty straight forward and no really funky logic involved here. */
template<typename K, typename V, typename H, typename MK, typename MV>
auto
Map<K,V,H,MK,MV>::
resizeImpl(size_t newCapacity, bool force = false) -> Table*
{
    Table* oldTable;

    // 1. Insert the new table in the chain.

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
                nullptr, safeNewTable.get());

    } while(!done);


    Table* newTable = safeNewTable.release();


    // 2. Exhaustively move all the elements of the old table to the new table.
    // Note that we'll receive help from the other threads as well.

    Table* dest = newTable;
    for (size_t i = 0; i < newCapacity; ++i)
        dest = moveBucket(dest, oldTable->buckets[i]);


    // 3. Get rid of oldTable.

    std::assert(oldTable->prev);
    std::assert(oldTable->prev->load() == oldTable);

    // Don't store newTable because it might have been resized and removed
    // from the list.
    oldTable->prev.store(oldTable->next.load());
    rcu.defer([=] { std::free(oldTable); });

    return newTable;
}

} // lockless
