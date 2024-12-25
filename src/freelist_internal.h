#ifndef FREELIST_INTERNAL_H
#define FREELIST_INTERNAL_H

/**
 * @brief A freelist to speed up allocation of objects of the same type.
 * 
 * This structure simplifies allocations and freeing objects of the same type,
 * by keeping a list of free objects. This is useful when you have a lot of
 * objects of the same type that are allocated and freed frequently, and
 * you want to avoid the overhead of malloc/free, while keeping a bit
 * of memory committed to the objects.
 */
typedef struct freelist_s freelist_t;

/**
 * @brief Create a new freelist
 * 
 * This function creates a new freelist. The freelist will allocate
 * memory for the freelist structure and the array of pointers to the
 * objects. The objects themselves are not allocated yet.
 * 
 * It is suggested to use this function lazily to create the freelist
 * when it is needed.
 * 
 * @param elem_size     Size of each element in the freelist
 * @param max_elem      Maximum number of elements in the freelist
 * @return freelist_t*  Pointer to the freelist, or NULL on error
 */
freelist_t *freelist_create(int elem_size, int max_elem);

/**
 * @brief Allocate an object from the freelist
 * 
 * This function allocates an object from the freelist. If there are no
 * free objects in the freelist, it will allocate a new object using
 * malloc.
 * 
 * @param list          Freelist to allocate from
 * @return void*        Pointer to the allocated object, or NULL on error
 */
void* freelist_alloc(freelist_t *list);

/**
 * @brief Free an object back to the freelist
 * 
 * This function frees an object back to the freelist. If the freelist
 * is full, the object will be freed using free.
 * 
 * @param list          Freelist to free to
 * @param elem          Pointer to the object to free
 */
void freelist_free(freelist_t *list, void *elem);

/**
 * @brief Flush the freelist
 * 
 * This function frees all objects in the freelist, but keeps the
 * freelist ready for further usage.
 * 
 * @param list          Freelist to flush
 */
void freelist_flush(freelist_t *list);

/**
 * @brief Destroy the freelist
 * 
 * This function destroys the freelist, freeing all objects in the
 * freelist and the freelist itself.
 * 
 * @param list          Freelist to destroy
 */
void freelist_destroy(freelist_t *list);


#endif
