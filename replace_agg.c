/*
** replace_agg.c - 2013 - jakethaw
**
*************************************************************************
**
** MIT License
** 
** Copyright (c) 2020 jakethaw
** 
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
** 
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
** SOFTWARE.
*/
/*
*************************************************************************
** SQLite3  *************************************************************
*************************************************************************
**
** replace_agg(A, B, C)
**
** The aggregate replace_agg(A, B, C) function. Three arguments are all strings: 
** call them A (constant), B, and C. The result is also a string which is derived
** from A by replacing every occurrence of each B with its corresponding C.
** The match must be exact. Collating sequences are not used.
** replace_agg() output is identical to that of the scalar replace() function if 
** aggregating over 1 row.

** To compile replace_agg with gcc as a run-time loadable extension:
**
**   UNIX-like : gcc -g -O3 -fPIC -shared replace_agg.c -o replace_agg.so
**   Mac       : gcc -g -O3 -fPIC -dynamiclib replace_agg.c -o replace_agg.dylib
**   Windows   : gcc -g -O3 -shared replace_agg.c -o replace_agg.dll
**
*************************************************************************
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <string.h>

typedef struct replace_Replacement *replace_Replacement;
struct replace_Replacement {
  char *z;                               /* The replacement string C */
  int n1;                                /* Length of C */
  int n2;                                /* Length of pattern string B */
  int i;                                 /* Counter of usage */
};
typedef struct replace_PatternLocation *replace_PatternLocation;
struct replace_PatternLocation {
  int i;                                 /* Location of B in A */
  struct replace_Replacement *r;         /* Link to B's replacement string C */
  struct replace_PatternLocation *p;     /* Link to next PatternLocation */
};
typedef struct replaceCtx replaceCtx;
struct replaceCtx {
  char *z;                               /* The input string A */
  int n;                                 /* Length of A */
  struct replace_PatternLocation *p;     /* Link to head PatternLocation */
  int firstStep;                         /* False if A needs to be stored */
};

static void replaceStep(sqlite3_context *context, int argc, sqlite3_value **argv){
  const char *z;                         /* The pattern string B */
  int n;                                 /* Length of B */
  const char *z2;                        /* Temporary pointer to an input string */ 
  int i, j;                              /* Loop counters */
  int bFirstMatch = 1;                   /* True if pattern string needs to be stored */
  replaceCtx *ctx;                       /* SQLite aggregate context */
  replace_Replacement r, r2;             /* Active replacement, and neighbouring replacement */
  replace_PatternLocation p, p2, p3, p4; /* Active PatternLocation, and neighbouring PatternLocations */

  ctx = (replaceCtx*)sqlite3_aggregate_context(context, sizeof(*ctx));
  
  if( ctx ){
    if( !ctx->firstStep ){
      /* Store the input string A */
      if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
      z2 = (char *)sqlite3_value_text(argv[0]);
      ctx->n = sqlite3_value_bytes(argv[0]);
      if( ctx->n ){
        ctx->z = sqlite3_malloc(ctx->n);
        memcpy(ctx->z, z2, ctx->n);
      }
      ctx->p = 0;
      ctx->firstStep = 1;
    }
    /* if A is empty, then don't bother continuing */
    if( !ctx->n ) return;

    /* get pattern string B */
    if( sqlite3_value_type(argv[1])==SQLITE_NULL ) return;
    if( sqlite3_value_type(argv[2])==SQLITE_NULL ) return;
    z = (char *)sqlite3_value_text(argv[1]);
    n = sqlite3_value_bytes(argv[1]);
    if( !n ) return;

    for(i=0; i<=ctx->n-n; i++){
      /* check first character match */
      if( ctx->z[i]==z[0] ){
        j=1;
        /* check rest of characters */
        if( n>1 ){
          while (ctx->z[i+j]==z[j]){
            j++;
            if( j==n ) break;
          }
        }
        /* if match is found */
        if( j==n ){
          if( bFirstMatch ){
            /* Store the replacement string if the first occurrence 
            ** of the pattern string is found
            */
            bFirstMatch=0;
            r = (replace_Replacement) sqlite3_malloc(sizeof(struct replace_Replacement));
            r->n2 = n;
            z2 = (char *)sqlite3_value_text(argv[2]);
            r->n1 = sqlite3_value_bytes(argv[2]);
            if( r->n1 ){
              r->z = sqlite3_malloc(r->n1);
              memcpy(r->z, z2, r->n1);
            }
            r->i = 0;
          }
          /* Store the location of the pattern string in the input string */
          p = (replace_PatternLocation) sqlite3_malloc(sizeof(struct replace_PatternLocation));
          p->i = i;
          p->r = r;
          r->i++;

          /* Insert pattern location into linked list */
          if( ctx->p == 0 ){
            /* if this is the first pattern location, just make 
            ** it the head node
            */
            p->p = 0;
            ctx->p = p;
          }else{
            /* else go through linked list, starting at head node.
            ** node order: p4->p3->p2. 
            ** 
            ** Potential outcomes for p (the new pattern location):
            ** 
            ** Case 1: p lies before p2 where p2 is the head node, and
            **         no overlapping occurs.
            **         => p becomes the head node.
            ** 
            ** Case 2: p lies between p3 and p2 and no overlapping occurs.
            **         => p will be inserted between p3 and p2.
            ** 
            ** Case 3: p overlaps either p3 or p2 and has a shorter
            **         pattern string length.
            **         => p will be ignored.
            ** 
            ** Case 4: p3 overlaps p on the left, and p3 has a shorter
            **         pattern string length.
            **         => p will take the position of p3.
            ** 
            ** Case 5: p2 overlaps p on the right, and p2 has a 
            **         shorter pattern string length.
            **         => p will take the position of p2.
            ** 
            ** Case 6: p3 overlaps p on the left, and p2 overlaps
            **         p on the right, and both p3 and p2 have
            **         shorter pattern string length.
            **         => p will take position of p2 and p3.
            ** 
            ** Case 7: p lies after the current tail node,
            **         and does not overlap.
            **         => p becomes the tail node.
            ** 
            ** Case 8: the tail node overlaps p on the left and 
            **         the tail node has a shorter pattern string length.
            **         => p will take the position of the tail node.
            ** 
            ** Case 9: the tail node overlaps p on the left and 
            **         p has a shorter pattern string length.
            **         => p will be ignored.
            */
            p2 = ctx->p;
            p3 = p4 = 0;
            while (1){
              /* if arrived at insertion point */
              if( p->i < p2->i ) {
                /* check if any overlapping will occur at this point */
                if( p3 ){
                  /* if p3 currently points to a node,
                  ** then check for left overlap
                  */
                  r2 = p3->r;
                  if( p3->i+r2->n2 > p->i ){
                    /* overlap does occur, so remove shorter pattern */
                    if( r2->n2 < r->n2 ){
                      /* p3 is shorter
                      ** if p4 currently points to a node,
                      ** then link it to p2, otherwise p2 will
                      ** be the head node (p is yet to be inserted)
                      */
                      if( p4 ){
                        p4->p = p2;
                      }else{
                        ctx->p = p2;
                      }
                      /* remove replacement string if all matching patterns
                      ** have been overlapped
                      */
                      r2->i--;
                      if( !r2->i ){
                        if( r2->n1 ) sqlite3_free(r2->z);
                        sqlite3_free(r2);
                      }
                      /* remove current p3 and make p3 = p4 */
                      sqlite3_free(p3);
                      p3 = p4;
                    }else{
                      /* p is shorter, no changes to list */
                      r->i--;
                      sqlite3_free(p);
                      p = 0;
                    }
                  }
                }
                if( p ){
                  /* if p still exists, then check for right overlap */
                  if( p->i+r->n2 > p2->i ){
                    /* overlap does occur, so remove shorter pattern */
                    r2 = p2->r;
                    if( r2->n2 < r->n2 ){
                      /* p2 is shorter */
                      if( p3 ){
                        p3->p = p2->p;
                      }else{
                        ctx->p = p2->p;
                      }
                      /* remove replacement string if all matching patterns
                      ** have been overlapped
                      */
                      r2->i--;
                      if( !r2->i ){
                        if( r2->n1 ) sqlite3_free(r2->z);
                        sqlite3_free(r2);
                      }
                      /* remove current p2 and make p2 it's link */
                      p4 = p2;
                      p2 = p2->p;
                      sqlite3_free(p4);
                    }else{
                      /* p is shorter, no changes to list */
                      r->i--;
                      sqlite3_free(p);
                      p = 0;
                    }
                  }
                }
                if( p ){
                  /* if p still exists at this point
                  ** insert new pattern location between p3 and p2
                  */
                  p->p = p2;
                  if( !p3 ){ /* (!p3 => p2 is the head node) */
                    /* if p2 was the head node, just make
                    ** p the new head node
                    */
                    ctx->p = p;
                  }else{
                    p3->p = p;
                  }
                }
                break;
              }else{
                /* if final node */
                if ( !p2->p ){
                  /* check for right overlap */
                  r2 = p2->r;
                  if( p2->i+r2->n2 > p->i ){
                    /* overlap does occur, so remove shorter pattern */
                    if( r2->n2 < r->n2 ){
                      /* p2 is shorter
                      ** if p3 currently points to a node,
                      ** then link it to p, otherwise p will
                      ** be the head node
                      */
                      p->p = 0;
                      if( p3 ){
                        p3->p = p;
                      }else{
                        ctx->p = p;
                      }
                      /* remove p2 */
                      r2->i--;
                      if( !r2->i ){
                        if( r2->n1 ) sqlite3_free(r2->z);
                        sqlite3_free(r2);
                      }
                      sqlite3_free(p2);
                    }else{
                      /* p is shorter, no changes to list */
                      r->i--;
                      sqlite3_free(p);
                      p = 0;
                    }
                  }else{
                    /* else, no overlap */
                    p2->p = p;
                    p->p = 0;
                  }
                  break;
                }
                /* otherwise move to next node */
                if( p3 ) p4 = p3;
                p3 = p2;
                p2 = p2->p;
              }
            }
          }
          /* add n-1 to continue searching the input
          ** string from beyond the matched pattern
          */
          i += n-1;
        }
      }
    }
    /* remove replacement string of current step
    ** if all matches have been removed due to overlap
    ** (this is fairly unlikely, but still possible)
    */
    if( !bFirstMatch ){
      if( !r->i ){
        if( r->n1 ) sqlite3_free(r->z);
        sqlite3_free(r);
      }
    }
  }
}
static void replaceFinalize(sqlite3_context *context){
  char *z;
  int n = 0;
  int i, j;
  replaceCtx *ctx;
  replace_PatternLocation p, p2;
  replace_Replacement r;
  
  ctx = sqlite3_aggregate_context(context, 0);
  
  if( ctx ){
    /* if the input string is empty,
    ** then return an empty string
    */
    if( !ctx->n ){
      sqlite3_result_text(context, "", 0, SQLITE_STATIC);
      return;
    }

    /* if there are no values to replace, then 
    ** just return the input string unchanged
    */
    if( !ctx->p ){
      sqlite3_result_text(context, ctx->z, ctx->n, sqlite3_free);
      return;
    }
    
    /* calculate memory required */
    p = ctx->p;
    while (1){
      r = p->r;
      n += r->n1 - r->n2;
      if( !p->p ) break;
      p = p->p;
    }
    n += ctx->n;
    /* if the output string has length 0,
    ** then return an empty string
    */
    if( !n ){
      sqlite3_result_text(context, "", 0, SQLITE_STATIC);
      return;
    }
    z = sqlite3_malloc(n);
    
    /* construct new string */
    p = ctx->p;
    i = p->i;
    /* copy first part of input string */
    memcpy(&z[0], ctx->z, i);
    while (1){
      r = p->r;
      /* copy replacement string */
      if( r->n1 ) memcpy(&z[i], r->z, r->n1);
      i += r->n1;
      r->i--;
      
      /* quit loop if on last pattern location */
      if( !p->p ) {
        if( r->n1 ) sqlite3_free(r->z);
        sqlite3_free(r);
        sqlite3_free(p);
        break;
      }
      
      /* copy next part of input string */
      j = p->i + r->n2;
      p2 = p;
      p = p->p;
      sqlite3_free(p2);
      memcpy(&z[i], &(ctx->z[j]), p->i-j);
      i += p->i-j;
      if( !r->i ){
        if( r->n1 ) sqlite3_free(r->z);
        sqlite3_free(r);
      }
    }
    /* tail end of input string */
    memcpy(&z[i], &(ctx->z[ctx->n-n+i]), n-i);
    sqlite3_free(ctx->z);
    
    sqlite3_result_text(context, z, n, sqlite3_free);
  }
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_replaceagg_init( 
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db,
                               "replace_agg",
                               3,
                               SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                               0,
                               0, 
                               replaceStep,
                               replaceFinalize);
  return rc;
}
