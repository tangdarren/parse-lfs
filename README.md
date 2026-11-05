# Parse\_LFS

**Updates:**
- Project updates will be added here.
- Test cases will be released later this week. For now work on parsing the given image files in `sample_files`.

File systems are the backbone of how data is stored, managed, and accessed on a disk. 
The log-structured file system (LFS) was developed in the early 1990s as a solution to common file system inefficiencies, particularly regarding write performance and sequential versus random I/O operations
(see the textbook for why this is the case).
LFS addresses these issues by buffering both data and metadata in memory and subsequently writing them in large sequential segments. This design leverages the disk's sequential bandwidth, significantly improving performance.

In this project, you will develop a parser (`parse_lfs`) that reads an image file representing a log-structured file system on the disk.
Your parser will extract and interpret the underlying structure and metadata from the LFS image and use this information to print the LFS tree. Additionally, your parser should be able to
extract specified files from the LFS image and print their contents.

**Learning Objectives:**
- Learn the basics of file system data structure organization (inode, imap, data block, etc.).
- Understand how files are arranged in a log structured file system.
- Understand how to reconstruct files from the log.

## Background

Understanding the basics of FS data structures is important for this project.

- [File System Implementation](https://pages.cs.wisc.edu/~remzi/OSTEP/file-implementation.pdf)
- [Log-structured File Systems](https://pages.cs.wisc.edu/~remzi/OSTEP/file-lfs.pdf)

## General Idea

Your parser (`parse_lfs`) should take the following arguments on the commandline, an operation to perform, and a LFS image file.
The operation is to either print the LFS tree ("ls") or print the data of a specific file in the LFS image ("cat").
If the `cat` operation is used, an additional argument should be provided to specify which file is being read.
Below are examples of both operations.

**Printing LFS Tree**

```
$ ./bin/parse_lfs ls ./sample_files/a.img
/
         256 2025-04-15 19:00:18 UTC -rw-------    bar.txt
           0 2025-04-15 19:00:18 UTC -rw-------    foo.txt
           0 2025-04-15 19:00:18 UTC -rwxrwxrwx    scary.bin
          30 2025-04-15 19:00:18 UTC drwxr-xr-x    dir
           0 2025-04-15 19:00:18 UTC -rwxr-xr-x        a
          24 2025-04-15 19:00:18 UTC drwxr-xr-x        b
           0 2025-04-15 19:00:18 UTC -rw-r-----            bar.txt
        4096 2025-04-15 19:00:18 UTC -rw-r--r--            foo.txt
          26 2025-04-15 19:00:18 UTC drwxr-xr-x        c
           0 2025-04-15 19:00:18 UTC -rwxr-xr-x            a.txt
           0 2025-04-15 19:00:18 UTC -rwxr-xr-x            b.csv
          15 2025-04-15 19:00:18 UTC drwxr-xr-x            d
       65536 2025-04-15 19:00:18 UTC -rw-------                sample.txt
          13 2025-04-15 19:00:18 UTC drwxr-xr-x        new_dir
         512 2025-04-15 19:00:18 UTC -rw-------            data.txt
```

The output of `parse_lfs ls` should be formatted as above. We provide a `ls_print_file()` function in `./lib` which handles
the string formatting given data for one line. You will need to account for the depth of each file in the tree so that
the indenting of the directory structure is correct.

Additionally, the files should be sorted alphabetically with directories listed last for each depth level.

**Printing File**
```
$ ./bin/parse_lfs cat /bar.txt ./sample_files/a.img
revised to the end of modernizing
and improving them".   The grand jury commented on a number
of other topics, among them the Atlanta and Fulton County purchasing
departments which it said "are well operated and follow generally
accepted practices
```

`parse_lfs cat` should search the LFS image tree using the given filepath and print the data contents of the file.
Make sure to only print the contents of the file and not left over bytes in the data block!

You can assume that the file path given will start at root (example: `/dir/foo/bar.txt`.) However, your parser should **logically** support
`.` and `..` in the file path (`.` and `..` files are not actually in the image!). So all of the below paths are valid:

```
$ ./bin/parse_lfs cat /dir/b/bar.txt ./sample_files/a.img
$ ./bin/parse_lfs cat /dir/./b/bar.txt ./sample_files/a.img
$ ./bin/parse_lfs cat /dir/c/../b/bar.txt ./sample_files/a.img
```

**Error handling**

If `./parse_lfs cat` is called on a non-existent file or bad file path your program should return 1 and print to stderr:

```
parse_lfs error: Failed to find file.
```

## Data Layout

### Basics

The LFS image is comprised of 4 KB data blocks and 1 KB meta data blocks (CR, Imap, Inode).
The max size an LFS image can be is 1 GB.

Inodes only point to direct blocks, indirect blocks are not present, and all disk\_offsets are in units of bytes.

**You can assume that the root directory "/" is always inumber 0!**

Square brackets are for formatting clarity, these are not actually
written in the binary file!

### Checkpoint Regions (CR)

Each image file begins with a checkpoint region (CR) (1 KB)

```
uint Image_Offset  // Offset to current end of image log.
uint Entry_Count   // Number of entries in CR

// CR Entry corresponding to Imap, repeats Count times
uint inumber_start       
uint inumber_end         
uint imap_disk_offset    
    .
    .
    .

```

Each entry in the CR tells us where a corresponding Imap
is on the disk image and which inodes are mapped within 
[inumber\_start, inumber\_end] \(inclusive\).

So if we see an entry with `[4,7] 55296`, then our
Imap located at file image offset `55296` bytes _can_ hold
inumber to file image offset mappings for inodes `4`, `5`, `6`, and `7`.

### Imaps

Each Imap resides in a 1 KB region in the file.

```
uint Entry_Count   // Number of entries in Imap

// Imap Entry corresponding to Inode, repeats Count times
uint inumber             
uint inode_disk_offset   
    .
    .
    .

```

So if we were to see an entry `8 72704`, we know that Inode `8` is located
at file image offset `72704`.

### Inodes

Each Inode resides in a 1 KB region in the file.

```
uint    file_cursor              // Offset where writes should be directed to. (not used by parse_lfs)
uint    size                     // Size of file, != total size of data blocks.
mode_t  permissions              // File access permissions and type (regular/directory, etc.)
time_t  mtime                    // File modification time stamp.

uint Direct_Block_Count          // Number of direct blocks pointed to by Inode.

// Each direct block entry (repeats count times).
uint direct_block_disk_offset    // Block 0, Offset units are in bytes.
uint direct_block_disk_offset    // Block 1, Offset units are in bytes.
    .
    .
    .
```

### Data Blocks

Data blocks are allocated as 4 KB regions. You can assume files are not
internally fragmented (e.g. a data block is fully used before a new one is allocated for a file).
**However, the last data block of a file may only be partially filled.**

Data is first written at the start of the block (smallest image offset), and subsequent writes
go to increasing offsets until the block is full and a new block is allocated. (You won't need
to do this, but may help you visualize the data layout).

A regular file can have any data values inside its data blocks. However directories follow
a specific format with their data blocks:

```
char fileName[]         // Not null terminated! Instead ends with ','
uint inumber
```

So given a directory structure like:
```
          30 2025-04-15 19:56:24 UTC drwxr-xr-x    dir
           0 2025-04-15 19:56:24 UTC -rwxr-xr-x        a
          24 2025-04-15 19:56:24 UTC drwxr-xr-x        b
           0 2025-04-15 19:56:24 UTC -rw-r-----            bar.txt
        4096 2025-04-15 19:56:24 UTC -rw-r--r--            foo.txt
          26 2025-04-15 19:56:24 UTC drwxr-xr-x        c
```

`dir`'s data block would contain:

```
a,5
b,6
c,7
```

And `b`'s data block would contain:

```
foo.txt,10
bar.txt,11
```

Using arbitrary inumbers here. Also **entries are not guaranteed to be sorted**, you will need to sort your files when printing the tree.


### Putting it all together

**Looking up the first data block of a file by inumber**

Let's say we want to access the first data block of inode 7. We will assume this file actually exists in the image.
- Starting with the CR, look through the entries and find the offset of the Imap holding inode 7. 
- We find CR entry `[4,7] 55296`, so our Imap is located at offset `55296`.
- We seek to `55296` and read in the Imap structure located there.
- We find an entry in the Imap: `7 54272`. We now know our inode is located at offset `54272`.
- We seek to `54272` and read in the Inode struct.
- Our Inode holds a list of block offsets and we find our first one is `40960`.
- We can now seek to `40960` and access our data.


## Writing your Solution

Starting off, your entire LFS image will be on disk. You can write your parser to purely use the on disk format when performing
operations, but for your sanity we'd recommend reading in the image first into your own in-memory data structures. 

Here is one path for approching the project:
- Read in and sanity check the CR at the start of the image.
- Repeat for the Imaps and Inodes.
- You should now have all the data needed for the `ls` and `cat` operations.

Start testing with `simple.img` as it contains only the root directory and files and no subdirectories.

### Grading

We will be testing your parser with a variety of LFS images for correctness. We will only use fully consistent images, but
we may try to access non-existent files. We will also test for memory leaks.

## Administrivia 
- **Due Date** by April 28, 2025 at 11:59 PM
- Questions: We will be using Piazza for all questions.
- Collaboration: You may work with a partner for this project. Even if you don't, you must submit a partners.csv file with the cslogins of
  both individuals in your group (just your login under cslogin1 if working alone) when you turn in the project in the
  top-level directory of your submission. Copying
  code (from other groups) is considered cheating. [Read
  this](http://pages.cs.wisc.edu/~remzi/Classes/537/Spring2018/dontcheat.html)
  for more info on what is OK and what is not. Please help us all have
  a good semester by not doing this.
- This project is to be done on the [lab
  machines](https://csl.cs.wisc.edu/docs/csl/2012-08-16-instructional-facilities/),
  so you can learn more about programming in C on a typical UNIX-based
  platform (Linux).
- A few sample tests are provided in the project repository. To run
  them, execute `run-tests.sh` in the `tests/` directory. Try
  `run-tests.sh -h` to learn more about the testing script. Note these
  test cases are not complete, and you are encouraged to create more
  on your own.
- **No Slip Days!**:
  - **There are no slip days for this assignment.** Instead, the late penalty is 10%
    per day as opposed to the usual 20%.
  - Example project directory structure. (some files are omitted)
  ```
  p6/
  ├─ solution/
  │  ├─ parse_lfs.c
  │  ├─ parse_lfs.h
  ├─ tests/
  ├─ ...
  ├─ partners.csv
  ```

## Submitting your work
- Run `submission.sh` 
- Download generated tar file
- Upload it to Canvas
  * Links to Canvas assignment: 
  * [Prof. Mike Swift's class](https://canvas.wisc.edu/courses/434150/assignments/2659202)
  * [Prof. Ali Abedi's class](https://canvas.wisc.edu/courses/434155/assignments/2659204)

