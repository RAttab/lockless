lockless
========

Collection of random lockfree doodads that exist for the sole purpose of
amusing/frustrating me. Current doodads worthy of notice:

- Log: Lockfree logger meant to act a bit like printk: can be used anywhere
  without having to worry about performance side-effects.

- Check: Assertion library that meshes with Log to dump detailed historical
  information to aid debugging.

- Rcu: Light implementation that uses no static members or extra threads.

- GlobalRcu: Heavier but more scalable implementation that makes use of static
  thread local storage for each epoch along with an side thread for the defered
  work.

- Queue: Unbounded wait-free queue which uses RCU and sentinel nodes to be
  throughly slower then a bounded queue. Hooray!

- Map (WIP): open addressing linear probing hash table that steals a few bits
  from the key and value to manage a state machine for each bucket. Uses
  chaining resizes and a probe window which avoids linear worst-case probe time
  by aggressively resizing instead. Whether this is a good idea is up for debate
  but I think you can prove with high probability that the resizes can be
  amortized to keep a constant worst case cost. Currently this thing is giving
  me a headache that won't go away.

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
