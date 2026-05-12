#include <iostream>

int main() {
    int* p = nullptr;
    std::cout << "Before crash\n";
    *p = 10;  // nullptr에 쓰기 시도 → SIGSEGV 발생 가능
    std::cout << "After crash\n";
    return 0;
}