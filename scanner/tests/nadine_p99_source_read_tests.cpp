#include "nadine_p99_source_read.h"
#include "nadine_p99_group_memory.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace s = coop::p99_source_read;
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
struct Fixture {
    std::uintptr_t base;
    std::vector<std::byte> data = std::vector<std::byte>(0x12000);
    s::Contract contract{};
    std::uintptr_t slot, retained, player, fg, art, raw, pose, controller, grenade, weapon, handle, parent;
    std::uintptr_t guard = 0, turn_address = 0;
    std::uint32_t turn_value = 0;
    unsigned turn_on = 0, hits = 0, copies = 0, queries = 0;
    explicit Fixture(std::uintptr_t origin = 0x100000) : base(origin), slot(base), retained(base+0x1000),
        player(base+0x2000), fg(base+0x8000), art(base+0x9000), raw(base+0xA000), pose(base+0xB000),
        controller(base+0xC000), grenade(base+0xD000), weapon(base+0xE000), handle(base+0xF000), parent(base+0x10000) {
        contract = {slot, 0x142A4BFE8, 0x142A47F90, 0x142A227A0, 0x142A9BF48};
        put(slot, retained); put(slot+8, std::uint32_t{0x123}); put(retained+8, player);
        put(player, contract.player_vtable); put(player+0xBC, std::uint32_t{0x123});
        put(player+0x170, contract.look); put(player+0x1A8, fg);
        put(player+0x1D88, controller); put(player+0x1E50, grenade);
        put(player+0x140, std::array<float,4>{0,0,0,1});
        put(player+0x240, std::array<float,3>{1,2,3});
        put(fg+6, std::uint8_t{2}); put(fg+0x18, art); put(fg+0x100, pose);
        put(fg+0x1A8, retained); put(fg+0x1B0, std::uint32_t{0x123});
        put(art+0x24, contract.art_id); put(art+0x34, std::uint32_t{580}); put(art+0x40, raw);
        put(raw+4, contract.skeleton_id); put(raw+0x10, std::uint16_t{580});
        put(controller, contract.controller_vtable); put(controller+0x108, player);
        put(controller+0x120, std::int32_t{2}); put(controller+0x134, std::uint32_t{2});
        put(controller+0x168, handle); put(controller+0x170, std::uint32_t{0x456});
        put(grenade, contract.grenade_vtable); put(grenade+0x108, player);
        put(handle+8, weapon); put(weapon, contract.weapon_vtable); put(weapon+0x72, std::uint8_t{0x35});
        put(weapon+0xBC, std::uint32_t{0x456}); put(weapon+0x860, contract.pistol_id);
        put(weapon+0x4E0, parent); put(parent+8, player);
    }
    template<class T> void put(std::uintptr_t address, const T& value) {
        std::memcpy(data.data()+(address-base), &value, sizeof(value));
    }
    bool query(std::uintptr_t address, coop::p99_group_readability::Region& out) {
        ++queries;
        if (address < base || address-base >= data.size()) return false;
        const auto page = address & ~std::uintptr_t{0xFFF};
        out = {page, 0x1000, true, true, page == guard};
        return true;
    }
    bool copy(std::uintptr_t address, void* output, std::size_t bytes) {
        ++copies;
        if (address < base || bytes > data.size() || address-base > data.size()-bytes) return false;
        if (address == turn_address && ++hits == turn_on) put(turn_address, turn_value);
        std::memcpy(output, data.data()+(address-base), bytes);
        return true;
    }
    bool capture(s::Owner& result) {
        s::Reader reader([&](auto address, auto& out){ return query(address,out); },
                         [&](auto address, auto* out, auto bytes){ return copy(address,out,bytes); });
        return s::owner(reader, contract, player, result);
    }
};

bool live_copy(std::uintptr_t address, void* output, std::size_t count) noexcept {
    __try {
        const auto* from = reinterpret_cast<const volatile std::byte*>(address);
        auto* to = static_cast<std::byte*>(output);
        for (std::size_t i=0;i<count;++i) to[i]=from[i];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct Allocation {
    void* pointer = nullptr;
    ~Allocation(){ if(pointer) VirtualFree(pointer,0,MEM_RELEASE); }
};

// These are lexical wiring tripwires, not a C++ control-flow proof. They ensure
// this fixture cannot pass while a known source caller silently drops purpose.
std::string body(const std::string& source, const std::string& signature) {
    const auto start=source.find(signature);
    if(start==std::string::npos) return {};
    const auto first=source.find('{',start);
    if(first==std::string::npos) return {};
    unsigned depth=0;
    bool quoted=false, character=false, line_comment=false, block_comment=false, escape=false;
    for(std::size_t i=first;i<source.size();++i){
        const char c=source[i], next=i+1<source.size()?source[i+1]:0;
        if(line_comment){ if(c=='\n')line_comment=false; continue; }
        if(block_comment){ if(c=='*'&&next=='/'){block_comment=false;++i;} continue; }
        if(quoted||character){
            if(escape){escape=false;continue;} if(c=='\\'){escape=true;continue;}
            if(quoted&&c=='"')quoted=false; else if(character&&c=='\'')character=false;
            continue;
        }
        if(c=='/'&&next=='/'){line_comment=true;++i;continue;}
        if(c=='/'&&next=='*'){block_comment=true;++i;continue;}
        if(c=='"'){quoted=true;continue;} if(c=='\''){character=true;continue;}
        if(c=='{')++depth;
        if(c=='}'&&--depth==0)return source.substr(first,i-first+1);
    }
    return {};
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

// The runtime adapter only dispatches legacy vs. owned reads. Exercise the
// actual shared Reader primitive in its four new callback-free range shapes.
// Counts below are query counts for explicit synthetic layouts, not timings.
void range_batch_scopes() {
    struct Scope {
        std::array<std::size_t, 4> sizes;
        std::size_t count;
    };
    const std::array<Scope, 4> scopes{{
        {{{0x48, 0x24, 473 * 48, 0}}, 3}, // Art/raw/reference.
        {{{0x40, 0x2E, 93 * 46, 96 * 12}}, 4}, // Post-lookup entry.
        {{{0x40, 0x2E, 71 * 46, 0}}, 3}, // Post-lookup row124.
        {{{93 * 46, 71 * 46, 430 * 48, 426 * 48}}, 4}, // Copy sources.
    }};
    constexpr std::uintptr_t base = 0x100000;
    constexpr std::size_t stride = 0x8000;
    for (const auto& scope : scopes) {
        unsigned queries = 0, copies = 0;
        auto copy = [&](auto, auto*, auto) { ++copies; return true; };
        auto same_region = [&](auto, auto& region) {
            ++queries;
            region = {base, 0x40000, true, true, false};
            return true;
        };
        for (std::size_t i = 0; i < scope.count; ++i) {
            s::Reader independent(same_region, copy);
            check(independent.range(base + i * stride, scope.sizes[i]),
                  "independent full source span admitted");
        }
        check(queries == scope.count && copies == 0,
              "old same-region admission queries once per span without payload");
        queries = 0;
        {
            s::Reader batch(same_region, copy);
            for (std::size_t i = 0; i < scope.count; ++i)
                check(batch.range(base + i * stride, scope.sizes[i]),
                      "batched same-region full source span admitted");
            check(queries == 1 && batch.query_count() == 1 && copies == 0,
                  "one callback-free scope shares one region observation");
        }
        queries = 0;
        auto distinct_regions = [&](auto address, auto& region) {
            ++queries;
            region = {base + ((address - base) / stride) * stride,
                      stride, true, true, false};
            return true;
        };
        {
            s::Reader batch(distinct_regions, copy);
            for (std::size_t i = 0; i < scope.count; ++i)
                check(batch.range(base + i * stride, scope.sizes[i]),
                      "distinct source allocation is admitted independently");
            check(queries == scope.count && copies == 0,
                  "batch does not assume distinct allocations share pages");
        }
        for (std::size_t guarded_index = 0; guarded_index < scope.count; ++guarded_index) {
            queries = 0;
            auto guarded_region = [&](auto address, auto& region) {
                const auto index = (address - base) / stride;
                ++queries;
                region = {base + index * stride, stride, true, true,
                          index == guarded_index};
                return true;
            };
            s::Reader batch(guarded_region, copy);
            for (std::size_t i = 0; i < guarded_index; ++i)
                check(batch.range(base + i * stride, scope.sizes[i]),
                      "prefix before guarded source remains admissible");
            check(!batch.range(base + guarded_index * stride, scope.sizes[guarded_index]) &&
                  queries == guarded_index + 1 && copies == 0,
                  "guard at every source position refuses without payload");
            check(!batch.range(base, 1) && queries == guarded_index + 1,
                  "failed range batch cannot retry an earlier source");
        }
        for (std::size_t i = 0; i < scope.count; ++i) {
            queries = 0;
            const auto start = base + i * stride;
            const auto last = start + scope.sizes[i] - 1;
            auto guarded_tail = [&](auto address, auto& region) {
                ++queries;
                region = address < last
                    ? coop::p99_group_readability::Region{start, scope.sizes[i] - 1, true, true, false}
                    : coop::p99_group_readability::Region{last, 1, true, true, true};
                return true;
            };
            s::Reader batch(guarded_tail, copy);
            check(!batch.range(start, scope.sizes[i]) && queries == 2 && copies == 0,
                  "every complete span includes its final guarded byte");
        }
        queries = 0;
        bool guarded = false;
        auto changing_region = [&](auto, auto& region) {
            ++queries;
            region = {base, 0x40000, true, true, guarded};
            return true;
        };
        {
            s::Reader before_callback(changing_region, copy);
            for (std::size_t i = 0; i < scope.count; ++i)
                check(before_callback.range(base + i * stride, scope.sizes[i]),
                      "pre-boundary scope sees initial accessible region");
        }
        guarded = true; // Simulated native callback boundary changes protection.
        {
            s::Reader after_callback(changing_region, copy);
            check(!after_callback.range(base, scope.sizes[0]) && queries == 2 && copies == 0,
                  "new post-callback scope queries again and observes guard");
        }
    }
}

void owner_phase_scopes() {
    Fixture separate;
    std::uintptr_t player = 0;
    s::Owner first{}, second{};
    {
        s::Reader reader([&](auto a, auto& r) { return separate.query(a, r); },
                         [&](auto a, auto* p, auto n) { return separate.copy(a, p, n); });
        check(s::retained_player(reader, separate.slot, player), "separate retained phase");
    }
    check(separate.capture(first) && separate.capture(second), "separate complete owner snapshots");
    const auto separate_queries = separate.queries;
    const auto separate_copies = separate.copies;

    Fixture shared;
    const auto original = shared.data;
    {
        s::Reader reader([&](auto a, auto& r) { return shared.query(a, r); },
                         [&](auto a, auto* p, auto n) { return shared.copy(a, p, n); });
        check(s::retained_player(reader, shared.slot, player), "shared retained still reads full tuple");
        check(s::owner(reader, shared.contract, player, first), "shared first owner snapshot");
        const auto first_queries = shared.queries;
        check(s::owner(reader, shared.contract, player, second), "shared second owner snapshot");
        check(shared.queries == first_queries && shared.queries < separate_queries,
              "callback-free owner snapshots reuse only page queries");
        check(shared.copies == separate_copies && shared.data == original,
              "all fresh payload observations preserved byte-for-byte");
        shared.put(shared.weapon + 0xBC, std::uint32_t{999});
        const auto before_copies = shared.copies;
        check(!s::owner(reader, shared.contract, player, second) && second.player == 0 &&
              shared.copies > before_copies && shared.queries == first_queries,
              "cached page certificate cannot hide changed weapon generation");
    }
    for (const auto page : std::array<std::uintptr_t, 12>{0, 0x1000, 0x2000, 0x8000,
            0x9000, 0xA000, 0xB000, 0xC000, 0xD000, 0xE000, 0xF000, 0x10000}) {
        Fixture guarded;
        guarded.guard = guarded.base + page;
        s::Reader reader([&](auto a, auto& r) { return guarded.query(a, r); },
                         [&](auto a, auto* p, auto n) { return guarded.copy(a, p, n); });
        check(!(s::retained_player(reader, guarded.slot, player) &&
                s::owner(reader, guarded.contract, player, first)),
              "guard in each retained/owner dependency refuses the shared phase");
    }
    Fixture boundary;
    {
        s::Reader reader([&](auto a, auto& r) { return boundary.query(a, r); },
                         [&](auto a, auto* p, auto n) { return boundary.copy(a, p, n); });
        check(s::owner(reader, boundary.contract, boundary.player, first), "owner before lexical boundary");
    }
    boundary.guard = boundary.weapon;
    const auto before_queries = boundary.queries;
    {
        s::Reader reader([&](auto a, auto& r) { return boundary.query(a, r); },
                         [&](auto a, auto* p, auto n) { return boundary.copy(a, p, n); });
        check(!s::owner(reader, boundary.contract, boundary.player, first) &&
              boundary.queries > before_queries,
              "next lexical owner phase re-queries and rejects new guard");
    }
}

void wiring(const std::filesystem::path& runtime_path) {
    const auto source=read_file(runtime_path);
    check(!source.empty(),"runtime source available for wiring audit");
    const std::array signatures{
        "bool seh_capture_p99_chloe_transfer_art_skeleton(",
        "bool seh_p99_chloe_transfer_locked_registry_shape(",
        "bool seh_capture_p99_chloe_transfer_entry(",
        "bool seh_validate_p99_chloe_transfer_row124(",
        "bool seh_capture_p99_chloe_transfer_locked_operands_while_locked(",
        "bool capture_p99_chloe_transfer_locked_operands(",
        "bool seh_p99_chloe_transfer_registry_identity_while_locked(",
        "bool p99_chloe_transfer_registry_identity_exact(",
        "bool seh_select_p99_chloe_transfer_epoch(",
        "bool seh_p99_chloe_transfer_epoch_owner_still_exact(",
        "P99ChloeTransferMaterializeResult seh_materialize_p99_chloe_embedded_epoch(",
        "bool seal_p99_chloe_transfer_ticket("};
    for(const auto signature:signatures){
        const auto function=body(source,signature);
        check(!function.empty(),signature);
        for(const auto forbidden:{"readable_bytes(","get_weapon_from_slot(","retained_player_object(",
                                  "seh_resolve_idle_arm_pose_owner_scope("})
            check(function.find(forbidden)==std::string::npos,forbidden);
    }
    const auto capture=body(source,"bool seh_capture_p99_chloe_transfer_locked_operands_while_locked(");
    for(const auto edge:{"&epoch, purpose", "&player, purpose", "&chloe, purpose", "&nadine, purpose",
                        "&row20_native, purpose", "&row157_native, purpose", "&row124_entry, purpose",
                        "&epoch_after, purpose", "pages.range(chloe.reference_pose",
                        "pages.range(nadine.reference_pose"})
        check(capture.find(edge)!=std::string::npos,edge);
    const auto art_capture = body(source, "bool seh_capture_p99_chloe_transfer_art_skeleton(");
    check(art_capture.find("P99SourceRangeBatch pages(purpose)") != std::string::npos &&
          art_capture.find("pages.range(art, 0x48)") != std::string::npos &&
          art_capture.find("pages.range(underlying, 0x24)") != std::string::npos &&
          art_capture.find("pages.range(reference_pose,") != std::string::npos,
          "three Art/raw/reference spans use one callback-free batch");
    for (const auto signature : {"bool seh_capture_p99_chloe_transfer_entry(",
                                "bool seh_validate_p99_chloe_transfer_row124("}) {
        const auto function = body(source, signature);
        const auto native_lookup = function.find("p99_source_find_entry(");
        const auto batch = function.find("P99SourceRangeBatch pages(purpose)");
        check(native_lookup != std::string::npos && batch != std::string::npos && native_lookup < batch,
              "entry range batch begins only after native lookup");
        check(function.find("pages.range(entry, 0x40)") != std::string::npos &&
              function.find("pages.range(authored, 0x2E)") != std::string::npos &&
              function.find("pages.range(records,") != std::string::npos,
              "entry/authored/records all keep full range admission");
    }
    const auto range_adapter = source.find("class P99SourceRangeBatch {");
    const auto adapter_end = source.find("bool p99_source_range(", range_adapter);
    const auto adapter = range_adapter != std::string::npos && adapter_end != std::string::npos
        ? source.substr(range_adapter, adapter_end - range_adapter) : std::string{};
    check(range_adapter != std::string::npos && adapter_end != std::string::npos &&
          adapter.find("purpose_ == P99SourcePurpose::kPersistentJointCache") <
              adapter.find("readable_bytes(pointer) >= bytes") &&
          adapter.find("reader_.range(") != std::string::npos,
          "batch adapter preserves legacy behavior and shared owned Reader");
    check(body(source,"bool capture_p99_chloe_transfer_locked_operands(").find("target_skeleton, output, purpose")!=std::string::npos,"capture forwards purpose");
    check(body(source,"bool p99_chloe_transfer_registry_identity_exact(").find("target_skeleton, purpose")!=std::string::npos,"identity forwards purpose");
    const auto materialize=body(source,"P99ChloeTransferMaterializeResult seh_materialize_p99_chloe_embedded_epoch(");
    check(materialize.find("prepared.target_skeleton, &operands, purpose")!=std::string::npos,"materializer capture forwards purpose");
    check(materialize.find("prepared.target_skeleton, purpose")!=std::string::npos,"materializer identity forwards purpose");
    const auto seal=body(source,"bool seal_p99_chloe_transfer_ticket(");
    check(seal.find("epoch.target_skeleton, purpose")!=std::string::npos,"seal identity forwards purpose");
    check(seal.find("p99_source_retained_player(purpose)")!=std::string::npos,"seal checked retained resolution");
    const auto owner=body(source,"bool seh_resolve_p99_source_owner_scope(");
    check(owner.find("purpose == P99SourcePurpose::kPersistentJointCache")<owner.find("seh_resolve_idle_arm_pose_owner_scope("),"shared owner reachable only through legacy gate");
    check(owner.find("p99_source::owner(")!=std::string::npos,"owned owner uses tested reader");
    const auto ranges=body(source,"bool p99_source_range(");
    check(ranges.find("purpose == P99SourcePurpose::kPersistentJointCache")<ranges.find("readable_bytes("),"broad probes legacy only");
    const auto retained=body(source,"void* p99_source_retained_player(");
    check(retained.find("purpose == P99SourcePurpose::kPersistentJointCache")<retained.find("retained_player_object("),"unchecked retained legacy only");
    check(source.find("constexpr bool kEnableP99GroupShadow = false;")!=std::string::npos,"group flag remains disabled");
    check(source.find("constexpr bool kEnableLegacyNadineP99ChloeGripTransfer = false;")!=std::string::npos,"persistent writer remains disabled");
}
}

int main(int argc,char** argv) {
    range_batch_scopes();
    owner_phase_scopes();
    Fixture valid;
    const auto untouched=valid.data;
    s::Owner out{};
    check(valid.capture(out),"valid P99 current slot");
    check(out.weapon==valid.weapon&&out.player==valid.player&&out.slot==2&&out.state==2,"owned identity values");
    check(out.position==std::array<float,3>{1,2,3},"owned numeric values");
    check(valid.data==untouched,"all input bytes unchanged including weapon72");
    check(valid.queries<32,"bounded callback-free owner certificate reuse");
    for(const auto slot:std::array<std::int32_t,5>{-1,18,22,23,INT32_MAX}){
        Fixture bad;bad.put(bad.controller+0x120,slot);out= s::Owner{};out.player=1;
        check(!bad.capture(out)&&out.player==0,"P99 rejects noncurrent/admissible slot");
    }
    for(unsigned mutation=0;mutation<21;++mutation){
        Fixture bad;
        switch(mutation){
        case 0:bad.put(bad.slot,std::uintptr_t{0});break;
        case 1:bad.put(bad.slot+8,std::uint32_t{0});break;
        case 2:bad.put(bad.retained+8,std::uintptr_t{0});break;
        case 3:bad.put(bad.player+0xBC,std::uint32_t{999});break;
        case 4:bad.put(bad.player,std::uintptr_t{123});break;
        case 5:bad.put(bad.player+0x4010,std::uint32_t{1});break;
        case 6:bad.put(bad.player+0x170,std::uint32_t{2});break;
        case 7:bad.put(bad.fg+6,std::uint8_t{1});break;
        case 8:bad.put(bad.fg+0x1B0,std::uint32_t{999});break;
        case 9:bad.put(bad.art+0x24,std::uint32_t{0});break;
        case 10:bad.put(bad.raw+4,std::uint32_t{0});break;
        case 11:bad.put(bad.controller,std::uintptr_t{0});break;
        case 12:bad.put(bad.controller+0x108,std::uintptr_t{0});break;
        case 13:bad.put(bad.controller+0x134,std::uint32_t{1});break;
        case 14:bad.put(bad.grenade+0x120,std::uint32_t{1});break;
        case 15:bad.put(bad.controller+0x168,std::uintptr_t{0});break;
        case 16:bad.put(bad.controller+0x170,std::uint32_t{0});break;
        case 17:bad.put(bad.weapon+0xBC,std::uint32_t{999});break;
        case 18:bad.put(bad.weapon+0x860,std::uint32_t{0});break;
        case 19:bad.put(bad.parent+8,std::uintptr_t{0});break;
        case 20:bad.put(bad.weapon,std::uintptr_t{0});break;
        }
        out.player=1;
        check(!bad.capture(out)&&out.player==0,"identity/state mutation fails closed");
    }
    for(unsigned mutation=0;mutation<5;++mutation){
        Fixture bad;bad.turn_on=2;
        const std::array<std::uintptr_t,5> addresses{bad.controller+0x134,bad.controller+0x120,
            bad.controller+0x170,bad.weapon+0xBC,bad.slot+8};
        bad.turn_address=addresses[mutation];bad.turn_value=mutation==0?1:999;
        out.player=1;
        check(!bad.capture(out)&&out.player==0,"observed turnover rejected");
    }
    for(const auto offset:std::array<std::uintptr_t,5>{0x1000,0x2000,0xC000,0xE000,0xF000}){
        Fixture bad;bad.guard=bad.base+offset;
        check(!bad.capture(out),"guard inside owner dependency rejected");
    }
    {
        Fixture bad;bad.put(bad.controller+0x168,UINTPTR_MAX-3);
        check(!bad.capture(out),"overflowing weak handle rejected");
    }
    {
        Fixture good;std::uintptr_t player=0;
        s::Reader reader([&](auto a,auto& r){return good.query(a,r);},[&](auto a,auto* p,auto n){return good.copy(a,p,n);});
        check(s::retained_player(reader,good.slot,player)&&player==good.player,"checked retained helper used by seal");
    }
    {
        unsigned queries=0,copies=0;
        auto query=[&](auto a,auto& r){++queries;r={0x100000,0x40000,true,true,a>=0x120000};return true;};
        // Split regions so the second half must be queried, not implicitly covered.
        auto split=[&](auto a,auto& r){++queries;r={a<0x120000?0x100000u:0x120000u,0x20000,true,true,a>=0x120000};return true;};
        auto copy=[&](auto,auto*,auto){++copies;return true;};
        s::Reader reader(split,copy);
        check(!reader.range(0x100000,0x40000)&&queries==2&&copies==0,"full table second-half guard rejected without touch");
        check(!reader.range(0x100000,1)&&queries==2,"failed reader does not retry");
        s::Reader fresh(query,copy);
        check(fresh.range(0x100000,0x40000),"generic maximum source span accepted independently of retarget capacity");
        check(!fresh.range(0x100000,0x40001),"source maximum bounded");
        s::Reader overflow(query,copy);std::uint32_t word=0;
        check(!overflow.read(UINTPTR_MAX-1,4,word)&&copies==0,"base plus offset overflow");
    }
    // Real Windows mappings in THIS test process, never the game.
    Allocation memory{VirtualAlloc(nullptr,0x13000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)};
    if(!memory.pointer)return 2;
    const auto address=reinterpret_cast<std::uintptr_t>(memory.pointer);
    Fixture real(address);std::memcpy(memory.pointer,real.data.data(),real.data.size());
    DWORD previous=0;
    if(!VirtualProtect(memory.pointer,0x12000,PAGE_READONLY,&previous)||
       !VirtualProtect(reinterpret_cast<void*>(address+0x12000),0x1000,PAGE_READONLY|PAGE_GUARD,&previous))return 2;
    {
        s::Reader reader(&coop::p99_group_memory::query_region,&live_copy);
        check(s::owner(reader,real.contract,real.player,out),"read-only owner inputs succeed");
        check(std::memcmp(memory.pointer,real.data.data(),real.data.size())==0,"read-only fixture byte-for-byte unchanged");
        MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(address+0x12000),&info,sizeof(info));
        check((info.Protect&PAGE_GUARD)!=0,"adjacent guard not consumed");
    }
    if(!VirtualProtect(reinterpret_cast<void*>(real.weapon),0x1000,PAGE_READONLY|PAGE_GUARD,&previous))return 2;
    {
        s::Reader reader(&coop::p99_group_memory::query_region,&live_copy);
        check(!s::owner(reader,real.contract,real.player,out),"fresh post-boundary reader sees guard");
        MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(real.weapon),&info,sizeof(info));
        check((info.Protect&PAGE_GUARD)!=0,"internal guard not consumed");
    }
    if(!VirtualProtect(reinterpret_cast<void*>(real.weapon),0x1000,PAGE_READONLY,&previous))return 2;
    {
        s::Reader reader(&coop::p99_group_memory::query_region,&live_copy);
        check(s::owner(reader,real.contract,real.player,out),"new snapshot after protection restoration");
    }
    const auto runtime_path=argc>1?std::filesystem::path(argv[1]):
        std::filesystem::path(__FILE__).parent_path().parent_path()/"src"/"coop_runtime.cpp";
    wiring(runtime_path);
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
