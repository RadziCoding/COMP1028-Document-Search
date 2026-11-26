#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <dirent.h>

#define HASH_SIZE 20011
#define MAX_WORD 64
#define MAX_QUERY_TERMS 32
#define NAME_LEN 256
#define PATH_LEN 1024
#define LINE_LEN 1024


/* One node in a term's posting list */
typedef struct Posting {
    int doc_id;
    int freq;
    struct Posting *next;
} Posting;

/* One unique term in the index */
typedef struct Term {
    char *word;
    int df;
    Posting *head, *tail;
    struct Term *next;
} Term;

/* Metadata for one document */
typedef struct {
    int id;
    char filename[NAME_LEN];
    char path[PATH_LEN];
    long total_terms;
    long unique_terms;
} Document;

typedef struct {
    Document *docs;
    int count, capacity;
    char folder[512];
} Collection;

typedef struct {
    Term *buckets[HASH_SIZE];
    long unique_terms;
    long total_postings;
    long total_terms;
} Index;

typedef struct {
    int doc_id;
    double score;
    unsigned long mask;
} Result;


/* Global state */
static Collection coll;
static Index idx;
static int index_built = 0;
static int use_stopwords = 1;
static int scoring_mode = 1;
static int top_k = 10;

/* Last search kept for save_report */
static char last_query[LINE_LEN] = "";
static char last_terms[MAX_QUERY_TERMS][MAX_WORD];
static int last_nterms = 0;
static Result *last_results = NULL;
static int last_count = 0;


static const char *STOPWORDS[] = {
    "a", "an", "and", "are", "as", "at", "be", "by",
    "for", "from", "has", "he", "in", "is", "it",
    "its", "of", "on", "that", "the", "to", "was",
    "were", "will", "with", NULL
};

static void write_stats(FILE *out, int all_docs){
    fprintf(out, "=== Collection statistics ===\n");
    fprintf(out, "Folder: %s\n", coll.folder);
    fprintf(out, "Documents: %d\n", coll.count);  
    fprintf(out, "Stopwords: %s\n", use_stopwords ? "removed" : "kept"); //ternary operator
    
    if (index_built){// built first to collect them as the index hasnt been populated yet
        fprintf(out, "Total terms: %ld\n", idx.total_terms);
        fprintf(out, "Unique terms: %ld\n", idx.unique_terms);
        fprintf(out,"Posting entries: %ld\n", idx.total_postings);
    
        if(idx.unique_terms > 0){
         double average = (double)idx.total_postings / (double) idx.unique_terms; 

        fprintf(out, "Average postings per term: %.2f\n", average);
        }
    
    }else {
        fprintf(out, "Index not built yet.\n");
    }    
   
    fprintf(out, "\n%-5s %-30s %-12s %s\n","ID","File","Total terms","Unique terms");
     int lim = all_docs ? coll.count: 
        (coll.count < 20 ? coll.count : 20);   //all docs = 0 then 20; all docs = 1 then 35     
         
        for (int i = 0; i < lim; i++){ 
            fprintf(out, "%-5d %-30.30s %-12ld %ld\n", coll.docs[i].id, 
                coll.docs[i].filename,coll.docs[i].total_terms,coll.docs[i].unique_terms);
            
        
    }
      if (lim < coll.count){
                fprintf(out,"...(%d more; full list is in the saved report)\n", coll.count - lim);
            }
    //changes being implemented 
    // Average total terms per document 
    // Average unique terms per document 
    // The largest document by total count 

    fprintf(out, "\n=== Collection analysis ===\n");

    if (coll.count > 0) {
        long total_doc_terms = 0;
        long total_doc_unique = 0;
        int largest_doc = 0;

        for (int i = 0; i < coll.count; i++){
            total_doc_terms += coll.docs[i].total_terms;
            total_doc_unique += coll.docs[i].unique_terms;

              if (coll.docs[i].total_terms > coll.docs[largest_doc].total_terms) {
            largest_doc = i;
            }
        }
            //print the largest document for checking all douments 
        if (total_doc_terms == 0){
                fprintf(out, "Largest document: all documnets are empty.\n");
            }else{
              fprintf(out, "Largest document: %s (%ld terms)\n", 
                coll.docs[largest_doc].filename,coll.docs[largest_doc].total_terms);    
            }

        fprintf(out, "Average terms per document: %.2f\n", (double)total_doc_terms / coll.count);
        fprintf(out, "Average unique terms per document: %.2f\n",(double)total_doc_unique / coll.count);
      // change 2
    // Vocubulary density     
    if(total_doc_terms > 0){
        double density = 100.0 * total_doc_unique / total_doc_terms;

        fprintf(out, "Vocabulary density: %.2f%%\n", density);
    }   
    }  
}

//checks if documents have been uploaded; calls write_stats
static void show_statistics(void){
if (coll.count == 0){//display all or set limits on docs
    printf("Load a collection first.\n");
    return;
}
write_stats(stdout,0);//where to diplay the output
}


static void save_report(void){

    if (coll.count == 0){
        printf("Load a collection first.\n");
        return;
    }

    FILE *f = fopen("report.txt","w");//return valid file poinet if opens
    if (f == NULL){//returns NULL if fails
        printf("Cannot write report.txt.\n");
        return;
    }
    time_t now = time(NULL);//gets current time

    fprintf(f,"COMP1028 Search Engine Report\nGenerated: %s\n",ctime(&now));//converts to readable time and date

    write_stats(f,1); // 1 because we need all docs and not only 20 docs
    fprintf(f,"\n=== Last search ===\n");

    if (last_query[0] == '\0'){//null terminator: if True last_query is empty

        fprintf(f,"(no search performed yet)\n");
    }

    else{
        fprintf(f, "Query: %s\nScoring: %s\n", last_query, scoring_mode == 2 ? "TF-IDF" : "term frequency");
        
        print_results(f, last_terms, last_nterms,last_results, last_count, top_k);
    }
    //checks for writing error.
    if (ferror(f)){
        printf("An error occured while writing the report.\n");
        fclose(f); // closes the opened the file. 
        return;  
    } //because error can occur when buffered search is there.
    if (fclose(f) != 0){
        printf("Warning: report may not have been saved completely.\n");
        return;
    }
    printf("Saved to report.txt\n");
}

static void save_index(void) {
    if (!index_built) /*if index is 0 there is nothing to save*/ {
    printf("Nothing to save: build the index first.\n");
    return;
}

FILE *f = fopen("index.dat", "w");

if (!f) {
    printf("Cannot write index.dat.\n");
    return;
}

fprintf(f, "COMP1028IDX 1\nSTOPWORDS %d\nDOCS %d\n",
        use_stopwords, coll.count);

        //Save document metadata
for (int i = 0; i < coll.count; i++) {
    fprintf(f, "%d\t%ld\t%ld\t%s\t%s\n",
            coll.docs[i].id,
            coll.docs[i].total_terms,
            coll.docs[i].unique_terms,
            coll.docs[i].filename,
            coll.docs[i].path);
}
//Save the number of unique terms
fprintf(f, "TERMS %ld\n", idx.unique_terms);

//save every term and its posting list
for (int b = 0; b < HASH_SIZE; b++){
    for(Term *t = idx.buckets[b]; t != NULL; t = t->next){
        fprintf(f, "%s %d", t->word, t->df);

        for(Posting *p = t->head; p !=NULL; p = p->next){
            fprintf(f, " %d:%d", p->doc_id, p->freq);
        }
        fprintf(f, "\n");
    }
}

//Check for write error & detects file writing

if (ferror(f)) {
    printf("An error occurred while saving the index.\n");
    fclose(f);
    return;
}

if (fclose(f) != 0) {
    printf("Warning: index may not have been saved completely.\n");
    return;
}

printf("Index saved to index.dat\n");
}
//CHECK Compatibilty with load_index (sofea's part)