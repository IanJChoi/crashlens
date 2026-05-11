#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: ./exit_status_demo <number>\n";
        return 1;
    }

    int code = std::atoi(argv[1]);
    if (code == 0) return 0;

    std::exit(code);
}