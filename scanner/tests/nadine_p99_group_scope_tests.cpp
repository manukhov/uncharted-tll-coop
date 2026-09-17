#include "nadine_p99_group_scope.h"
#include <iostream>

int main() {
    using coop::p99_group_scope::State;
    unsigned checks=0,failed=0;
    const auto check=[&](bool value) {++checks;if(!value)++failed;};
    for (unsigned entered=0;entered<8;++entered)
        for (unsigned completed=0;completed<8;++completed)
            for (unsigned refused=0;refused<2;++refused)
                for (unsigned group=0;group<4;++group)
                    for (unsigned first=0;first<2;++first) {
                        State state{static_cast<std::uint8_t>(entered),static_cast<std::uint8_t>(completed),refused!=0};
                        const bool expected=!refused && entered==completed &&
                            ((group==0 && first==1 && completed==0) ||
                             (group==1 && first==0 && completed==1));
                        check(state.can_start(group,first!=0)==expected);
                    }
    State job{};
    check(!job.incomplete()); check(!job.enter(1)); check(job.enter(0));
    check(job.incomplete()); check(!job.enter(0)); check(!job.enter(1));
    job.finish(0); check(job.incomplete()); check(job.enter(1));
    check(job.incomplete()); job.finish(1); check(!job.incomplete());
    check(!job.enter(0)); check(!job.enter(1));
    State no_publish{}; no_publish.refused=true;
    check(!no_publish.enter(0)); check(!no_publish.incomplete());
    State partial{}; check(partial.enter(0)); partial.finish(0); partial.refused=true;
    check(!partial.enter(1)); check(partial.incomplete());
    State exception{}; check(exception.enter(0)); check(exception.incomplete());
    State next_job{}; check(next_job.enter(0)); // No cross-job ticket reuse.
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed?1:0;
}
