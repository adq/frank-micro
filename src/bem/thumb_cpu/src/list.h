/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_LIST_H
#define B_EM_PICO_LIST_H
#include <pico.h>

struct list_element {
    struct list_element *next;
};
        
inline static void list_prepend(struct list_element **phead, struct list_element *e)
{
    assert(e->next == NULL);
    assert(e != *phead);
    e->next = *phead;
    *phead = e;
}

inline static void list_prepend_all(struct list_element **phead, struct list_element *to_prepend)
{
    struct list_element *e = to_prepend;

    // todo should this be assumed?
    if (e)
    {
        while (e->next)
        {
            e = e->next;
        }

        e->next = *phead;
        *phead = to_prepend;
    }
}

inline static struct list_element *list_remove_head(struct list_element **phead)
{
    struct list_element *e = *phead;

    if (e)
    {
        *phead = e->next;
        e->next = NULL;
    }

    return e;
}

inline static struct list_element *list_remove_head_ascending(struct list_element **phead, struct list_element **ptail)
{
    struct list_element *e = *phead;

    if (e)
    {
        *phead = e->next;

        if (!e->next)
        {
            assert(*ptail == e);
            *ptail = NULL;
        }
        else
        {
            e->next = NULL;
        }
    }

    return e;
}

inline static void list_insert_after(struct list_element *prev, struct list_element *e)
{
    e->next = prev->next;
    prev->next = e;
}

inline static bool list_remove(struct list_element **phead, struct list_element *e)
{
    if (!*phead) return false;

    struct list_element *prev = *phead;

    bool found = true;
    if (prev == e)
    {
        *phead = e->next;
    }
    else
    {
        while (prev->next && prev->next != e)
        {
            prev = prev->next;
        }

        if (prev->next) {
            assert(prev->next == e);
            prev->next = e->next;
        } else {
            found = false;
        }
    }

    e->next = NULL;
    return found;
}

#endif //B_EM_LIST_H
