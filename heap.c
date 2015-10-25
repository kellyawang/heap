// Implementation of heaps.
// (c) The Great Class of 2015, especially <Kelly Wang>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "heap.h"

/*
 * Extensions: grow now returns the chunk that it allocates
 * Included a check to make sure we don't try to free an already free chunk
 *
 */

// info: describes size/flags associated with a chunk. 0=>dummy
typedef int info;

/* chunk: basic unit of allocation.
 * User gets payload when chunk is allocated.
 * When not allocated, payload stores double links into a list of free chunks.
 */
typedef struct chunk chunk;
struct __attribute__((__packed__)) chunk {
  info header;
  // payload starts here:
  chunk *prev;
  chunk *next;
  // :end of payload
  // info footer;
};

/*
 * Sizes of things.
 * This code works on systems where H_PS is 8, and H_IS is 4.
 * It will not port if H_IS*2 != H_PS without some redesign.
 */
#define H_PS            (sizeof(void*)) //pointer
#define H_IS            (sizeof(info))  //info tag (size of chunk + flags)
#define H_MINPAYLOAD	(2*H_PS)
#define H_MINCHUNK	(H_MINPAYLOAD+2*H_IS)

/*
 * information described in headers and footers:
 * size (always a multiple of 8), with low 3 bits representing up to 3 flags
 */
#define H_FREE 0x1
// others would be declared as 0x2 and 0x4

/*
 * Sizes will always be a multiple of 8.
 * Therefore, the bottom three bits are useful as flags.
 * Use this mask to determine the number of bytes in the associated chunk.
 */
#define H_SIZEMASK (~0x7)

/*
 * Pointer manipulation macros.
 * It is assumed that all pointers in this code are void* and cast when dereferenced.
 */
#define PTR_ADD(p,i) (((char*)p)+(i))
#define PTR_DIFF(a,b) (((char*)a)-(char*)b)

/*
 * Global state.
 * BASE...HWM is the range of space alloced for heap use.  
 * (May not be true if others call sbrk.)
 * PAGE_SIZE is useful with predicting the best values for sbrk.
 * FreeList
 */
static void *BASE = 0;     // pointer to first byte allocated
static void *HWM = 0;      // high water mark; first byte not allocated
static int PAGE_SIZE = 0;   // the default page size (likely 4096)
static chunk FreeList[1];  // the free list

/*
 * A quick macro that is turned on if you set DEBUG environment variable
 */
static int debug = 0;      // by default, don't debug
#define debugPrint if (debug) printf

/**
 * FORWARD PRIVATE METHOD DECLARATIONS.
 * STUDENTS: please write hmalloc, hfree, and all methods marked with <== below
 **/
static void   init(void);
static chunk   *grow(int, chunk *);               // 

static info  *ck_footerAddr(chunk *);
static void   ck_setInfo(chunk *, info);
static chunk *ck_split(chunk *c, int paysize);	// 
static void   ck_merge(chunk *c1, chunk *c2);	// 
static int    ck_size(chunk *c);
static int    ck_payloadSize(chunk *c);

static void   fl_insert(chunk *, chunk *);	// 
static void   fl_remove(chunk *);	        // 
static int    fl_size(chunk *);
static chunk *fl_findBestFit(chunk *, int);	// 

static void   ck_print(chunk *c);
static void   fl_print(void);
static void   hprint(void);
/*
 * init(void).
 * This function must be called to initialize the system.
 * It should:
 *   * set up the FreeList dummy node representing "no free chunks"
 *   * capture the PAGE_SIZE of the system
 *   * initialize the HWM and BASE pointer to point to the "program break"
 * Set in this way, the next allocation will trigger a grow => sbrk.
 */
void init(void)
{
  if (BASE != 0) return;
  debug = 0 != getenv("DEBUG"); // see getenv(3)

  // set up dummy node in free list
  FreeList->header = 0;      // 0 => DUMMY
  FreeList->prev = FreeList;
  FreeList->next = FreeList;
  
  // HWM-BASE is total space allocated
  HWM = BASE = sbrk(0); 
  PAGE_SIZE = getpagesize();
}

/*
 * grow(delta,FreeList).
 * Allocate one or more pages (at least delta bytes) and add chunk to FreeList.
 */
chunk *grow (int delta, chunk *l)
// pre: delta > H_MINCHUNK
// post: space is allocated, encapsulated by a chunk, added to freelist l
//       HWM is updated to reflect extent of new allocation
{
  init(); 
  delta = delta + 4*H_IS; //bring payload size up to chunk size from hmalloc
  delta = (delta + PAGE_SIZE-1)/PAGE_SIZE*PAGE_SIZE; 
  chunk *c = sbrk(delta); //c now points to prev program break

  HWM = sbrk(0); //returns end of the allocated space; new program break
  
  *(info*)c = 0; //thing c points to (dereferenced info pointer) is 0 - initialize top segment boundary
  info* end = (info*)PTR_ADD(HWM, -H_IS); //subtract size of an info from HWM 
  *end = 0; //initialize bottom segment boundary

  c = (chunk*)PTR_ADD(c, H_IS); //increment c to point at where the chunk will start

  int size = PTR_DIFF(end, c); 
  ck_setInfo(c, size|H_FREE);//set free bit 
  
  fl_insert(l, c);
  
  return c;
  
}

/*
 * ck_size(c).
 * Return the size of chunk c in bytes.
 */
int ck_size(chunk *c)
// pre: c is a valid chunk, possibly free
// post: size of chunk is returned (for payload size see ck_payload)
{
  return c->header&H_SIZEMASK;
}

/*
 * ck_payloadSize(c).
 * return the payload size of c.
 */
int ck_payloadSize(chunk *c)
// pre: c is a valid chunk, possibly free
// post: returns the size of c's payload, in bytes (see ck_size for chunk size)
{
  return ck_size(c)-2*H_IS;
}

/*
 * ck_footerAddr(c).
 * Generate a pointer to the footer info field at the end of chunk c.
 */
info *ck_footerAddr(chunk *c)
// pre: c is a pointer to a valid chunk
// post: returns a pointer to footer
{
  int size = c->header & H_SIZEMASK; // clear non-size flags  
  return (info*)PTR_ADD(c,size-H_IS);
}

/*
 * ck_setInfo(c,i).
 * Set the header and footer info fields for this chunk to i.
 * The location of the footer depends on size which is computed from i.
 */
void ck_setInfo(chunk *c, info i)
// pre: c is a pointer to a chunk, possibly not initialize
// post: set header and footer to i
{
  c->header = i;
  info *foot = ck_footerAddr(c);
  *foot = i;
}

/*
 * ck_split(c,paySize)
 * Split a chunk, c, into two pieces: c and the return value, rest.
 */
chunk *ck_split(chunk *c, int paysize)
// pre: c is a chunk (not marked free), paysize is the desired payload size
// post: c is trimmed appropriately and the remainder is returned as another chunk
//       if chunk can't be split, 0 is returned.
{
  printf("ck_split entered!\n");

  //chunk can only be split if paysize big enough; this is an extra check even though hmalloc already takes care of this
  if (paysize >= H_MINPAYLOAD) {
    int CHUNK_SIZE = ck_size(c); //we'll need this later
    paysize = (paysize + H_PS-1)/H_PS*H_PS; //round paysize up to multiple of 8
    info size_c = paysize + 2*H_IS; //reset c's payload size
    
    info size_d = CHUNK_SIZE - size_c;
    
    //make sure remainder would be a big enough chunk
    if (size_d < H_MINCHUNK) {
      return 0;
    } 
    
    //trim c's payload by creating one chunk of size paysize + header and footer
    ck_setInfo(c, size_c); 

    //find where c ends
    info *end_c = ck_footerAddr(c);
    chunk *d = (chunk*)PTR_ADD(end_c, H_IS); //new chunk d starts where c ended
    ck_setInfo(d, size_d|H_FREE); 
	       
    //insert it into the free list
    fl_insert(FreeList, d);
    
    return d;

  } else { //chunk can't be split; return 0
    return 0;
  }

}

/*
 * ck_merge(c1,c2).
 * Merge neighboring free chunks together.
 * c1 and c2 are removed from the free list, merged into a single chunk
 * accessible by c1 and added back into the free list.
 */
void ck_merge(chunk *c1, chunk *c2)
// pre: c1 and c2 are adjacent free chunks with c1 < c2
// post: c2 is merged into c1 and the entire chunk is classified as free
{
  fl_remove(c1);
  fl_remove(c2);
  
  chunk *sum = c1;
  //merge c1 with c2; take sum of sizes
  info totalSize = ck_size(c1) + ck_size(c2);
  ck_setInfo(sum, totalSize|H_FREE);
  
  fl_insert(sum, FreeList);

}


/**
 * Free List methods.
 **/
/*
 * fl_insert(l,c).
 * Insert chunk c into FreeList, l.
 * Different approaches to managing FreeList lead to different performance.
 */
void fl_insert(chunk *l, chunk *c)
// pre: c not in l
// post: c is added (to head) of l
{
  c->prev = l; //connect c's pointers
  c->next = l->next;
  l->next->prev = c; //chunk after l
  l->next = c; //connect l's pointer
}

/*
 * fl_remove(c).
 * Remove chunk c from list.
 * Notice that we don't need to provide a list: c knows where it's located.
 */
void fl_remove(chunk *c)
// pre: c is in a list
// post: c is removed from that list
{
  c->prev->next = c->next;
  c->next->prev = c->prev;
}

/*
 * fl_size(l).
 * Determine the size of the list l.
 * If list is circular l can be a reference to any node in the list.
 */
int fl_size(chunk *l)
// pre: l is a list
// post: returns number of elements in l
{
  chunk *p = l->next;	// p moves around list until it hits l
  int size = 0;
  while (p != l) {
    p = p->next;
    size++;
  }
  return size;
}

/*
 * fl_findBestFit(l,size).
 * We look for element in list l that will best hold a size targetPayload payload.
 */
chunk *fl_findBestFit(chunk *l, int targetPayload)
// pre: l is a list of free chunks, targetPayload is minimum required payload size
// post: returns the "best" chunk in free list
{  
  
  chunk *bestSoFar = l; //best so far starts out pointing to dummy node
  chunk *p = l->next; 
  int excess = 0;
  int bestExcess = 0;
  
  //iterate through FreeList to find closest matching chunk
  while (p != l) {
    if (ck_payloadSize(p) < targetPayload) { 
      p = p->next; 
      
      //If exact match, return immediately; otherwise keep looking
    } else if (ck_payloadSize(p) == targetPayload) { 
      return p; 
      
      //current payload > targetPayload so there's room for splitting
    } else { 
      excess = ck_payloadSize(p) - targetPayload; 
      
      if(!bestExcess || excess < bestExcess) { //if we haven't found a best chunk yet or if current free chunk does better 
	bestExcess = ck_payloadSize(bestSoFar) - targetPayload; //excess of best so far	  
	bestSoFar = p;//found a better match; update bestSoFar	  	 
	p = p->next;
	  
      } else { //current free chunk does the same or worse than bestSoFar
	p = p->next;
      }
    }
    return bestSoFar; //return a pointer to the proper chunk, NOT payload
  }
  
  //if nothing matches at all, bestSoFar points to dummy node
  return bestSoFar;
}

/**
 * PUBLIC METHODS.
 **/
/*
 * hmalloc(size).
 * Allocate and return memory to hold size bytes.
 */
void *hmalloc(int size)
{  
  /*
   Look through free list to see if there's a chunk big enough to suit your needs (size). If no, grows by at least size. Returns the pointer to the beginning of the whole payload area, not the header; to preserve header info
   */
  init();
  
  if (size < H_MINPAYLOAD) {
    size = H_MINPAYLOAD;
  }
  
  chunk *found = fl_findBestFit(FreeList, size);
  if (found->header == 0) { //if dummy
    found = grow(size, FreeList); //we know if we got to this point nothing in freelist fit
                                  //chunk we allocate in grow is the one we want to grab
  } else {
    ck_split(found, size);
  }

  ck_setInfo(found, ck_size(found));
  fl_remove(found);
  
  return (chunk*)PTR_ADD(found, H_IS); //return pointer to the payload

}

/*
 * hcalloc(count,size).
 * Allocate, zero, and return array of count elements, each sized size.
 */
void *hcalloc(int count, int size)
{
  init();
  int n = count*size;
  return memset(hmalloc(n),0,n);
} 

/*
 * hrealloc(p,size)
 * Re-allocate memory pointed to by p to be at least size.
 * No guarantees about optimality.
 */
void *hrealloc(void *p, int size)
{
  init();
  chunk *c = (chunk*)PTR_ADD(p,-H_IS);
  if (ck_payloadSize(c) < size) {
    void *q = hmalloc(size);
    memcpy(q,p,size);
    hfree(p);
    return q;
  } else {
    return p;
  }
} 


/*
 * hfree(m).
 * Return/recycle heap-allocated memory, m.
 */
void hfree(void *m)
//return a chunk or some memory to the free list. reset free bits to free
{
  init();
    
  chunk *theChunk = (chunk*)PTR_ADD(m, -H_IS); 
  
  if (!(theChunk->header)&H_FREE) { //use bit mask of 0001; if 1, chunk is free
    ck_print(theChunk); //this is the chunk being freed      
    int size = ck_size(theChunk); //size of the entire chunk; saved in header info
    ck_setInfo(theChunk, size|H_FREE); //reset the flag bits to free
    
    fl_insert(FreeList, theChunk);
  } else {
    printf("Cannot free a chunk that's already free\n");
  }
}

/*
 * hstrdup(s).
 * Allocate new copy of string s using just the space necessary.
 */
char *hstrdup(char *s)
{
  return strcpy((char*)hmalloc(strlen(s)),s);
}

/**
 * DEBUGGING ROUTINES.
 **/

/*
 * ck_print(c).
 * Print out information about chunk c.
 */
void ck_print(chunk *c)
// pre: c is a pointer to a chunk.
// post: prints a description to stdout
{
  int size = c->header & H_SIZEMASK;
  int paySize = ck_payloadSize(c);
  info *foot = ck_footerAddr(c);
  int f = c->header & H_FREE;
  printf("%schunk @%p, size %d (payload %d), %svalid.",f?"Free ":"Working ",c,size,paySize,(c->header == *foot)?"":"in");
  if (c->header != *foot) {
    printf(" (head: %d, foot: %d)\n",c->header,*foot);
  }
  putchar('\n');
}

/*
 * fl_print().
 * Prints the chunks in the order they're encountered on the free list.
 */
void fl_print(void)
// pre: FreeList has been initialized.
// post: prints chunks appearing on the free list to stdout
{
  init();
  int s = fl_size(FreeList);
  printf("Free list contains %d chunks:\n",s);
  chunk *p = FreeList->next;
  int i = 0;
  while (p != FreeList) {
    printf(" %d. ",i); ck_print(p);
    i++;
    p = p->next;
  }
}

/*
 * hprint()
 * Print out the segment(s) between BASE and HWM.
 * All allocated and free chunks are described as encountered.
 */
void hprint(void)
// post: prints the chunks in the heap between BASE and HWM
{
  init();
  void *p = BASE;
  while (p < HWM) {
    // loop across segments
    info i = *(info*)p;
    if (i == 0) { // i should be a dummy (0) info field
      printf("%p: base dummy\n",p);
      p = PTR_ADD(p,H_IS);
      while ((i = ck_size(p))) { // p is a non-dummy info
	printf("%p: ",p); ck_print(p);
	p = PTR_ADD(p,i);
      }
      printf("%p: top dummy\n",p);
      p = PTR_ADD(p,H_IS);
    }
  }
}

/*
 * A main method used to test each memory allocation function individually

int main()
{
  
  int *ip = (int*)hcalloc(10,sizeof(int));
  hprint();
  hfree(ip);
  
  printf("--------------------------------------------------------------\n");
  grow(8, FreeList); //FreeList[1] is actual chunk
  printf("grow(8, FreeList)\n");
  fl_print();
  hprint();
 
  printf("--------------------------------------------------------------\n");
  chunk *fit = fl_findBestFit(FreeList, 4000);
  fl_print();
  printf("Best fit for payload 4000: %d (size) %d (payload size)\n", fit->header, ck_payloadSize(fit));

  int *a = (int*)hmalloc(4000);
  printf("HMALLOC(4000)\n");
  fl_print();
  hprint();
  
  printf("------------------------------------------------------\n");
  int *b = (int*)hmalloc(20);
  printf("HMALLOC(20)\n");
  chunk *nextfit = fl_findBestFit(FreeList, 20);
  fl_print();
  hprint();
  printf("Best fit for payload 20: %d (size) %d (payload size)\n", nextfit->header, ck_payloadSize(nextfit));  
  
  printf("------------------------------------------------------\n");
  hmalloc(8);
  printf("HMALLOC(8)\n");
  fl_print();
  hprint();
 
  printf("------------------------------------------------------\n");
  printf("FREE the 4000!\n");
  hfree(a);
  fl_print();
  hprint();
  printf("Try to FREE the 4000 again?\n");
  hfree(a);
  fl_print();
    
  printf("------------------------------------------------------\n");
  printf("FREE the 20!\n");
  hfree(b);
  hprint();
  fl_print();
  
  //printf("Page size is:%d\n", PAGE_SIZE);
  //int i = 8;
  //int pSize = PAGE_SIZE;
  //i = (i + pSize-1)/pSize*pSize;
  //printf("Payload size is:%d\n", i);
  return 0;
}

*/



