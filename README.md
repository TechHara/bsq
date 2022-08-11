# bsq
Fast query on static database with binary search

### Quick Start
Say we have an extremely large database as a raw text file
```
$ head db.tsv
Sim,Rillett,srillett0@sphinn.com,Male,4250e9a343e164200c92e331b7bd5110
Ara,Boord,aboord1@hp.com,Female,403b822eec75bbdf90fbc59ca8276c18
Nikolia,Mateu,nmateu2@sakura.ne.jp,Female,0c8ae9f6eac2230d942dec7660553d76
Angelika,Dyzart,adyzart3@weibo.com,Female,61c3f190f5c1708fbb13dd132ce6fdef
Ali,Pobjoy,apobjoy4@nhs.uk,Bigender,f83c5ac33b36e8fbea1941d03941be68
Cate,Edmans,cedmans5@ehow.com,Female,6c4bf1f05705b5a11eaa3dba3b335774
Smitty,Balcock,sbalcock6@flickr.com,Male,d6b8efab3b8a62ae668255c13268312a
Tracey,Askin,taskin7@google.com.br,Male,0f2f1ac556cae69c508023196299778d
Simone,Neller,sneller8@privacy.gov.au,Female,64d97332c144405e5c269486986947ea
Griz,Breffit,gbreffit9@ifeng.com,Male,c39fe6c2e102149ebf4a70d88c4ad5e8
```
where we want to query entries by its key, say the hash value in the column. We could do
```
$ grep d6b8e db.csv
Smitty,Balcock,sbalcock6@flickr.com,Male,d6b8efab3b8a62ae668255c13268312a
```
but this is not scalable, as grep employs a _linear search_ method.

This is where **bsq** comes in to play, where it searches for the key using _binary search_. First we need to sort the database by its key column so that we can perform a binary search.
```
$ sort -k5,5 -s -t, db.tsv -o db.tsv
```
Sorting is an expensive operation and should be performed only once, which is why **bsq** is intended for querying a _static_ database. To query a key, we do
```
$ ./bsq -t, -k5 db.tsv d6b8e
Smitty,Balcock,sbalcock6@flickr.com,Male,d6b8efab3b8a62ae668255c13268312a
```

### Usage
```
$ ./bsq -h
Usage: ./bsq [-t CHAR] [-k N] [-w] [-h] FILE [KEY...]
-t CHAR: column separator. Default: tab
-k N: key column index. Default: 1
-w: exact match only. Default: prefix match
-c: check if the input is sorted. No search is performed
-h: print this message
FILE: input file to be read using mmap. Must be sorted by the key column
KEY: search key(s).  Each key will be searched independently.
Default: read from stdin delimited by LF
```

### Compile
```
# debug: will print debugging info to stderr
g++ -std=c++14 -OO -g bsq.cc -o bsq

# release
g++ -std=c++14 -O3 -DNDEBUG bsq.cc -o bsq
```