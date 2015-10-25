// Public entrypoints for a memory heap implementation.
// (c) The Great Class of 2015
#ifndef HEAP_H
#define HEAP_H

// The public entry points.  
// These all follow the functionality of the common h-free counterparts.
extern void *hmalloc(int);	   // allocate bytes (see malloc(3))
extern void *hcalloc(int,int);	   // allocate zeroed bytes (ditto)
extern void *hrealloc(void*,int);  // re-allocate bytes (ditto)
extern void  hfree(void *);	   // free bytes (ditto)
extern char *hstrdup(char *);	   // string duplication (see strdup(3))
#endif
