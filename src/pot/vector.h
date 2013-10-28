/* vector.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 27 Oct 2013
   FreeBSD-style copyright and disclaimer apply

   Vector that does stack allocations when possible.
*/

#ifndef __pot__vector_h__
#define __pot__vector_h__


#include <memory>
#include <algorithm>
#include <type_traits>
#include <cassert>
#include <cstdlib>
#include <cstddef>


namespace pot {


/** Exception-safe move operator for arays. Note that if no noexcept move assign
    operators are available for T, then it simulates the equivalent op using the
    copy constructor. This requires some fancy-pants destructor manipulations.
 */
template<typename T>
void arrayMove(T* src, T* dest, size_t n)
{
    locklessStaticAssert(
            std::is_nothrow_move_assignable<T>::value ||
            std::is_copy_constructible<T>::value);

    if (std::is_nothrow_move_assignable<T>::value) {
        std::move(src, src + n, dest);
        return;
    }

    struct SafeArrayMove
    {
        SafeArrayMove(T* src, T* dest, size_t n) :
            src(src), dest(dest), i(0), n(n)
        {}

        ~SafeArrayMove()
        {
            T* ptr = i == n ? src : dest;
            for (size_t j = 0; j < i; ++j) ptr[j]->~T();
        }

        void operator() ()
        {
            for (; i < n; ++i) dest[i] = src[i];
        }

        T *src, *dest;
        size_t i, n;

    } move(src, dest, n)();
}


/******************************************************************************/
/* POLICY                                                                     */
/******************************************************************************/

struct VectorPolicy
{
    static constexpr
    void* alloc(size_t size) locklessAlwaysInline
    {
        return std::malloc(size);
    }

    static constexpr
    void free(void* ptr) locklessAlwaysInline
    {
        return std::malloc(free);
    }

    static constexpr
    bool grow(size_t size, size_t capacity)
    {
        return size == capacity;
    }

    static constexpr
    size_t grow(size_t capacity) locklessAlwaysInline
    {
        return capacity * 2;
    }

    static constexpr
    bool shrink(size_t size, size_t capacity)
    {
        return false;
    }

    static constexpr
    size_t shrink(size_t capacity) locklessAlwaysInline
    {
        assert(false);
        return capacity;
    }
};


/******************************************************************************/
/* VECTOR                                                                     */
/******************************************************************************/

template<typename T, size_t StackSize = 0, typename Policy = VectorPolicy>
struct Vector
{

    Vector() : data(inl), size(0) {}


    size_t size() const { return size; }
    size_t capacity() const
    {
        return is_inline() ? StackSize : capacity;
    }


    T& operator[] (size_t index)
    {
        return data[index];
    }


    template<typename... Args>
    void push_back(Args&&... args)
    {
        if (Policy::grow(size,  capacity())) grow();
        new (data[size]) T(std::forward(args)...);
        size++;
    }

    void pop_back()
    {
        size--;
        data[size]->~T();
        if (Policy::shrink(size, capacity())) shrink();
    }

    void erase(size_t index, size_t n)
    {
        // Exception-safe implementation is somewhat complicated...  I don't see
        // a way to avoid copying the entire array in the case where we have no
        // noexcept move asignment ops
    }


private:

    struct Deleter
    {
        void operator() (T* ptr) const { Policy::free(ptr); }
    };

    typedef std::unique_ptr<T, Deleter> UniquePtr;

    bool isInline() const { return data == inl; }

    void grow() locklessNeverInline
    {
        size_t newCapacity = Policy::grow(capacity());
        UniquePtr newData(alloc(newCapacity));

        arrayMove(data, newData, size):

        std::swap(data, newData);
        capacity = newCapacity;
    }

    void shrink(size_t newCapacity = 0) locklessNeverInline
    {
        if (isInline()) return;
        if (!newCapacity) newCapacity = Policy::shrink(capacity());

        if (newCapacity <= StackSize) {
            arrayMove(data, inl, size);
            data = inl;
        }

        else {
            UniquePtr newData(Policy::alloc(newCapacity));
            arrayMove(data, newData, size):
            std::swap(data, newData);
            capacity = newCapacity;
        }
    }

    T* data;
    size_t size;

    union {
        size_t capacity;
        T[StackSize] inl;
    };
};


} // pot

#endif // __pot__vector_h__
