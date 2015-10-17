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

size_t getFilesize(const char* filename) {
    struct stat st;
    stat(filename, &st);
    return st.st_size;   
}

void constructMap(const char* mmappedData, size_t filesize, 
    std::unordered_map<unsigned int, char* const>& mapKeyPointer) {

    // from mmap, create a pair of values from each line, 
    // and place every pairing into the hash map
    const char* const endOfFile = mmappedData + filesize;
    while(mmappedData && mmappedData != endOfFile) {

        // grab key
        unsigned int key = atoi(mmappedData);

        // iterate pointer until a whitespace
        while((mmappedData != static_cast<char*>(
            memchr(mmappedData, ' ', endOfFile - mmappedData))))
            mmappedData++;

        // move one past the whitespace to value
        mmappedData++;

        // make pairs and insert into hash map
        std::pair<unsigned int, char* const> keyPointerPair = std::make_pair(
            key, const_cast<char* const>(mmappedData));
        mapKeyPointer.insert(keyPointerPair);

        // jump to next key
        mmappedData += 11;
    }
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

    // store an unchanging pointer to the start so we can reference it from munmap
    char* const mmappedDataStart = mmappedData;

    // create and construct local unordered hash map
    std::unordered_map<unsigned int, char* const> mapKeyPointer;
    constructMap(mmappedData, filesize, mapKeyPointer);

    // prompt user for valid input and store result in file
    while(true) {

        // prompt user for input
        std::cout << "\"exit\" or \"x y\" to create a mapping x -> y" << std::endl;
        std::string input = "";
        getline(std::cin, input);
        std::istringstream iss(input);

        unsigned int x = 0;
        unsigned int y = 0;

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
        if (!(iss >> x >> y)) {
            std::cout << "error: could not parse two numbers" << std::endl;
            continue;
        }

        // check that x is in range
        if (x > 65535) {
            std::cout << "error: x is out of range" << std::endl;
            continue;
        }

        // get length of value, create string that will be added to file
        std::string value = std::to_string(y);

        // pad the end with spaces to make it 10 characters
        int length = value.length();
        for (int i = 0; i < (10 - length); ++i) {
            value.append(" ");
        }

        // c++ strings to c strings
        std::string result = std::to_string(x) + " " + value;
        result.append("\n");
        const char* data = result.c_str();

        // look for key in hashmap
        auto found = mapKeyPointer.find(x);

        // if the key does not exist in the hashmap
        if (found == mapKeyPointer.end()) {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // write key value pair to end of file
            std::ofstream ofs(argv[1], std::ios::out|std::ios::app);
            ofs << data;
            int written = std::strlen(data);
            ofs.close();

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
            filesize += written;

            // mmap again
            mmappedData = static_cast<char*>(
                mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0));
            if (mmappedData == NULL) {
                std::cerr << "error: could not memory map file" << std::endl;
                exit(EXIT_FAILURE);
            }

            // update flag
            isEmpty = false;

            // erase and reconstruct hashmap
            mapKeyPointer.clear();
            constructMap(mmappedData, filesize, mapKeyPointer);
        }

        // if the key exists in the hashmap
        else {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // overwrite each character in memory
            char* const valueAddress = mapKeyPointer[x];
            const char* const valueChar = value.c_str();
            for (int i = 0; i < 10; ++i) 
                valueAddress[i] = valueChar[i];

            // release lock
            flock(fd, LOCK_UN);
        }
    }
}
