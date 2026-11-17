#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>   // FOR: Benchmark indexing and query execution speeds (latency) and track document timestamps.
#include <dirent.h> // FOR: Opening and traversing directories to automatically discover and index local text files without hardcoding file paths.

/* ----------------------------
        Constant Variables 
   ---------------------------- */
#define HASH_SIZE 50021     // Prime number to avoid hash collisions
#define MAX_WORD 128        // Max word length (2^7 chars including '\0')
#define MAX_QUERY_TERMS 64  // Max words allowed in a single search query
#define NAME_LEN 256        // Standard OS max filename length 
#define PATH_LEN 1024       // Standard OS max file path length
#define LINE_LEN 4096       // 4 KB buffer size for reading file lines efficiently

/* -------------------------
        Data Structures 
   ------------------------- */
typedef struct Posting {
    int doc_id;
    int freq; 
    struct Posting *next;
}
Posting;

typedef struct Term {
    char *word;
    int doc_freq;
    Posting *head, *tail;
    struct Term *next;
}
Term;

typedef struct {
    int id;
    char filename[NAME_LEN];
    char path[PATH_LEN];
    long total_terms;
    long unique_terms;
} 
Document;

typedef struct {
    Document *docs;
    int count;
    int capacity;
    char folder[512];
}
Collection;

typedef struct {
    Term *buckets[HASH_SIZE];
    long unique_terms;
    long total_postings;
    long total_terms;
}
Index;

typedef struct {
    int doc_id;
    double score;
    unsigned long mask;
}
Result;

/* --------------------------
        Global Variables 
   -------------------------- */
static Collection collect;
static Index index;
static int index_built = 0;
static int use_stopwords = 1;
static int scoring_mode = 1;
static int max_search_results = 10;

static char last_query[LINE_LEN] = "";
static char last_terms[MAX_QUERY_TERMS][MAX_WORD];
static int last_nterms = 0;
static Result *last_results = NULL;
static int last_count = 0;

static const char *STOPWORDS[] = {"a", "an", "the", "and", "or", 
                                "is", "am", "are", "was", "were", 
                                "i", "you", "he", "she", "it", 
                                "they", "we", "as", "at", "be", 
                                "by", "for", "from", "has", "have", 
                                "had", "in", "on", "its", "of", 
                                "that", "to", "will", "with", NULL};

/* ------------------------
        Inverted Index 
   ------------------------ */
// Includes the use of djb2 (For fast data lookup/pinpoint)

static unsigned long hash_word (const char *s) {

}

static Term *find_term (const char *word) {

}

static Term *find_create_term (const char *word, int *created) {

}

static int add_occurence (Term *t, int doc_id) {

}


/* -------------------------------------
        Query, Ranking, & Display 
   ------------------------------------- */

static int parse_query (const char *query, char terms[][MAX_WORD]) {

}

static int cmp_result (const void *a, const void *b) {

}

static double term_score (int tf, int df) {

}

static Result *finalise_results (double *scores, unsigned long *masks, int *count) {

}

static Result *search_index (char terms[][MAX_WORD], int nterms, int *count) {

}

static Result *search linear (char terms[][MAX_WORD], int nterms, int *count) {

}

static void print_results (FILE *out, char terms[][MAX_WORD], int nterms, Result *r, int count, int k) {

}

static void do_search (void) {

}
