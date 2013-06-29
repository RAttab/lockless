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

    Important thing to note is that there are no transitions out of tombstone.
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

#include "check.h"

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
Atom setValue(Atom atom)
{
    return atom & ~(Magic::mask0 | Magic::mask1);
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

template<typename Magic, typename Atom>
char fmtState(Atom atom)
{
    if (isValue<Magic>(atom)) return 'v';
    if (isEmpty<Magic>(atom)) return 'e';
    if (isMoving<Magic>(atom)) return 'm';
    if (isTombstone<Magic>(atom)) return 't';
    return '?';
}

template<typename Magic, typename Atom>
std::string fmtAtom(Atom atom)
{
    char s = fmtState<Magic>(atom);

    if (s != 'v' && s != 'm')
        return format("{%c}", s);

    Atom val = clearMarks<Magic>(atom);
    return format("{%c,%ld}", s, val);
}

enum Policy
{
    ProbeWindow = 8,

    /* A double call to remove on the same key can lead to serious trouble if
       the probe window is full (I blame using identity function for std::hash
       on ints). This will lead to a doubling of the table size everytime.
     */
    TombstoneThreshold = 1,
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

    log(LogMap, "move-0", "table=%p, next=%p", t, t->nextTable());

    moveBucket(t->nextTable(), bucket);
    return true;
}


/* The point of lockBucket is to maintain these 2 invariants:

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
lockBucket(Bucket& bucket)
{
    using namespace details;

    KeyAtom keyAtom;
    KeyAtom oldKeyAtom = bucket.keyAtom.load();
    do {
        if (isTombstone<MKey>(oldKeyAtom)) return;
        if (isMoving<MKey>(oldKeyAtom)) {
            keyAtom = oldKeyAtom;
            break;
        }

        if (isEmpty<MKey>(oldKeyAtom))
            keyAtom = setTombstone<MKey>(oldKeyAtom);
        else keyAtom = setMoving<MKey>(oldKeyAtom);

    } while(!bucket.keyAtom.compare_exchange_weak(oldKeyAtom, keyAtom));


    ValueAtom valueAtom;
    ValueAtom oldValueAtom = bucket.valueAtom.load();

    // Ensures that if either are tombstone then both will be.
    if (isTombstone<MKey>(keyAtom)) {
        bucket.valueAtom.store(setTombstone<MValue>(oldValueAtom));
        return;
    }

    do {
        if (isTombstone<MValue>(oldValueAtom)) return;
        if (isMoving<MValue>(oldValueAtom)) {
            valueAtom = oldValueAtom;
            break;
        }

        if (isEmpty<MValue>(oldValueAtom))
            valueAtom = setTombstone<MValue>(oldValueAtom);
        else valueAtom = setMoving<MValue>(oldValueAtom);

    } while (!bucket.valueAtom.compare_exchange_weak(oldValueAtom, valueAtom));

    /* If value is tombstoned then we don't propagate it to the key on purpose.

       The reason is that if we're moving a half-inserted bucket (key but no
       value) and we were to fully tombstone that bucket then the tombstoned key
       should be deleted. The problem is that there's simply no way to
       differentiate between a half-inserted bucket and a thread that beat us to
       the move in this function which means that we can't safely deleted the
       key.

       Instead we move the key to the new table and, in moveBucket, we leave the
       tombstoned value empty. The result of this is that when the interupted
       insert op is resumed, it will find its half inserted bucket and finish
       its insertion.
     */
}


/* This op is pretty tricky and I concentrated most of the doc in the function
   itself around the complicated parts of the algorithm. Also checkout the doc
   for lockBucket which describes 2 key ideas behind the algorithm: the isMoving
   state and the probing window.

   There's one key idea I should reiterate though. If we're moving a key then no
   operations on that key (insert, find, compareExchange, remove) can take place
   until that key is moved. See the lockBucket documentation for more details on
   why.

   \todo Should probably merge the probing portion of the lockBucket doc to this
         doc where it would make more sense.
 */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
moveBucket(Table* dest, Bucket& src)
{
    using namespace details;

    /* Ensures that any ops that affect this key will end up waiting until the
       move is finished. In the meantime, the thread will help out with the
       move. And by help out, I really mean: get in the way and generally make
       things more complicated. Such is the cost of having a lockfree algo.
     */
    lockBucket(src);


    KeyAtom srcKeyAtom = src.keyAtom.load();

    // If it was tombstone then someone else finished the move. bail.
    if (isTombstone<MKey>(srcKeyAtom)) return;
    locklessCheck(isMoving<MKey>(srcKeyAtom), log);

    srcKeyAtom = setValue<MKey>(srcKeyAtom);
    size_t hash = hashFn(KeyAtomizer::load(clearMarks<MKey>(srcKeyAtom)));


    /* Start Probing for a bucket to move to.

       Note that even though we're in a move, we may need to help out with
       another resize. Recursivity is fun!
    */

    size_t tombstones = 0;
    for (size_t i = 0; i < ProbeWindow; ++i) {
        size_t probeBucket = this->bucket(hash, i, dest->capacity);
        log(LogMap, "probe",
                "table=%p, capacity=%ld, bucket=%ld",
                dest, dest->capacity, probeBucket);
        Bucket& bucket = dest->buckets[probeBucket];
        if (doMoveBucket(dest, bucket)) continue;


        // 1. Lock a bucket for our move by setting the key.

        KeyAtom destKeyAtom = bucket.keyAtom.load();

        log(LogMap, "mov-1", "bucket=%ld, srcKey=%s, destKey=%s",
                probeBucket,
                fmtAtom<MKey>(srcKeyAtom).c_str(),
                fmtAtom<MKey>(destKeyAtom).c_str());

        if (isTombstone<MKey>(destKeyAtom)) {
            tombstones++;
            continue;
        }

        // Ongoing move, switch to move mode
        if (isMoving<MKey>(destKeyAtom)) {
            --i;
            continue;
        }

        if (!isEmpty<MKey>(destKeyAtom)) {
            /* We can just compare the atoms because srcKeyAtom points to the
               unique instance of that key in the table and the only other
               instance of that key can only come from the atom having been
               moved to this bucket by another thread.
             */
            if (srcKeyAtom != destKeyAtom) continue;
        }

        else if (!bucket.keyAtom.compare_exchange_weak(destKeyAtom, srcKeyAtom))
        {
            // Someone beat us; recheck the bucket to see if our key was
            // involved.
            --i;
            continue;
        }


        // 2. Complete the move by setting the value.

        ValueAtom srcValueAtom = src.valueAtom.load();

        /* If it was tombstoned then someone else finished the move. bail.

           Fun fact: It's possible that the bucket is half built and will remain
           like that after we bail. Turns out that this is perfectly acceptable
           because by definition this bucket is after the correctly moved bucket
           and since it's half-built, it doesn't officially exist.

           We could tombstone it but leaving like it is effectively equivalent.
           Better still, if our key gets removed and inserted again then this
           bucket will be ready for it. Otherwise it'll just get cleaned up in
           the next resize. No harm done.

           This actually comes in handy to avoid memory leaks when moving a
           half-inserted bucket.
         */
        if (isTombstone<MValue>(srcValueAtom)) return;
        locklessCheck(isMoving<MValue>(srcValueAtom), log);
        srcValueAtom = setValue<MValue>(srcValueAtom);


        ValueAtom destValueAtom = bucket.valueAtom.load();

        if (isEmpty<MValue>(destValueAtom))
            if (bucket.valueAtom.compare_exchange_strong(destValueAtom, srcValueAtom))
                destValueAtom = srcValueAtom;

        locklessCheck(!isEmpty<MValue>(destValueAtom), log);

        /* If it's a value then at least one thread succeeded in the CAS. Before
           we can continue, we first need to broadcast the fact that the move is
           completed.

           Note that if isMoving is true then the value was still set by
           somebody which is all we care about.
         */
        if (!isTombstone<MValue>(destValueAtom)) {
            src.keyAtom.store(setTombstone<MKey>(srcKeyAtom));
            src.valueAtom.store(setTombstone<MValue>(srcValueAtom));
            return;
        }


        // 3. Uh. Oh. The move failed and we don't know why!

        /* There's 2 reasons why the move may have found a tombstone in value:

           - Another thread completed the move and the key was then removed.
           - A resize op tombstoned our half-moved bucket.

           The second scenario is the simplest. Since the move is still ongoing
           then retrying is the correct way to resolve the dilema.

           The first scenario is trickier; we know that the src bucket has been
           tombstoned because the dest bucket was tombstoned. The only way for
           the key to have been removed is for one thread to have finished the
           move, tombstoned src's bucket and then went on to remove dest. In any
           case, the first thing the retry will do is read src's bucket and
           bail when it finds a tombstone.
         */
        moveBucket(dest, src);
        return;
    }

    // Nowhere to put the key; make some room and try again.
    doResize(dest, tombstones);
    locklessCheck(dest->nextTable(), log);
    moveBucket(dest->nextTable(), src);
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
        KeyAtom keyAtom,
        ValueAtom valueAtom,
        DeallocAtom dealloc)
{
    log(LogMap, "insert-0", "table=%p", t);

    using namespace details;

    size_t tombstones = 0;

    keyAtom = setValue<MKey>(keyAtom);
    valueAtom = setValue<MValue>(valueAtom);

    for (size_t i = 0; i < ProbeWindow; ++i) {
        size_t probeBucket = this->bucket(hash, i, t->capacity);
        log(LogMap, "probe",
                "table=%p, capacity=%ld, bucket=%ld",
                t, t->capacity, probeBucket);
        Bucket& bucket = t->buckets[probeBucket];
        if (doMoveBucket(t, bucket)) continue;

        // 1. Set the key.

        KeyAtom bucketKeyAtom = bucket.keyAtom.load();

        log(LogMap, "ins-1", "bucket=%ld, key=%s, ins=%s",
                probeBucket,
                fmtAtom<MKey>(bucketKeyAtom).c_str(),
                fmtAtom<MKey>(keyAtom).c_str());

        if (isTombstone<MKey>(bucketKeyAtom)) {
            tombstones++;
            continue;
        }

        // Ongoing move, switch to move mode
        if (isMoving<MKey>(bucketKeyAtom)) {
            --i;
            continue;
        }

        if (isValue<MKey>(bucketKeyAtom)) {
            if (key != KeyAtomizer::load(clearMarks<MKey>(bucketKeyAtom)))
                continue;
        }

        else {

            // Make sure that our key atom is still valid before we try to
            // insert it. This is required because our insert op could have been
            // interupted by a move op after it wrote its key. In this scenario,
            // the move op would have wiped the key and rewriting our atom
            // elsewhere would cause some trouble. Makes you wish you had a
            // GC...
            if (!(dealloc & DeallocKey)) {
                keyAtom = KeyAtomizer::alloc(key);
                dealloc = setDeallocFlag(dealloc, DeallocKey);
            }

            // If the key in the bucket is a match we can skip this step. The
            // idea is if we have 2+ concurrent inserts for the same key then
            // the winner is determined when writing the value.
            else if (!bucket.keyAtom.compare_exchange_weak(bucketKeyAtom, keyAtom))
            {
                // Someone beat us; recheck the bucket to see if our key wasn't
                // involved.
                --i;
                continue;
            }

            // If we inserted our key in the table then don't dealloc it. Even
            // if the call fails!
            else dealloc = clearDeallocFlag(dealloc, DeallocKey);

        }


        // 2. Set the value.

        ValueAtom bucketValueAtom = bucket.valueAtom.load();

        bool dbg_once = false;

        // This loop can only start over once and the case only executed
        // once. The first pass is to avoid a pointless cas (an
        // optimization). The second go around is to redo the same checks after
        // we fail.
        while (true) {

            log(LogMap, "ins-2", "bucket=%ld, value=%s, ins=%s",
                    probeBucket,
                    fmtAtom<MValue>(bucketValueAtom).c_str(),
                    fmtAtom<MValue>(valueAtom).c_str());

            // Beaten by a move or a delete operation. Retry the bucket.
            if (isMoving<MValue>(bucketValueAtom) ||
                    isTombstone<MValue>(bucketValueAtom))
            {
                --i;
                break;
            }

            // Another insert beat us to it.
            if (isValue<MValue>(bucketValueAtom)) {
                keyAtom = clearMarks<MKey>(keyAtom);
                valueAtom = clearMarks<MValue>(valueAtom);
                deallocAtomNow(dealloc, keyAtom, valueAtom);
                return false;
            }

            locklessCheck(!dbg_once, log);
            dbg_once = true;

            if (bucket.valueAtom.compare_exchange_strong(bucketValueAtom, valueAtom)) {
                DeallocAtom newFlag = clearDeallocFlag(dealloc, DeallocValue);
                keyAtom = clearMarks<MKey>(keyAtom);
                valueAtom = clearMarks<MValue>(valueAtom);
                deallocAtomNow(newFlag, keyAtom, valueAtom);
                return true;
            }
        }
    }

    log(LogMap, "ins-3", "tombs=%ld, t=%p", tombstones, t);

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    locklessCheck(t->nextTable(), log);
    return insertImpl(t->nextTable(), hash, key, keyAtom, valueAtom, dealloc);
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
    log(LogMap, "find-0", "table=%p", t);

    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        size_t probeBucket = this->bucket(hash, i, t->capacity);
        log(LogMap, "probe",
                "table=%p, capacity=%ld, bucket=%ld",
                t, t->capacity, probeBucket);
        Bucket& bucket = t->buckets[probeBucket];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Check the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        log(LogMap, "fnd-1", "bucket=%ld, key=%s, target=%s",
                probeBucket,
                fmtAtom<MKey>(keyAtom).c_str(),
                std::to_string(key).c_str());

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

        if (key != KeyAtomizer::load(clearMarks<MKey>(keyAtom)))
            continue;


        // 2. Check the value.

        ValueAtom valueAtom = bucket.valueAtom.load();

        log(LogMap, "fnd-2", "bucket=%ld, value=%s",
                probeBucket, fmtAtom<MKey>(valueAtom).c_str());

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

        return std::make_pair(
                true, ValueAtomizer::load(clearMarks<MValue>(valueAtom)));
    }

    log(LogMap, "fnd-3", "tomb=%ld, t=%p", tombstones, t);

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    locklessCheck(t->nextTable(), log);
    return findImpl(t->nextTable(), hash, key);
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
        ValueAtom desired)
{
    log(LogMap, "xch-0", "table=%p", t);

    using namespace details;

    size_t tombstones = 0;

    desired = setValue<MKey>(desired);

    for (size_t i = 0; i < ProbeWindow; ++i) {
        size_t probeBucket = this->bucket(hash, i, t->capacity);
        log(LogMap, "probe",
                "table=%p, capacity=%ld, bucket=%ld",
                t, t->capacity, probeBucket);
        Bucket& bucket = t->buckets[probeBucket];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Find the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        log(LogMap, "xch-1", "bucket=%ld, key=%s, target=%s",
                probeBucket,
                fmtAtom<MKey>(keyAtom).c_str(),
                std::to_string(key).c_str());

        if (isTombstone<MKey>(keyAtom)) {
            tombstones++;
            continue;
        }
        if (isMoving<MKey>(keyAtom)) {
            --i;
            continue;
        }
        if (isEmpty<MKey>(keyAtom)) {
            ValueAtomizer::dealloc(clearMarks<MValue>(desired));
            return false;
        }

        if (key != KeyAtomizer::load(clearMarks<MKey>(keyAtom)))
            continue;


        // 2. Replace the value.

        ValueAtom valueAtom = bucket.valueAtom.load();

        while (true) {

            log(LogMap, "xch-2",
                    "bucket=%ld, value=%s, expected=%s, desired=%s",
                    probeBucket,
                    fmtAtom<MValue>(valueAtom).c_str(),
                    std::to_string(expected).c_str(),
                    fmtAtom<MValue>(desired).c_str());

            // We may be in the middle of a move so try the bucket again.
            if (isTombstone<MValue>(valueAtom) || isMoving<MValue>(valueAtom)) {
                --i;
                break;
            }

            // Value not fully inserted yet; just bail.
            if (isEmpty<MValue>(valueAtom)) {
                ValueAtomizer::dealloc(clearMarks<MValue>(desired));
                return false;
            }

            // make the comparaison.
            Value bucketValue = ValueAtomizer::load(clearMarks<MValue>(valueAtom));
            if (expected != bucketValue) {
                ValueAtomizer::dealloc(desired);
                expected = bucketValue;
                return false;
            }

            // make the exchange.
            if (bucket.valueAtom.compare_exchange_weak(valueAtom, desired)) {
                // The value comes from the table so defer the dealloc.
                if (!IsAtomic<Value>::value) {
                    valueAtom = clearMarks<MValue>(valueAtom);
                    rcu.defer([=] { ValueAtomizer::dealloc(valueAtom); });
                }
                return true;
            }
        }
    }

    log(LogMap, "xch-3", "tomb=%ld, t=%p", tombstones, t);

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    locklessCheck(t->nextTable(), log);
    return compareExchangeImpl(t->nextTable(), hash, key, expected, desired);
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
    log(LogMap, "remove-0", "table=%p", t);

    using namespace details;

    size_t tombstones = 0;

    for (size_t i = 0; i < ProbeWindow; ++i) {
        size_t probeBucket = this->bucket(hash, i, t->capacity);
        log(LogMap, "probe",
                "table=%p, capacity=%ld, bucket=%ld",
                t, t->capacity, probeBucket);
        Bucket& bucket = t->buckets[probeBucket];
        if (doMoveBucket(t, bucket)) continue;


        // 1. Find the key

        KeyAtom keyAtom = bucket.keyAtom.load();

        log(LogMap, "rmv-1", "bucket=%ld, key=%s, target=%s",
                probeBucket,
                fmtAtom<MKey>(keyAtom).c_str(),
                std::to_string(key).c_str());

        if (isTombstone<MKey>(keyAtom)) {
            tombstones++;
            continue;
        }
        if (isMoving<MKey>(keyAtom)) {
            --i;
            continue;
        }
        if (isEmpty<MKey>(keyAtom)) return std::make_pair(false, Value());

        if (key != KeyAtomizer::load(clearMarks<MKey>(keyAtom)))
            continue;


        // 2. Check the value

        ValueAtom valueAtom = bucket.valueAtom.load();

        log(LogMap, "rmv-2", "bucket=%ld, value=%s",
                probeBucket, fmtAtom<MKey>(valueAtom).c_str());

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

        log(LogMap, "rmv-3", "bucket=%ld, newKey=%s",
                probeBucket, fmtAtom<MKey>(newKeyAtom).c_str());

        if (!bucket.keyAtom.compare_exchange_strong(keyAtom, newKeyAtom)) {
            --i;
            continue;
        }

        // Reload the value from the bucket in case there's a lagging replace
        // op. Also prevent any further replace op by tombstoning the value.
        ValueAtom newValueAtom = setTombstone<MValue>(valueAtom);

        log(LogMap, "rmv-4", "bucket=%ld, newValue=%s",
                probeBucket, fmtAtom<MKey>(newValueAtom).c_str());

        valueAtom = bucket.valueAtom.exchange(newValueAtom);

        // The atoms come from the table so defer the delete in case someone's
        // still reading them.
        keyAtom = clearMarks<MKey>(keyAtom);
        valueAtom = clearMarks<MValue>(valueAtom);
        deallocAtomDefer(DeallocBoth, keyAtom, valueAtom);

        return std::make_pair(true, ValueAtomizer::load(valueAtom));
    }

    log(LogMap, "rmv-5", "tomb=%ld, table=%p", tombstones, t);

    // The key is definetively not in this table, try the next.
    doResize(t, tombstones);
    locklessCheck(t->nextTable(), log);
    return removeImpl(t->nextTable(), hash, key);
}


/* Implements the resize policy for the table. */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
doResize(Table* t, size_t tombstones)
{
    if (t->isResizing()) return;

    if (tombstones >= details::TombstoneThreshold)
        resizeImpl(t, t->capacity, true);
    else resizeImpl(t, t->capacity * 2, false);
}


/* Pretty straight forward and no really funky logic involved here. */
template<
    typename Key, typename Value, typename Hash, typename MKey, typename MValue>
void
Map<Key, Value, Hash, MKey, MValue>::
resizeImpl(Table* start, size_t newCapacity, bool force)
{
    log(LogMap, "rsz-0", "start=%p, newCapacity=%ld, force=%d",
            start, newCapacity, force);

    using namespace details;

    // 1. Insert the new table in the chain.

    std::unique_ptr<Table, MallocDeleter> safeNewTable;
    std::atomic<Table*>* prev = start ? &start->next : &table;
    Table* prevTable = start ? start : nullptr;
    Table* curTable = prev->load();
    bool done = false;

    do {
        log(LogMap, "rsz-1",
                "prev=%p, prevTable=%p, curTable=%p, curCapacity=%ld, next=%p",
                prev, prevTable, curTable,
                curTable ? curTable->capacity : 0,
                curTable ? curTable->next.load() : 0);

        if (curTable) {
            if (newCapacity < curTable->capacity) return;
            if (newCapacity == curTable->capacity) {
                if (!force) return;
                // Looking do to a cleanup but someone else is already doing it.
                if (start == curTable && curTable->isResizing()) return;
            }

            prevTable = Table::clearMark(curTable);
            prev = &prevTable->next;
            curTable = prev->load();
            continue;
        }

        if (!safeNewTable)
            safeNewTable.reset(Table::alloc(newCapacity));

        done = prev->compare_exchange_weak(curTable, safeNewTable.get());
    } while(!done);

    Table* newTable = safeNewTable.release();

    log(LogMap, "rsz-2", "prev=%p, prevTable=%p, next=%p, new=%p",
            prev, prevTable, prev->load(), newTable);

    if (!prevTable) return;


    // 2. Exhaustively move all the elements of the old table to the new table.
    // Note that we'll receive help from the other threads as well.
    for (size_t i = 0; i < prevTable->capacity; ++i)
        moveBucket(newTable, prevTable->buckets[i]);


    // 3. Get rid of oldTable.

    Table* toRemove = prevTable;

  restart:
    prev = &table;
    curTable = prev->load();
    Table* oldTable = curTable;

    while(curTable) {
        curTable = Table::clearMark(curTable);

        log(LogMap, "rsz-3", "prev=%p, cur=%p, next=%p, target=%p",
                prev, curTable, curTable->next.load(), toRemove);

        if (curTable != toRemove) {
            Table* nextTable = curTable->next.load();

            // We can't modify the next pointer of a table that is marked so
            // keep the last unmarked prev pointer.
            if (!Table::isMarked(nextTable)) {
                prev = &curTable->next;
                oldTable = nextTable;
            }
            curTable = Table::clearMark(nextTable);
            continue;
        }

        // Make sure nobody tries to modify our next pointer.
        Table* nextTable = curTable->mark();
        locklessCheck(!Table::isMarked(nextTable), log);

        // Remove our table from the list.
        if (prev->compare_exchange_strong(oldTable, nextTable)) break;

        /* If the cas failed then it's either because prev was marked or because
           a later table managed to finish before us and replaced our prev.
           There also seems to be a third possibility which I can't quite figure
           out.

           Because of the third possibility, we need to play it safe and always
           restart.
         */
        goto restart;
    }

    log(LogMap, "defer", "table=%p, prev=%p, next=%p",
            toRemove, prev, prev->load());

    rcu.defer([=] {
                this->log(LogMap, "free", "table=%p", toRemove);
                std::free(toRemove);
            });
}

} // lockless
