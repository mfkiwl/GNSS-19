// rangefile_parser.cpp: 定义应用程序的入口点。
//

#include <stdio.h>
#include "rangefile_parser.h"

using namespace std;

int get_file_size(const char * filename) // path to file
{
    FILE* p_file = NULL;
    p_file = fopen(filename, "rb");
    fseek(p_file, 0, SEEK_END);
    int size = ftell(p_file);
    fclose(p_file);
    return size;
}

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        const char* file_path = argv[1];
        printf("file_path: %s\r\n", file_path);
        int file_length = get_file_size(file_path);
        printf("file_length: %d\r\n", file_length);
    } else {
    
    }

	return 0;
}
