#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#define SKIPLIST_MAXLEVEL 32 /* Should be enough for 2^32 elements */
#define SKIPLIST_P 0.25      /* Skiplist P = 1/4 */

typedef struct skiplistNode {
    void *obj;
    double score;
    struct skiplistLevel {
        struct skiplistNode *forward;
        unsigned int span;
    } level[];
} skiplistNode;

typedef struct skiplist {
    struct skiplistNode *header;
    unsigned long length;
    int level;
} skiplist;

skiplist *createList(void);
void freeList(skiplist *sl);
skiplistNode *skiplistInsert(skiplist *sl,double score,void *obj);
int skiplistDelete(skiplist *sl,double score,void *obj);
#endif
