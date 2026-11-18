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
        Helper Methods 
   ------------------------ */

/* ------------------------
        Inverted Index 
   ------------------------ */
// Includes the use of djb2 (For fast data lookup/pinpoint)

// Converts the word into a unique array index
// So the program knows which hash table bucket to look in
static unsigned long hash_word (const char *s) {
        unsigned long hash_value = 5381;     // starting seed from the djb2 algorithm
        while (*string != '\0') {
                hash_value = hash_value * 33 + (unsigned char) *string++;
        }
        return hash_value % HASH_SIZE;
}

// Searches a specific hash table bucket to see if a word is already stored in the index, 
// returning a pointer to it if found
static Term *find_term (const char *word) {
        // Find which bucket index the word belongs in
        unsigned long bucket_index = hash_word(word);

        // Start with the first Term node in that bucket's linked list
        Term *current = index.buckets[bucket_index];
    
        while (current != NULL) {
                // Checking whether the stored word matches the target word
                if (strcmp (current -> word, word) == 0) {
                        return current;
                }
                current = current -> next;  // Move to the next term in the linked list
        }
        return NULL;    // Intended word wasn't found
}

// Looks up a word using find_term and, if it doesn't exist yet, 
// allocate memory for a new Term entry and adds it to the hash table
static Term *find_create_term (const char *word, int *created) {
/* Return code: 
            0 = Tell caller a new term wasn't created
            1 = Tell caller a new term was created
            NULL = Out of memory
                    */
        // Check if the word is already in the hash table
        Term *existing_term = find_term(word);

        if (existing_term != NULL) {
                if (created != NULL) {
                        *created = 0;   
                }
                return existing_term;
        }

        // Allocating a new zero-initialised memory for the new Term
        Term *new_term = calloc (1, sizeof *new_term);
        if (new_term == NULL) {
                return NULL;   
        }

        // Copy the word string into the new Term
        new_term -> word = duplic_string (word);
        if (new_term -> word == NULL) {
                free (new_term);    // Cleaning up the Term structure before failing
                return NULL;
        }

        // Insert the new Term at the front of its hash bucket's linked list
        unsigned long bucket_index = hash_word(word);
        new_term -> next = index.buckets[bucket_index];
        index.buckets[bucket_index] = new_term;
        index.unique_terms++;

        if (created != NULL) {
                *created = 1;   // Tell caller a new term was created
        }
        return new_term;
}

// Records that a word appeared in a specific document by either increasing the
// frequency count on an existing entry or adding a new Posting node to the word's list
static int add_occurence (Term *t, int doc_id) {
/* Return code: 
            -1 = Allocation failure
            0 = Existing posting updated
            1 = New posting created
                    */

        // If the last posting is already for this same document, then just add 1 to its frequency
        if (term -> tail != NULL && term -> tail -> doc_id == doc_id) {
                term -> tail -> freq++;
                return 0;  
        }

        // Allocate a new Posting node for this document
        Posting *new_posting = malloc (sizeof *new_posting);
        if (new_posting == NULL) {
                return -1;
        }

        new_posting -> doc_id = doc_id;
        new_posting -> freq = 1;
        new_posting -> next = NULL;

        // Attach the new posting to the end of the Term's posting list
        if (term -> tail != NULL) {
                term -> tail -> next = new_posting;
        }
        else {
                term -> head = new_posting;     // First posting in the list
        }
        term -> tail = new_posting;

        // Update document frequency and global posting counts
        term -> doc_freq++;
        index.total_postings++;

        return 1; 
}

// Cleans up all dynamically allocated memory used by the hash table, posting lists, 
// and words, then resets the index counters back to zero
static void free_index (void) {

        // Loop through every bucket in the hash table
        for (int bucket = 0; bucket < HASH_SIZE; bucket++) {
                Term *current_term = index.buckets[bucket];

                // Walk through all Term nodes chained in this bucket
                while (current_term != NULL) {
                        // Save the pointer to the next Term before freeing the current
                        Term *next_term = current_term -> next;

                        // Free every Posting node attached to this Term
                        Posting *current_posting = current_term -> head;
                        while (current_posting != NULL) {
                                Posting *next_posting = current_posting -> next;
                                free (current_posting);
                                current_posting = next_posting;
                        }

                        // Free the word string and the Term structure itself
                        free (current_term -> word);
                        free (current_term);

                        // Advance to the next Term in the bucket
                        current_term = next_term;
                }

                // Reset all index counters and status flags back to zero
                index.unique_terms = 0;
                index.total_postings = 0;
                index.total_terms = 0;
                index_built = 0;
        }
}

// Releases the memory used to store the loaded documents and
// resets the document collection state
static void free_collection (void) {
        free (collect.docs);
        collect.docs = NULL;
        collect.count = collect.capacity = 0;
}

// Wipes out the stored search results from the previous query
// so the program is ready for a fresh search
static void clear_last_result (void) {
        free (last_results);
        last_results = NULL;
        last_count = 0;
        last_nterms = 0;
        last_query[0] = '\0';
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
