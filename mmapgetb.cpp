#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <iostream>
#include <sstream>
#include <string>

struct Pair { uint32_t index[]; };
struct PairArray { uint64_t index[]; };

size_t getFilesize(const char* filename) {
    struct stat st;
    stat(filename, &st);
    return st.st_size;   
}

void binarySearch(const PairArray* const pairArray, uint16_t numElements, uint32_t key, uint32_t*& value) {

    int mid = numElements/2;

    if (numElements == 0) return;

    // interpret the pair as an array of 4 byte key and value
    const Pair* pair = reinterpret_cast<const Pair*>(&(pairArray->index[mid]));

    // new pointer to pass into recursive call
    const PairArray* const newPairArray = reinterpret_cast<const PairArray* const>(
        &(pairArray->index[mid + 1]));

    // if key is found, point pointer passed by reference to value
    if (key == pair->index[0]) {
        value = const_cast<uint32_t*>(&pair->index[1]);
        return;
    } 

    // if search key is less than current key, binary search on left subarray
    else if (key < pair->index[0]) {
        binarySearch(pairArray, mid, key, value);
    } 

    // otherwise, binary search on right subarray
    else (numElements%2 == 0) 
            ? binarySearch(newPairArray, mid - 1, key, value)
            : binarySearch(newPairArray, mid, key, value);
}

int main(int argc, char** argv) {

    // check for a single argument 
    if (argc < 2) {
        std::cerr << "usage: mmapset <filename>" << std::endl;
        exit(EXIT_FAILURE);
    }

    // open file
    int fd = open(argv[1], O_RDWR, 0);
    if (fd < 0) {
        std::cerr << "error: file could not be opened" << std::endl;
        exit(EXIT_FAILURE);
    }   

    while(true) {

        // prompt user for input
        std::cout << "\"exit\" or \"x\" to retrieve a value mapped to x" << std::endl;
        std::string input = "";
        getline(std::cin, input);
        std::istringstream iss(input);

        // get size of the file
        size_t filesize = getFilesize(argv[1]);

        // execute mmap:
        char* mmappedData = static_cast<char*>(
            mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0));
        if (mmappedData == NULL) {
            std::cerr << "error: could not memory map file" << std::endl;
            exit(EXIT_FAILURE);
        }

        // store an unchanging pointer to the start so we can reference it from munmap
        char* const mmappedDataStart = mmappedData;

        uint32_t x = 0;

        // check for user exit
        if (input == "exit") { 

            if (filesize != 0) {
                // unmap
                int rc = munmap(mmappedDataStart, filesize);
                if (rc != 0) {
                    std::cerr << "error: could not unmap memory" << std::endl;
                    exit(EXIT_FAILURE);
                }
            }

            // close file
            close(fd);

            exit(EXIT_SUCCESS);
        }

        // if file is empty, return null string
        if (filesize == 0) {
            std::cout << "result: null" << std::endl;
            continue;
        }

        // check for one number
        if (!(iss >> x)) {
            std::cout << "error: could not parse number" << std::endl;
            continue;
        }

        // check that x is in range
        if (x > 65535) {
            std::cout << "error: x is out of range" << std::endl;
            continue;
        }

        // interpret the memory mapped file as an array of 8 byte pairs
        const PairArray* const pairArray = reinterpret_cast<PairArray*>(mmappedData);

        // spin until file is unlocked, and take lock for yourself
        while(true) {
            int gotLock = flock(fd, LOCK_SH);
            if (gotLock == 0) break;
        }

        // binary search for key value pair
        uint32_t* value = nullptr;
        binarySearch(pairArray, filesize/8, x, value);
        (value == nullptr)
            ? std::cout << "null" << std::endl 
            : std::cout << *value << std::endl;

        // release lock
        flock(fd, LOCK_UN);
    }
}
