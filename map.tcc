/** map.tcc                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 22 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Implementation details of the Map template

*/

#ifndef __lockless__map_tcc__
#define __lockless__map_tcc__

namespace lockless {

/******************************************************************************/
/* UTILITIES                                                                  */
/******************************************************************************/

namespace details {

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
checkPolicies(Table* t, size_t probes, size_t tombstones)
{
    if (probes >= RESIZE_THRESHOLD)
        resizeImpl(capacity * 2);

#if 0 // Could trigger a double resizize. Disable for now.
    if (tombstones >= TOMBSTONE_THRESHOLD)
        resizeImpl(capacity, true);
#endif
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


    // We only check value because that's the last one to be written.
    if (isTombstone<MValue>(valueAtom)) return dest;

    // The move is done so skip to step 3. to help the cleanup.
    if (isTombstone<MKey>(keyAtom)) goto out; // Let the velociraptor strike me!


    // 2. Move the value to the dest table.

    std::assert(isMoving<MKey>(keyAtom) && isMonving<MValue>(valueAtom));

    keyAtom = clearMarks(keyAtom):
    valueAtom = clearMarks(valueAtom):

    Key key = KeyAtomizer::load(keyAtom);
    size_t hash = hashFn(key);

    while (true) {
        InsertResult result = insertImpl(dest, hash, key, keyAtom, valueAtom);

        if (result == KeyInserted) break;
        std::assert(result == TryAgain); // != KeyExists.

        // dest is resizing so move on to the next table.
        dest = dest.next.load();
        std::assert(dest);
    }


    // 3. Kill the values in the src table.

  out:

    // The only possible transition out of the moving state is to tombstone so
    // we don't need any RMW ops here.
    src.key.store(setTombstone<MKey>(keyAtom));
    src.value.store(setTombstone<MValue>(valueAtom));

    return dest;
}

template<typename K, typename V, typename H, typename MK, typename MV>
auto
Map<K,V,H,MK,MV>::
insertImpl(
        Table* t, size_t hash,
        Key key, KeyAtom keyAtom,
        ValueAtom valueAtom) -> InsertResult
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



template<typename K, typename V, typename H, typename MK, typename MV>
auto
Map<K,V,H,MK,MV>::
resizeImpl(size_t newCapacity, bool force = false) -> Table*
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

} // lockless

#endif // __lockless__map_tcc__
