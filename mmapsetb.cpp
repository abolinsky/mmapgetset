#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>

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
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        std::cerr << "error: file could not be opened" << std::endl;
        exit(EXIT_FAILURE);
    }   

    // get size of the file and set empty flag if empty
    size_t filesize = getFilesize(argv[1]);
    bool isEmpty = false;
    if (filesize == 0) isEmpty = true;

    // execute mmap:
    char* mmappedData = static_cast<char*>(
        mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0));
    if (mmappedData == NULL) {
        std::cerr << "error: could not memory map file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // prompt user for valid input and store result in file
    while(true) {

        // store an unchanging pointer to the start so we can reference it from munmap
        char* const mmappedDataStart = mmappedData;

        // prompt user for input
        std::cout << "\"exit\" or \"x y\" to create a mapping x -> y" << std::endl;
        std::string input = "";
        getline(std::cin, input);
        std::istringstream iss(input);

        uint32_t key = 0;
        uint32_t value = 0;

        // check for user exit
        if (input == "exit") {

            // unmap
            if (!isEmpty) {
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

        // check to make sure input is in valid format
        if (input.at(0) == ' ') {
            std::cout << "error: do not include spaces before the first number";
            std::cout << std::endl;
            continue;
        }

        // check for two numbers
        if (!(iss >> key >> value)) {
            std::cout << "error: could not parse two numbers" << std::endl;
            continue;
        }

        // check that x is in range
        if (key > 65535) {
            std::cout << "error: x is out of range" << std::endl;
            continue;
        }

        // interpret the memory mapped file as an array of 8 byte pairs
        const PairArray* const pairArray = reinterpret_cast<PairArray*>(mmappedData);

        // binary search mmapped file
        uint32_t* found = nullptr;
        binarySearch(pairArray, filesize/8, key, found);

        // if the key exists in memory
        if (found != nullptr) {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // overwrite value
            *found = value;

            // release lock
            flock(fd, LOCK_UN);

        } else {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // write key value pair to end of file as 2 and 4 bytes
            std::ofstream ofs(argv[1], std::ios::binary|std::ios::out|std::ios::app);
            ofs.write(reinterpret_cast<char*>(&key), sizeof(uint32_t));
            ofs.write(reinterpret_cast<char*>(&value), sizeof(uint32_t));
            ofs.close();

            //
            // sort the memory so it is in order
            //

            // release lock
            flock(fd, LOCK_UN);

            // unmap
            if (!isEmpty) {
                int rc = munmap(mmappedDataStart, filesize);
                if (rc != 0) {
                    std::cerr << "error: could not unmap memory" << std::endl;
                    exit(EXIT_FAILURE);
                }
            }

            // update size of file
            filesize += sizeof(uint64_t);

            // mmap again
            mmappedData = static_cast<char*>(
                mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0));
            if (mmappedData == NULL) {
                std::cerr << "error: could not memory map file" << std::endl;
                exit(EXIT_FAILURE);
            }

            // update flag
            isEmpty = false;
        }   
    }
}
