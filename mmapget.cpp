#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <iostream>
#include <sstream>
#include <string>

size_t getFilesize(const char* filename) {
    struct stat st;
    stat(filename, &st);
    return st.st_size;   
}

std::string getX(const char* mmappedData, const size_t filesize, const unsigned int x) {

    std::string result = "";

    std::string s = std::to_string(x);
    const char* input = s.c_str();
    int inputLength = std::strlen(input);

    // search every line in mmapped file for key
    const char* const endOfFile = mmappedData + filesize;
    while(mmappedData && mmappedData != endOfFile) {

        unsigned int key = atoi(mmappedData);
        if (key == x) {

            // jump to value
            mmappedData = mmappedData + inputLength + 1;

            // find range of character for value
            int range = 0;
            const char* pointer = mmappedData;
            while((pointer != static_cast<char*>(memchr(mmappedData, ' ', endOfFile - mmappedData)))) {
                pointer++;
                range++;
            }
    
            // create a string from that range
            result = std::string(mmappedData, range);

            return result;
        }

        // jump at least the length of the value
        mmappedData += 10;
        while((mmappedData != static_cast<char*>(memchr(mmappedData, '\n', endOfFile - mmappedData))))
            mmappedData++;
        mmappedData++;
    }

    return "null";
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
        char* mmappedData = static_cast<char*>(mmap(NULL, filesize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0));
        if (mmappedData == NULL) {
            std::cerr << "error: could not memory map file" << std::endl;
            exit(EXIT_FAILURE);
        }

        // store an unchanging pointer to the start so we can reference it from munmap
        char* const mmappedDataStart = mmappedData;

        unsigned int x = 0;

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

        // spin until file is unlocked, and take lock for yourself
        while(true) {
            int gotLock = flock(fd, LOCK_EX);
            if (gotLock == 0) break;
        }

        std::string result = getX(mmappedData, filesize, x);

        // release lock
        flock(fd, LOCK_UN);

        // give user result
        std::cout << "result: " << result << std::endl;
    }
}
