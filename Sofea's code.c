// My part: memory cleanup
// add #include <sys/stat.h> for stat() and S_ISREG() functions
//-----MEMORY CLEANUP-----
// free the whole index: every word, its list of documents, and the word itself

static void free_index(void) {
    for (int i = 0; i < HASH_SIZE; i++) {
        Term *t = idx.buckets[i];
        while (t) {
            Term *next_term = t->next; // remember next word first
            Posting *p = t->head;
            while (p) {
                Posting *next_post = p->next; // remember next posting first
                free(p);
                p = next_post;
            } 
            free(t->word);
            free(t);
            t = next_term;
            
        }
        idx.buckets[i] = NULL; // bucket is now empty
    }
    idx.unique_terms = idx.total_postings = idx.total_terms = 0;
    index_built = 0; // searching is blocked
}


// free the collection of documents and reset its state
static void free_collection(void) {
    free(coll.docs); 
    coll.docs = NULL;
    coll.count = coll.capacity = 0;
    coll.folder[0] = '\0'; // clear the folder name
    
}

// forget the last search so the report cannot show old results
static void clear_last_results(void) {
    free(last_results);
    last_results = NULL;
    last_count = 0;
    last_nterms = 0;
    last_query[0] = '\0';
}

//-----LOADING AND BUILDING-----
// sort file names the way a person would: 1, 2, 10 (not 1, 10, 2)
static int cmp_docs_by_name(const void *a, const void *b) {
    const char *x = ((const Document *)a)->filename;
    const char *y = ((const Document *)b)->filename;
    const char *s = x, *t = y;

    while (*s && *t) {
        if (isdigit((unsigned char)*s) && isdigit((unsigned char)*t)) {
            //compare numbers
            while (*s == '0') s++; // ignore leading zeros
            while (*t == '0') t++;
            const char *s_start = s, *t_start = t;
            while (isdigit((unsigned char)*s)) s++;
            while (isdigit((unsigned char)*t)) t++;
            size_t ls = (size_t)(s - s_start);
            size_t lt = (size_t)(t - t_start);
            if (ls != lt) {
                return (ls < lt) ? -1 : 1;  
            }
            int cmp = strncmp(s_start, t_start, ls);
            if (cmp != 0) {
                return cmp;
            }
        }   else {
            if (*s != *t) {
                return (unsigned char)*s - (unsigned char)*t;
            }
            s++;
            t++;
        }
    }
    if (*s || *t) {
        return (*s) ? 1 : -1; // shorter name comes first
    }
    return strcmp(x, y); // still equal? use strcmp
}

/* ask for a folder, find all real files with .txt extension, sort them and give them IDs
   the old documents are only replaced if the new folder loads successfully*/
static void load_collection(void) {
    char folder[512];
    if (!read_line("Enter folder path containing .txt files: ", folder, sizeof folder)){
        return;
    }
    
    //remove unnecessary trailing spaces and slashes
    //example: "C:\Users\Documents\MyFolder\  " becomes "C:\Users\Documents\MyFolder"
    size_t len = strlen(folder);
    while (len > 0 && isspace((unsigned char)folder[len - 1])) {
        folder[--len] = '\0';
    }
    while (len > 1 && (folder[len - 1] == '/' || folder[len - 1] == '\\')) {
        folder[--len] = '\0';
    }
    if (len == 0) {
        printf(" Folder path cannot be empty.\n");
        return;
    }
    
    DIR *d = opendir(folder);
    if (!d) {
        printf(" Cannot open folder '%s'.\n", folder);
        printf(" %s\n", coll.count ? "Previous collection kept." : "");
        return;
    }
    
    //Collect the files in a temporary list
    Document *docs = NULL;
    int count = 0;
    int capacity = 0;
    int skip_long = 0;
    int skip_notfile = 0;
    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        if (!has_txt_extension(e->d_name)) continue; // not a .txt name
        if (strlen(e->d_name) >= NAME_LEN) { // name too long
            skip_long++;
            continue;
        }

        char path[PATH_LEN];
        snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
        
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) { // not a regular file
            skip_notfile++;
            continue;
        }   

        //if the array is full, double it.
        if (count >= capacity) {
            int newcap = capacity ? capacity * 2 : 64;
            Document *nd = realloc(docs, (size_t)newcap * sizeof *nd);
            if (!nd) {
                printf(" Out of memory.\n");
                free(docs);
                closedir(d);
                return;
            }

            docs = nd;
            capacity = newcap;

        }
        Document *doc = &docs[count++];
        memset(doc, 0, sizeof *doc);
        snprintf(doc->filename, sizeof doc->filename, "%s", e->d_name);
        snprintf(doc->path, sizeof doc->path, "%s", path);

    }  
    closedir(d);

    if (count == 0) {
        printf(" No .txt files found in '%s'.\n", folder);
        printf(" %s\n", coll.count ? "Previous collection kept." : "");
        free(docs);
        return;
    }
    
    //sort the files and assign IDs 
    qsort(docs, (size_t)count, sizeof *docs, cmp_docs_by_name);
    for (int i = 0; i < count; i++) {
        docs[i].id = i;
    }

    // loading successful, replace the old collection
    free_index();
    free_collection();
    clear_last_results();
    coll.docs = docs;
    coll.count = count;
    coll.capacity = capacity;
    snprintf(coll.folder, sizeof coll.folder, "%s", folder);

    printf(" Found %d .txt document(s) in '%s'.\n", count, folder);
    if (skip_long) {
        printf(" Warning: %d file(s) skipped because the name is too long.\n", skip_long);
    }
    if (skip_notfile) {
        printf(" Warning: %d file(s) skipped because they are not regular files.\n", skip_notfile);
    }
    if (coll.count == 0) {
        printf(" Warning: collection is empty.\n");
    } else { 
        printf(" Now choose 'Build Index'.\n");
    }
}



// build the index from the loaded collection of documents

static void build_index(void){
    if(coll.count == 0){
        printf(" Load a collection first.\n");
        return;
    }
    
    free_index(); // start with empty index
    clear_last_results(); // old search results are no longer valid
    
    clock_t t0 = clock(); // start the timer
    int skipped = 0;
    int empty = 0;
    size_t word_bytes = 0; // for the memory usage report
    
    for (int i = 0; i < coll.count; i++){
        Document *doc = &coll.docs[i];
        doc->total_terms = doc->unique_terms = 0;
        
        long len;
        char *text = read_file(doc->path, &len);
        if(!text){
            printf(" Warning: cannot read '%s' - skipped.\n", doc->filename);
            skipped++;
            continue;
        }
    
    
        const char *p = text;
        char tok[MAX_WORD];
        while (next_token(&p, tok)) {
            if (use_stopwords && is_stopword(tok)) {
                continue;
            }
        
            int created = 0;
            Term *t = find_or_create_term(tok, &created);
            int r = t ? add_occurrence(t, doc->id) : -1;
            if (r < 0) {
                printf(" Out of memory while indexing '%s'. Index discarded.\n", doc->filename);
                free(text);
                free_index();
                return;
            }
            if (created) {
                word_bytes += strlen(tok) + 1; //+1 for null terminator
            }
            if (r == 1) {
                doc->unique_terms++;
            }

            doc->total_terms++;
            idx.total_terms++;

        }
        free(text);
        if (doc->total_terms == 0) { 
            printf(" Warning: '%s' is empty (no indexable terms).\n", doc->filename);
            empty++;
        }
    }
    index_built = 1;
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    
    double mem_kb = ((double)sizeof idx.buckets + (double)idx.unique_terms * sizeof(Term) + (double)word_bytes + (double)idx.total_postings * sizeof(Posting)) / 1024.0;
    

    printf(" Index built in %.4f s\n", secs);
    printf("  Documents  : %d (%d skipped, %d empty)\n", coll.count, skipped, empty);
    printf("  Total terms: %ld\n", idx.total_terms);
    printf("  Unique terms: %ld\n", idx.unique_terms);
    printf("  Postings   : %ld\n", idx.total_postings);
    printf("  Memory used: %.2f KB\n", mem_kb);
}