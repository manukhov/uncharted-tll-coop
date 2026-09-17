#include "nadine_p99_group_budget.h"
#include "nadine_p99_group_scope.h"
#include <iostream>

int main() {
    using coop::p99_group_budget::Budget;
    unsigned checks=0, failures=0;
    const auto check=[&](bool value){++checks;if(!value)++failures;};
    for(std::uint64_t limit=0;limit<32;++limit)
        for(std::uint64_t a=0;a<40;++a)
            for(std::uint64_t b=0;b<40;++b){
                Budget budget{limit};
                const bool first=budget.charge(100,100+a);
                const bool second=budget.charge(500,500+b);
                check(first==(limit!=0 && a<=limit));
                check(second==(limit!=0 && a<=limit && b<=limit-a));
                if(!second)check(!budget.charge(900,900));
            }
    check(coop::p99_group_budget::one_millisecond(10000000)==10000);
    check(coop::p99_group_budget::one_millisecond(999)==0);
    Budget missing{0};check(!missing.charge(0,0));
    Budget reversed{10};check(!reversed.charge(11,10));
    Budget clock_failed{10};check(!clock_failed.charge(0,0,false));
    Budget max{UINT64_MAX};check(max.charge(0,UINT64_MAX));check(!max.charge(0,1));
    Budget corrupt{10,11};check(!corrupt.charge(0,0));

    // Overrun before publication leaves stock authoritative. Late overrun
    // after g0 is explicitly partial, not rollback; never start shadow g1.
    coop::p99_group_scope::State early;
    Budget early_budget{10};
    if(!early_budget.charge(0,11))early.refused=true;
    check(!early.enter(0));check(!early.incomplete());
    coop::p99_group_scope::State late;
    Budget late_budget{10};
    check(late_budget.charge(0,6));check(late.enter(0));late.finish(0);
    if(!late_budget.charge(20,25))late.refused=true;
    check(!late.enter(1));check(late.incomplete());
    Budget next{10};check(!next.failed && next.spent==0);
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
