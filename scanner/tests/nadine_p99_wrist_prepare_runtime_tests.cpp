#include <iostream>
extern "C" __declspec(dllimport) unsigned p99_wrist_prepare_refusal_fixture();
int main() {
    const auto failures=p99_wrist_prepare_refusal_fixture();
    std::cout<<"Actual PREPARED body rejects own non-retail EXE; failure mask="<<failures<<'\n';
    return failures?1:0;
}
