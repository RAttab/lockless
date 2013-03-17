lockless
========

Collection of random lockfree doodads that exist for the sole purpose of
amusing/frustrating me.

Current doodads worthy of notice:

- Log: Lockfree logger meant to act a bit like printk: can be used anywhere
  without having to worry about performance side-effects.

- Check: Assertion library that meshes with Log to dump detailed historical
  information to aid debugging.

- Rcu: the implementation is pretty light and uses no static members.  I also
  think the defering mechanism is pretty clever but not very obvious.

- Queue: Unbounded wait-free queue which uses RCU and sentinel nodes to be
  throughly slower then a bounded queue. Hooray!

- Map (WIP): open addressing linear probing hash table that steals a few bits
  from the key and value to manage a state machine for each bucket. Uses
  chaining resizes and a probe window which avoids linear worst-case probe time
  by aggressively resizing instead (Whether this is a good idea is up for
  debate). Currently this thing is giving me a headache that won't go away.

Small note, some of these algorithms contains some pretty glaring performance
flaws (eg. both epochs in Rcu are stored on the same cache line). I'm
intentionally leaving these as is so that I can mesure their effects at a later
undisclosed point in time.
