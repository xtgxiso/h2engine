#ifndef _SHARED_PTR_H_
#define _SHARED_PTR_H_

#include "atomic_op.h"
#include "base/lock.h"
#include <assert.h>
#include <stdexcept>


namespace ff {
#define FF_ASSERT(X) if (!(X)) throw std::runtime_error("SharedPtr NULL DATA")

struct RefData
{
    RefCount      m_ref_count;
    RefCount      m_weak_ref_count;
    SpinLock      m_mutex;
};

template<typename T>
class SharedPtr
{
public:
    typedef T                         object_t;
    typedef SharedPtr<T>           self_type_t;
public:
    SharedPtr();
    SharedPtr(object_t* p);
    SharedPtr(const self_type_t& p);
    template<typename R>
    SharedPtr(const SharedPtr<R>& p):
        m_dest_ptr(p.get()),
        m_ref_data(p.ger_ref_count())
    {
        if (NULL != m_dest_ptr)
        {
            m_ref_data->m_ref_count.inc();
            m_ref_data->m_weak_ref_count.inc();
        }
    }
    SharedPtr(object_t* p, RefData* data_):
        m_dest_ptr(p),
        m_ref_data(data_)
    {
        if (NULL != m_dest_ptr)
        {
            //!外部已经累加过，无需重复累加
            m_ref_data->m_weak_ref_count.inc();
        }
    }

    virtual ~SharedPtr();

    RefData* ger_ref_count()  const  { return m_ref_data; }
    size_t       ref_count() const       { return m_ref_data != NULL? (size_t)m_ref_data->m_ref_count.value(): 0; }
    object_t*    get() const             { return m_dest_ptr; }
    void         reset();
    object_t&    operator*();
    object_t*    operator->();
    bool         operator==(const self_type_t& p);
    bool         operator==(const object_t* p);
    self_type_t& operator=(const self_type_t& src_);
    
    operator bool() const
    {
        return NULL != m_dest_ptr;
    }

private:
    object_t*         m_dest_ptr;
    RefData*       m_ref_data;
};

template<typename T>
SharedPtr<T>::SharedPtr():
    m_dest_ptr(NULL),
    m_ref_data(NULL)
{
}

template<typename T>
SharedPtr<T>::SharedPtr(object_t* p):
    m_dest_ptr(p),
    m_ref_data(NULL)
{
    if (NULL != m_dest_ptr)
    {
        m_ref_data = new RefData();
        m_ref_data->m_ref_count.inc();
        m_ref_data->m_weak_ref_count.inc();
    }
}

template<typename T>
SharedPtr<T>::SharedPtr(const self_type_t& p):
    m_dest_ptr(p.get()),
    m_ref_data(p.ger_ref_count())
{
    if (NULL != m_dest_ptr)
    {
        m_ref_data->m_ref_count.inc();
        m_ref_data->m_weak_ref_count.inc();
    }
}

template<typename T>
SharedPtr<T>::~SharedPtr()
{
    reset();
}

template<typename T>
void SharedPtr<T>::reset()
{
    if (m_dest_ptr)
    {
        if (true == m_ref_data->m_ref_count.dec_and_check_zero())
        {
            delete m_dest_ptr;
            m_dest_ptr = NULL;
        }
        if (true == m_ref_data->m_weak_ref_count.dec_and_check_zero())
        {
            delete m_ref_data;
            m_ref_data = NULL;
        }
    }
}

template<typename T>
typename SharedPtr<T>::object_t&    SharedPtr<T>::operator*()
{
    FF_ASSERT(NULL != m_dest_ptr);
    return *m_dest_ptr;
}

template<typename T>
typename SharedPtr<T>::object_t*    SharedPtr<T>::operator->()
{
    FF_ASSERT(NULL != m_dest_ptr);
    return m_dest_ptr;
}

template<typename T>
bool SharedPtr<T>::operator==(const self_type_t& p)
{
    return m_dest_ptr == p.get();
}

template<typename T>
bool SharedPtr<T>::operator==(const object_t* p)
{
    return m_dest_ptr == p;
}

template<typename T>
typename SharedPtr<T>::self_type_t& SharedPtr<T>::operator=(const self_type_t& src_)
{
    if (&src_ == this)
    {
        return *this;
    }
    reset();
    m_dest_ptr = src_.get();
    m_ref_data = src_.ger_ref_count();
    if (NULL != m_dest_ptr)
    {
        m_ref_data->m_ref_count.inc();
        m_ref_data->m_weak_ref_count.inc();
    }
    return *this;
}


template<typename T>
class WeakPtr
{
public:
    typedef T                         object_t;
    typedef WeakPtr<T>             self_type_t;
    typedef SharedPtr<T>           shared_type_t;
public:
    WeakPtr():
        m_dest_ptr(NULL),
        m_ref_data(NULL)
    {
    }
    template<typename R>
    WeakPtr(const SharedPtr<R>& p):
        m_dest_ptr(p.get()),
        m_ref_data(p.ger_ref_count())
    {
        if (NULL != m_ref_data)
        {
            m_ref_data->m_weak_ref_count.inc();
        }
    }
    WeakPtr(const self_type_t& p):
        m_dest_ptr(p.get()),
        m_ref_data(p.ger_ref_count())
    {
        if (NULL != m_ref_data)
        {
            m_ref_data->m_weak_ref_count.inc();
        }
    }

    virtual ~WeakPtr()
    {
        reset();
    }

    RefData* ger_ref_count()  const  { return m_ref_data; }
    object_t* get() const { return m_dest_ptr; }
    shared_type_t lock()
    {
        if (m_dest_ptr == NULL || m_ref_data == NULL)
        {
            return shared_type_t();
        }
        {
            spin_LockGuard lock(m_ref_data->m_mutex);
            if (1 == m_ref_data->m_ref_count.inc_and_fetch(1))
            {
                //! 数据已经被销毁
                m_ref_data->m_ref_count.dec_and_check_zero();
                m_dest_ptr = NULL;
                return shared_type_t();
            }
        }
        //! 构造新的sharedptr
        return shared_type_t(m_dest_ptr, m_ref_data);
    }
    void reset()
    {
        if (m_ref_data)
        {
            if (true == m_ref_data->m_weak_ref_count.dec_and_check_zero())
            {
                delete m_ref_data;
                m_ref_data = NULL;
            }
        }
        m_dest_ptr = NULL;
    }
    
    template<typename R>
    self_type_t& operator=(const SharedPtr<R>& p)
    {
        reset();
        m_dest_ptr = p.get();
        m_ref_data = p.ger_ref_count();
        if (NULL != m_ref_data)
        {
            m_ref_data->m_weak_ref_count.inc();
        }
        return *this;
    }
    self_type_t& operator=(const self_type_t& p)
    {
        if (&p == this)
        {
            return *this;
        }
        reset();
        m_dest_ptr = p.get();
        m_ref_data = p.ger_ref_count();
        if (NULL != m_ref_data)
        {
            m_ref_data->m_weak_ref_count.inc();
        }
        return *this;
    }
    operator bool() const
    {
        return bool(lock());
    }

private:
    object_t*         m_dest_ptr;
    RefData*       m_ref_data;
};
}

#endif
