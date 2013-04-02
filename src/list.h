/* list.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Linked list which allows lock-free deletions.

   This class is intentionally left wide-open to allow maximum flexibility in
   the usage of the class. This has the side effect that a lot of internal
   implementation details will leak in the interface. For example, the user is
   responsible for properly maintaining the mark's invariants and failure to do
   so will break the popMarked() and remove() ops.

   Memory management is entirely the responsability of the user of the class.

*/

#ifndef __lockless__list_h__
#define __lockless__list_h__

#include <atomic>

namespace lockless {

template<typename T> struct List;

/******************************************************************************/
/* LIST NODE                                                                  */
/******************************************************************************/

template<typename T>
struct ListNode
{
    typedef ListNode<T> Node;

    template<typename Value>
    ListNode(Value&& value) :
        data(std::forward<Value>(value)),
        rawNext(nullptr)
    {}

    operator T& () const
    {
        return data;
    }

    bool isMarked() const { return rawNext & 1; }
    Node* mark()
    {
        Node* oldNext = rawNext;
        while (!rawNext.compare_exchange_weak(oldNext, oldNext | 1));
        return oldNext & ~1;
    }

    void reset() { rawNext = nullptr; }

    Node* next() const { return rawNext & ~1; }
    void next(Node* node)
    {
        assert(!isMarked()); // Protects the invariant.
        rawNext = node;
    }

    bool compare_exchange_next(Node*& expected, Node* newNext)
    {
        assert(expected & ~1 == expected); // Protects the invariant.
        return rawNext.compare_exchange_strong(expected, newNext);
    }

    T value;

private:

    // It's a bad idea to expose this because the pointer could be marked and
    // dereferencing that will cause all sorts of problem.
    std::atomic<Node*> rawNext;

    // Allows access to rawNext because it simplifies the remove algo. Note that
    // this may not be the only function that uses the remove's access pattern
    // in which case we should expose this.
    friend struct List<T>;

};


/******************************************************************************/
/* LIST                                                                       */
/******************************************************************************/

template<typename T>
struct List
{
    typedef ListNode<T> Node;

    void push(Node* node)
    {
        locklessCheckEq(node & ~1, node, log);

        Node* next = head;
        do {
            node->next(next);
        } while (head.compare_exchange_weak(next, node));
    }

    Node* pop()
    {
        Node* node = head;
        do {
            if (!node) return nullptr;
        } while(!head.compare_exchange_weak(node, node->next()));

        node->reset();
        return node;
    }

    Node* popMarked()
    {
        Node* node = head;
        do {
            if (!node || !node->isMarked()) return nullptr;
        } while(!head.compare_exchange_weak(node, node->next()));

        node->reset();
        return next;

    }

    /** O(n) mechanism for removing an arbitraty node from the tree. This op
        relies on the node's mark and it's associated invariants to work
        properly. It's completely thread-safe and lock-free assuming that all
        other ops respect the mark's invariant and the are also lock-free.

     */
    bool remove(Node* toRemove)
    {
      restart:
        std::atomic<Node*>* prev = &head;
        Node* node = *prev;

        while(true) {
            if (!node) return false;

            if (node != toRemove) {

                // Since we can't change a marked node, prev should point to the
                // first node before our node that is not marked.
                if (!node->isMarked())
                    prev = &node->rawNext;

                node = node->next();
                continue;
            }

            // After marking the node, no other op will be able to change node's
            // next pointer.
            Node* oldNext = node->mark();

            // Linearilization point for two threads trying to remove the same
            // node is the first node to complete this call.
            if (!prev->compare_exchange_next(node, oldNext))
                goto restart;

            toRemove->reset();
            return true;
        }
    }

    std::atomic<Node*> head;

    DebuggingLog<10240, DebugList>::type log;
};

} // lockless

#endif // __lockless__list_h__
