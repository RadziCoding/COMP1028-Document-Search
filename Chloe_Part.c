#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
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
    uint64_t mask;
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
// To check if a word is already in the terms array
static bool is_duplicate (const char *word, char terms[][MAX_WORD], int count) {
        for (int i = 0; i < count; i++) {
                if (strcmp(terms[i], word) == 0) {
                        return true;
                }
        }
        return false;
}

// Memory allocation
static int alloc_track_arrays (double **scores, uint64_t **masks) {
/* Return code: 
            0 = Failed
            1 = Success
                    */
        *scores = NULL;
        *masks = NULL;

    if (collect.count <= 0) {
        return 1;
    }

    *scores = calloc((size_t)collect.count, sizeof(**scores));
    *masks = calloc((size_t)collect.count, sizeof(**masks));

    if (*scores == NULL || *masks == NULL) {
        free(*scores);
        free(*masks);

        *scores = NULL;
        *masks = NULL;

        return 0;
    }

    return 1;
}

// Counts matching query words inside a single document's text
static void score_document_text (const char *text, int doc_id, char terms[][MAX_WORD], int nterms, double *scores, uint64_t *masks) {
        int frequencies[MAX_QUERY_TERMS] = {0};

    const char *cursor = text;
    char word[MAX_WORD];

    while (next_token(&cursor, word)) {
        for (int i = 0; i < nterms; i++) {
            if (strcmp(word, terms[i]) == 0) {
                frequencies[i]++;
            }
        }
    }

    for (int i = 0; i < nterms; i++) {
        if (frequencies[i] > 0) {
            Term *term = find_term(terms[i]);

            if (term != NULL) {
                scores[doc_id] +=
                    term_score(frequencies[i], term->doc_freq);

                masks[doc_id] |= (UINT64_C(1) << i);
            }
        }
    }
}

/* ------------------------
        Inverted Index 
   ------------------------ */
// Includes the use of djb2 (For fast data lookup/pinpoint)

// Converts the word into a unique array index
// So the program knows which hash table bucket to look in
static unsigned long hash_word (const char *string) {
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
static Term *find_or_create_term (const char *word, int *created) {
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

        // Copy the word string into the new TermS
        // Allocate memory for the word, including the null terminator
        size_t length = strlen(word) + 1;

        new_term -> word = malloc(length);
        if (new_term -> word == NULL) {
                free (new_term);    // Cleaning up the Term structure before failing
                return NULL;
        }

        memcpy(new_term -> word, word, length);

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
static int add_occurence (Term *term, int doc_id) {
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

                index.buckets[bucket] = NULL; // Avoid dangling pointer to freed memory
        }
                // Reset all index counters and status flags back to zero
                index.unique_terms = 0;
                index.total_postings = 0;
                index.total_terms = 0;
                index_built = 0;
}

// Releases the memory used to store the loaded documents and
// resets the document collection state
static void free_collection (void) {

        free_index();
        clear_last_result();
        
        free (collect.docs);

        collect.docs = NULL;
        collect.count = 0;
        collect.capacity = 0;
        collect.folder[0] = '\0';
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

// Splits the query into unique, valid search terms
static int parse_query (const char *query, char terms[][MAX_WORD]) {
        int unique_count = 0;
        const char *cursor = query;
        char current_word[MAX_WORD];

        while (next_token (&cursor, current_word)) {
                // Skip common filler words if stopword filtering is enabled
                if (use_stopwords && is_stopword (current_word)) {
                        continue;
                }
                if (unique_count >= MAX_QUERY_TERMS) {
                    break;
                }

                if (is_duplicate(current_word, terms, unique_count)) {
                        continue;
                }
                // Add the unique word if there is still room in the array
                        strcpy (terms[unique_count], current_word);
                        unique_count++;
        }
        return unique_count;
}

// Sorts results: Highest score first, then lowest document ID first
static int cmp_result (const void *a, const void *b) {
/* Return code: 
            -1 = 'first' comes before 'second'
            1 = 'second' comes before 'first'
                    */
        const Result *first = (const Result *)a;
        const Result *second = (const Result *)b;
        
        if (first -> score > second -> score) {
                return -1;
        }
        if (first -> score < second -> score) {
                return 1;
        }

        if (first->doc_id < second->doc_id) {
                return -1;
        }

        if (first->doc_id > second->doc_id) {
                return 1;
        }
        return 0;
}

// Calculates the relevance score for a single word in a document
static double term_score (int term_freq, int doc_freq) {

        if (doc_freq <= 0 || collect.count <= 0) {
                return 0.0;
        }

        // Mode 2: Term Frequency-Inverse Document Frequency (TF-IDF)
        if (scoring_mode == 2) {
                double tf_weight = 1.0 + log((double)term_freq);
                double idf_weight = log(1.0 + (double)collect.count / (double)doc_freq);
                return tf_weight * idf_weight;
        }

        // Mode 1 (Default): Simple raw term frequency
        return (double)term_freq;
}

// Result Aggregation & Sorting
// Converts raw score/mask arrays into a compact, sorted array of matched documents
static Result *finalise_results (double *scores, uint64_t *masks, int *count) {

        // Count how many documents matched at least one query term
        int matched_count = 0;
        for (int i = 0; i < collect.count; i++) {
                if (masks[i] != 0) {
                        matched_count++;
                }
        }

        *count = matched_count;
        if (matched_count == 0) {
                return NULL;
        }

        // Allocate memory only for the matched documents
        Result *results = malloc ((size_t)matched_count * sizeof (*results));
        if (!results) {
                *count = -1;
                return NULL;
        }

        // Populate the compact results array
        int result_index = 0;
        for (int i = 0; i < collect.count; i++) {
                if (masks[i] != 0) {
                        results[result_index].doc_id = i;
                        results[result_index].score = scores[i];
                        results[result_index].mask = masks[i];
                        result_index++;
                }
        }

        // Sort results by relevance score
        qsort (results, (size_t)matched_count, sizeof(*results), cmp_result);
        return results;
}

// Inverted Index Search (Fast Retrieval)
// Looks up only the posting lists of the query terms
static Result *search_index (char terms[][MAX_WORD], int nterms, int *count) {

        // Allocate temporary tracking arrays for all documents
        double *scores = NULL;
        uint64_t *masks = NULL;

        if (!alloc_track_arrays (&scores, &masks)) {
                *count = -1;
                return NULL;
        }

        // Look up each search term in the index
        for (int term_index = 0; term_index < nterms; term_index++) {
                Term *term = find_term(terms[term_index]);
                if (!term) {
                        continue;
                }

                // Update scores for every document containing this term
                for (Posting *p = term -> head; p != NULL; p = p -> next) {
                        if (p->doc_id < 0 || p->doc_id >= collect.count) { 
                                continue;
                        }

                        scores[p->doc_id] += term_score(p->freq, term->doc_freq);
                        masks[p->doc_id] |= (UINT64_C(1) << term_index);
                }
        }

        // Package, sort, and clean up
        Result *results = finalise_results (scores, masks, count);
        free (scores);
        free (masks);

        return results;
}

// Linear scan search (Benchmarking baseline)
// Re-reads and re-tokenises every document from disk for every query
static Result *search_linear (char terms[][MAX_WORD], int nterms, int *count) {

        // Allocate temporary tracking arrays for all documents
        double *scores = NULL;
        uint64_t *masks = NULL;

        if (!alloc_track_arrays (&scores, &masks)) {
                *count = -1;
                return NULL;
        }

        // Read every file from disk and check its words
        for (int doc_id = 0; doc_id < collect.count; doc_id++) {
                char *file_text = read_file (collect.docs[doc_id].path, NULL);
                if (!file_text) {
                        continue;
                }

                score_document_text (file_text, doc_id, terms, nterms, scores, masks);
                free (file_text);
        }

        // Package, sort, and clean up
        Result *results = finalise_results (scores, masks, count);
        free (scores);
        free (masks);

        return results;
}

// Prints a formatted table of the top matching search results
// Displays each doc's rank, filename, relevance score, and the specific query terms (capped at the max search results)
static void print_results (FILE *out, char terms[][MAX_WORD], int nterms, Result *results, int count, int max_search_results) {

        // Handle the case where nothing matched
        if (count == 0) {
                fprintf (out, "No documents matched your query. \n");
                return;
        }

        // Decide how many results to display (cap at MAX_SEARCH_RESULTS)
        int num_to_show = count;
        if (num_to_show > max_search_results) {
                num_to_show = max_search_results;
        }

        // Print the summary line and the table headers
        fprintf (out, "%d matching document(s). Showing top %d:\n", count, num_to_show);
        fprintf (out, "%-5s %-30s %-10s %s\n", "Rank", "File", "Score", "Matched terms");

        // Print each matching document row by row
        for (int rank = 0; rank <num_to_show; rank++) {
                Result result = results[rank];
                const char *filename = collect.docs[result.doc_id].filename;

                // Print rank, filename, and score
                fprintf (out, "%-5d %-30.30s %-10.2f", rank + 1, filename, result.score);

                // Print the matched query terms at the end of the row
                print_matched_terms (out, terms, nterms, result.mask);
                fprintf (out, "\n");
        }
}

// Handles user input, runs the seaarch, times it, and saves the resultsfor reporting
static void do_search (void) {

        // Clear previous search results before starting a new search
        clear_last_result();
        
        // Make sure the search index is ready before seaarching
        if (!index_built) {
                printf ("Build or load the index first. \n");
                return;
        }
        // Read the search query from the user 
        char raw_query[LINE_LEN];
        printf("Enter search query: ");

        if (fgets(raw_query, sizeof(raw_query), stdin) == NULL) {
                return;
        }

        raw_query[strcspn(raw_query, "\n")] = '\0';
        
        // Clean and split the query into unique search terms
        char query_terms[MAX_QUERY_TERMS][MAX_WORD];
        int term_count = parse_query(raw_query, query_terms);

        if (term_count == 0) {
                printf ("Query is empty (or contains only stopwords/punctuation). \n");
                return;
        }

        // Display the cleaned search terms
        printf ("Normalised query terms: ");
        for (int i = 0; i < term_count; i++) {
                printf (" %s", query_terms[i]);
        }
        printf ("\n");

        // Run the search and measure how long it takes in miliseconds
        clock_t start_time = clock();
        int result_count = 0;
        Result *search_results = search_index (query_terms, term_count, &result_count);

        if (search_results == NULL && result_count == -1) {
                printf("Error: Memory allocation failed.\n");
                return;
        }
        
        clock_t end_time = clock();

        double elapsed_ms = 1000.0 * (double)(end_time - start_time) / CLOCK_PER_SEC;

        // Print the top results and the search duration
        print_results (stdout, query_terms, term_count, search_results, result_count, max_search_results);
        printf ("Search time: %.4f ms\n", elapsed_ms);

        // Save the results and query details in case the user generates a report later
        free (last_results);
        last_results = search_results;
        last_count = result_count;
        last_nterms = term_count;

        for (int i = 0; i < term_count; i++) {
                strcpy (last_terms[i], query_terms [i]);
        }
        snprintf (last_query, sizeof(last_query), "%s", raw_query);
}
