#include <iostream>

extern "C" void crash_here() {
    std::cout << "inside crash_here()" << std::endl;

    int* p = nullptr;
    *p = 123;
}

extern "C" void level_three() {
    std::cout << "inside level_three()" << std::endl;
    crash_here();
}

extern "C" void level_two() {
    std::cout << "inside level_two()" << std::endl;
    level_three();
}

extern "C" void level_one() {
    std::cout << "inside level_one()" << std::endl;
    level_two();
}

int main() {
    std::cout << "inside main()" << std::endl;
    level_one();
    return 0;
}