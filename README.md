lockless
========

Collection of random lockfree doodads that exist for the sole purpose of
amusing/frustrating me. Current doodads worthy of notice:

- Log: Lockfree logger meant to act a bit like printk: can be used anywhere
  without having to worry about performance side-effects.

- Check: Assertion library that meshes with Log to dump detailed historical
  information to aid debugging. Note that the Check and Log combo is
  surprisingly effective for debugging lock-free algorithms and data-structures
  which is why you'll see most classes in this library present a log object in
  its interface.

- Rcu: Light implementation that uses no static members or extra
  threads. Currently, this version doesn't scale too well and I'm hoping to use
  snzi to speed it up a bit.

- GlobalRcu: Heavier but more scalable implementation that makes use of static
  thread local storage for each epoch along with an side thread for the defered
  work. Currently the extra gc thread is a bit annoying and I'm hoping to get
  rid of it while keeping the overhead low using the mindicator primitive.

- Linked List: Flexible singly-linked list which mostly exists to simplify the
  lock-free removal of nodes. Note that this should eventually be upgraded to a
  doubly-linked list.

- Queue: Bounded queues that supports all combination of SR/SW MR/MW
  usage. There's also an unbounded queue which uses RCU and sentinel nodes to be
  throughly slower then the bounded version. Hooray!

- Map (WIP): open addressing linear probing hash table that steals a few bits
  from the key and value to manage a state machine for each bucket. Uses
  chaining resizes and a probe window which avoids linear worst-case probe time
  by aggressively resizing instead. Whether this is a good idea is up for debate
  but I think you can prove with high probability that the resizes can be
  amortized to keep a constant worst case cost. Currently this thing is giving
  me a headache that won't go away.

- Locks: Ironic considering the name of the project but fun to write
  none-the-less. The selection is: unfair spin-lock, fair (ticket) spin-lock,
  fair (ticket) rw spin-lock and an oddity named seq-lock. Still trying to
  figure out how to do a fair performance comparaison of these implementations.

- Alloc: Two wait-free memory allocators in the form of a simple arena allocator
  and a the more complicated block allocator. Note that, while I'm having a
  harding time comparing these to tcmalloc, they both seem to be generally
  slower (arena allocator is actually terrible in MT scenarios without any TLS
  support).

- Misc: TLS classes, timing, MT testing, performance testing.

Small note, some of these algorithms contains some pretty glaring performance
flaws (eg. both epochs in Rcu are stored on the same cache line). I'm
intentionally leaving these as is so that I can mesure the effect of fixing them
at a later undisclosed point in time.

How to Build
----------

First off, get these dependencies from your local software distribution center:

* cmake 2.6+
* gcc 4.7+
* gperftools' tcmalloc (optional but highly recomended).

Next, enter these two magical commands:

    cmake CMakeLists.txt
    make all test

That's it.
