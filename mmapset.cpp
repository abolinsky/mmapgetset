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

void constructMap(char* mmappedData, size_t filesize, bool isEmpty, 
    std::unordered_map<unsigned int, char*>& mapKeyPointer) {

    bool firstLine = true;

    // from mmap, create a pair of values from each line, 
    // and place every pairing into the hash map
    char* const endOfFile = mmappedData + filesize;
    while(!isEmpty && mmappedData && mmappedData != endOfFile) {

        // grab key
        unsigned int key = 0;
        if (firstLine) {
            key = atoi(mmappedData + 2);
            firstLine = false;
        } else key = atoi(mmappedData);

        // iterate pointer until a whitespace
        char* pointer = mmappedData;
        while((pointer != static_cast<char*>(memchr(pointer, ' ', endOfFile - pointer))))
            pointer++;

        // move one past the whitespace and move mmappedData to pointer
        pointer++;
        mmappedData = pointer;

        // store pointer to y
        char* yPointer = mmappedData;

        // iterate pointer until a newline
        pointer = mmappedData;
        while((pointer != static_cast<char*>(memchr(pointer, '\n', endOfFile - pointer))))
            pointer++;

        // move one past the endline and move mmappedData to pointer
        pointer++;
        mmappedData = pointer;

        // make pairs and insert into hash map
        std::pair<unsigned int, char*> keyPointerPair = std::make_pair(key, yPointer);
        mapKeyPointer.insert(keyPointerPair);
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

    // get size of the file
    size_t filesize = getFilesize(argv[1]);
    bool isEmpty = false;

    // if file is empty, make sure to mmap at least something
    if (filesize == 0) {
        isEmpty = true;
        ftruncate(fd, 2);
        filesize = getFilesize(argv[1]);
    }

    // execute mmap:
    char* mmappedData = (char*) mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    if (mmappedData == NULL) {
        std::cerr << "error: could not memory map file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // store an unchanging pointer to the start so we can reference it from munmap
    char* const mmappedDataStart = mmappedData;

    // create and construct local unordered hash map
    std::unordered_map<unsigned int, char*> mapKeyPointer;
    constructMap(mmappedData, filesize, isEmpty, mapKeyPointer);

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
            int rc = munmap((void*)mmappedDataStart, filesize);
            if (rc != 0) {
                std::cerr << "error: could not unmap memory" << std::endl;
                exit(EXIT_FAILURE);
            }

            // close file
            close(fd);

            exit(EXIT_SUCCESS);
        }

        // check to make sure input is in valid format
        if (input.at(0) == ' ') {
            std::cout << "error: do not include spaces before the first number" << std::endl;
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

        // check that y is in range
        if (y > 4294967295) {
            std::cout << "error: y is out of range" << std::endl;
            continue;   
        }

        // get length of value, create string that will be added to file
        std::string value = std::to_string(y);

        // pad the end with spaces to make it 10 characters
        ssize_t length = value.length();
        for (int i = 0; i < (10 - length); ++i) {
            value.append(" ");
        }

        // c++ strings to c strings
        char* valueChar = (char*) value.c_str();
        std::string result = std::to_string(x) + " " + value;
        result.append("\n");
        const char* data = result.c_str();

        // look for key in hashmap
        auto found = mapKeyPointer.find(x);

        //
        // if the key does not exist in the hashmap
        //
        if (found == mapKeyPointer.end()) {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // write key value pair to end of file
            std::ofstream ofs(argv[1], std::ios::out|std::ios::app);
            ofs << data;
            ssize_t written = std::strlen(data);
            ofs.close();

            // release lock
            flock(fd, LOCK_UN);

            // unmap
            int rc = munmap((void*)mmappedDataStart, filesize);
            if (rc != 0) {
                std::cerr << "error: could not unmap memory" << std::endl;
                exit(EXIT_FAILURE);
            }

            // update size of file
            filesize += written;

            // mmap again
            mmappedData = (char*) mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
            if (mmappedData == NULL) {
                std::cerr << "error: could not memory map file" << std::endl;
                exit(EXIT_FAILURE);
            }

            // update flag
            isEmpty = false;

            // erase and reconstruct hashmap
            mapKeyPointer.clear();
            constructMap(mmappedData, filesize, isEmpty, mapKeyPointer);
        }

        //
        // if the key exists in the hashmap
        //
        else {

            // spin until file is unlocked, and take lock for yourself
            while(true) {
                int gotLock = flock(fd, LOCK_EX);
                if (gotLock == 0) break;
            }

            // overwrite each character in memory
            char* valueAddress = mapKeyPointer[x];
            for (int i = 0; i < 10; ++i) 
                valueAddress[i] = valueChar[i];

            // release lock
            flock(fd, LOCK_UN);
        }
    }
}
