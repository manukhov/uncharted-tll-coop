// Compile the actual production data phase, not replacement admission helpers.
// These macros remove DllMain and add a test-only pre-copy fault seam. No game
// code is mapped/executed and no runtime installer or observation route is run.
#define TLL_P99_PREPARATION_AUDIT 1
#define TLL_P99_WRIST_READ_AUDIT 1
#include "../src/coop_runtime.cpp"

#include <bcrypt.h>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace wrist_read_test {
namespace a = coop::p99_group_admission;
namespace s = coop::p99_wrist_snapshot;
unsigned checks = 0, failures = 0;
const char* current_case = "setup";
void check(bool okay, const char* label) {
    ++checks;
    if (!okay) { ++failures; std::cerr << "FAIL " << current_case << ": " << label << '\n'; }
}
void require(bool okay, const char* label) {
    if (!okay) throw std::runtime_error(label);
}
void protect(std::uintptr_t address, std::size_t bytes, DWORD protection) {
    DWORD old = 0;
    require(VirtualProtect(reinterpret_cast<void*>(address), bytes, protection, &old) != FALSE,
            "VirtualProtect owned fixture");
}
template<class T> void put(std::uintptr_t address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template<class T> void edit(std::uintptr_t address, const T& value) {
    DWORD old = 0, ignored = 0;
    require(VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), PAGE_READWRITE, &old) != FALSE,
            "make owned mutation writable");
    put(address, value);
    require(VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), old, &ignored) != FALSE,
            "restore owned mutation protection");
}
std::vector<char> load_asset(const char* path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "open independently pinned Nadine PAK");
    std::vector<char> data{std::istreambuf_iterator<char>{input}, {}};
    require(data.size() == 732928, "Nadine PAK exact length");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
            "open SHA256");
    std::array<UCHAR,32> hash{};
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()),
        hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    require(status == 0, "hash Nadine PAK");
    // Independent full-file identity, not values copied out of the validator.
    constexpr std::array<UCHAR,32> expected{0x2b,0xf6,0x6e,0x61,0xfa,0xe1,0x56,0xa4,
        0x19,0x52,0xd8,0xd2,0x65,0x9e,0x06,0x21,0x5d,0xbe,0x8f,0x0c,0x82,0x7a,
        0x9f,0x6a,0x89,0xca,0xda,0x0c,0x94,0x90,0xb0,0x6b};
    require(hash == expected, "Nadine PAK SHA256 identity");
    return data;
}

struct Region {
    std::uintptr_t base = 0;
    explicit Region(std::size_t bytes, DWORD allocation) {
        base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, bytes, allocation, PAGE_READWRITE));
        require(base != 0, "VirtualAlloc owned fixture");
    }
    ~Region() { if (base) VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;
};
struct Fixture {
    static constexpr std::size_t graph_bytes = 0x43000;
    Region module{0x4000000, MEM_RESERVE};
    Region graph{graph_bytes, MEM_RESERVE | MEM_COMMIT};
    std::vector<std::uintptr_t> module_pages;
    std::uintptr_t retained, player, fg, art, raw, pose, controller, grenade, weapon,
        handle, parent, job, process_parent, scratch, commands, segment, work, metadata, arena;
    std::uint32_t words = 48;
    P99WristOwner owner{};
    P99WristProcessValues process{};
    explicit Fixture(const std::vector<char>& asset, bool sphere = false) :
        retained(graph.base+0x1000), player(graph.base+0x2000), fg(graph.base+0x8000),
        art(graph.base+0x9000), raw(graph.base+0xA000), pose(graph.base+0x14000),
        controller(graph.base+0x15000), grenade(graph.base+0x16000), weapon(graph.base+0x17000),
        handle(graph.base+0x18000), parent(graph.base+0x19000), job(graph.base+0x1A000),
        process_parent(graph.base+0x1B000), scratch(graph.base+0x1C000),
        commands(graph.base+0x1E000), segment(graph.base+0x1F000), work(graph.base+0x20000),
        metadata(graph.base+0x21000), arena(graph.base+0x22000) {
        g_module = module.base; g_state_net.enabled = true; g_net_role_b = true;
        g_p99_preparation_audit = {}; g_p99_wrist_copy_audit = nullptr;
        // Only four data pages are committed. Dispatch/vtable addresses are
        // identities in reserved non-executable memory; calling one must fault.
        for (auto rva : {std::uintptr_t{0x3EEBE80}, std::uintptr_t{0x3682F4B},
                        std::uintptr_t{0x300C938}, std::uintptr_t{0x3026CB0}}) {
            const auto page = (module.base+rva) & ~std::uintptr_t{0xFFF};
            require(VirtualAlloc(reinterpret_cast<void*>(page), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
                    "commit data-only module page");
            module_pages.push_back(page);
        }
        put(module.base+0x3EEBE80, retained); put(module.base+0x3EEBE88, std::uint32_t{0x123});
        put(retained+8, player); put(player, module.base+0x2A4BFE8);
        put(player+0xBC, std::uint32_t{0x123}); put(player+0x170, std::uint32_t{0xD27C074F});
        put(player+0x1A8, fg); put(player+0x1D88, controller); put(player+0x1E50, grenade);
        put(player+0x140, std::array<float,4>{0,0,0,1});
        put(player+0x240, std::array<float,3>{1,2,3});
        put(fg+6, std::uint8_t{2}); put(fg+0x18, art); put(fg+0x100, pose);
        put(fg+0x1A0, art); put(fg+0x1A8, retained); put(fg+0x1B0, std::uint32_t{0x123});
        put(art+0x24, std::uint32_t{0xED25C708}); put(art+0x34, std::uint32_t{580}); put(art+0x40, raw);
        std::memcpy(reinterpret_cast<void*>(raw), asset.data()+0x53FE0, 0x8F40);
        put(controller, module.base+0x2A47F90); put(controller+0x108, player);
        put(controller+0x120, std::int32_t{2}); put(controller+0x134, std::uint32_t{2});
        put(controller+0x168, handle); put(controller+0x170, std::uint32_t{0x456});
        put(grenade, module.base+0x2A227A0); put(grenade+0x108, player);
        put(handle+8, weapon); put(weapon, module.base+0x2A9BF48);
        put(weapon+0xBC, std::uint32_t{0x456}); put(weapon+0x860, std::uint32_t{0x8075397D});
        put(weapon+0x72, std::uint8_t{0x35}); put(weapon+0x4E0, parent); put(parent+8, player);
        put(job, std::uintptr_t{1}); put(job+8, fg);
        put(process_parent, art); put(process_parent+0x10, fg+0x1C0); put(process_parent+0x18, scratch);
        // Unused command data pointers are integer identities only. No fake
        // command executor is provided and none is required by the reader.
        for (auto offset : {0x110u,0x118u,0x120u,0x200u,0x220u,0x230u,0x240u})
            put(fg+offset, graph.base+0x42000+offset);
        put(module.base+0x300C938, std::uint16_t{6});
        for (const auto& binding : p99_program::kBindings) {
            const auto slot = binding.opcode < 128 ? module.base+0x3026D20+8*binding.opcode :
                module.base+0x3026CB0+8*(binding.opcode-128);
            put(slot, module.base+binding.handler_rva);
        }
        build_commands(sphere);
        // Native storage layout transcribed independently, not memcpy(WorkBuffer).
        put(work, arena); put(work+8, std::uint64_t{0x20000});
        put(work+0x10, metadata); put(work+0x18, std::uint64_t{0x1000});
        put(work+0x20, metadata+3*56); put(work+0x28, metadata+0xE00);
        put(work+0x30, std::uint64_t{0x4430}); put(work+0x38, std::uint64_t{0x20000});
        put(work+0x48, std::uint32_t{3}); put(work+0x50, std::uint32_t{64});
        const std::array<std::array<std::uint64_t,7>,3> rows{{
            {0,4992,0,104,0,0,0}, {0x2000,6720,0,105,0,3,0}, {0x4000,1072,0,266,0,5,0}}};
        for (std::size_t i=0;i<3;++i) { put(metadata+56*i, rows[i]); put(metadata+0xE00+8*i, metadata+56*i); }
        for (std::size_t i=3;i<63;++i) put(metadata+56*i+48, metadata+56*(i+1));
        put(segment, work); put(segment+0x10, module.base+0x3026CB0); put(segment+0x18, std::uint16_t{6});
        put(segment+0x28, raw); put(segment+0x30, scratch); put(segment+0x38, raw+0x40);
        put(segment+0x40, raw+0x140); put(segment+0x50, arena); put(segment+0x58, arena+0x2000);
        put(segment+0x68, arena+0x4000);
        for (std::size_t i=0;i<0x20000;++i)
            *reinterpret_cast<std::byte*>(arena+i)=std::byte((i*37+11)%256);
        put(arena+0x480+12, std::uint32_t{0x7FC01234}); // S.w NaN, uninterpreted.
        put(arena+0x480+44, std::uint32_t{0x80000000}); // T.w negative zero.
        put(arena+0x2000+0x5C0+48,
            std::array<std::uint32_t,4>{0xDEADBEEF,0xFFC01234,0x7F800000,0xA5A5A5A5});
        protect(graph.base, graph_bytes, PAGE_READONLY);
        for (auto page : module_pages) protect(page, 0x1000, PAGE_READONLY);
    }
    ~Fixture() {
        g_p99_wrist_copy_audit=nullptr; g_module=0; g_state_net.enabled=false;
        g_net_role_b=false; g_p99_preparation_audit={};
    }
    const void* callback() const { return reinterpret_cast<const void*>(module.base+0x6CD220); }
    void build_commands(bool sphere) {
        std::vector<std::uint32_t> data;
        const auto add = [&](std::uint32_t op, std::size_t count) {
            auto start=data.size(); data.resize(start+count,0xCCCCCCCC);
            data[start]=op | (static_cast<std::uint32_t>(count)<<16); return start;
        };
        const auto pointer = [&](std::size_t index, std::uintptr_t address) {
            data[index]=static_cast<std::uint32_t>(address); data[index+1]=static_cast<std::uint32_t>(address>>32);
        };
        const auto fgptr = [&](std::size_t offset) { std::uintptr_t value=0; std::memcpy(&value,reinterpret_cast<void*>(fg+offset),8); return value; };
        add(0,2); add(1,2); auto i=add(2,4); data[i+1]=0; data[i+2]=1;
        i=add(9,2); data[i+1]=0; add(3,2); add(4,2);
        i=add(14,6); pointer(i+2,pose); pointer(i+4,fgptr(0x120));
        i=add(15,4); pointer(i+2,fgptr(0x118)); add(16,2);
        i=add(17,4); pointer(i+2,fgptr(0x120));
        i=add(24,6); data[i+1]=0x248B1AAE; pointer(i+2,0); pointer(i+4,fgptr(0x110));
        if (sphere) {
            i=add(24,10); data[i+1]=0x514C0761; pointer(i+2,0);
            pointer(i+4,fgptr(0x200)); pointer(i+6,fg+0x1C0); pointer(i+8,fg);
        }
        i=add(26,10); pointer(i+2,fgptr(0x230)); pointer(i+4,fgptr(0x240)); pointer(i+6,fg+0x220);
        data[i+8]=0; data[i+9]=104; add(5,2);
        words=static_cast<std::uint32_t>(data.size());
        std::memcpy(reinterpret_cast<void*>(commands),data.data(),data.size()*4);
    }
    bool select(P99WristReadReject* reason=nullptr) {
        return seh_p99_wrist_owner(reinterpret_cast<void*>(job),&owner,reason);
    }
    bool admit(P99WristReadReject* reason=nullptr) {
        return seh_p99_wrist_process(reinterpret_cast<const std::uint32_t*>(commands),words,
            reinterpret_cast<const void*>(process_parent),0,callback(),owner,&process,reason);
    }
    bool capture(s::Result& result, P99WristReadReject* reason=nullptr,
                 P99WristReadDiagnostic* diagnostic=nullptr) {
        return seh_p99_wrist_snapshot(reinterpret_cast<void*>(job),owner,process,
            reinterpret_cast<const void*>(segment),&result,reason,diagnostic);
    }
    void prepare() {
        P99WristReadDiagnostic d{};
        if(!seh_p99_wrist_owner(reinterpret_cast<void*>(job),&owner,nullptr,&d)) {
            std::cerr<<"owner setup diagnostic reason="<<static_cast<int>(d.reason)
                <<" check="<<p99_source::owner_check_name(d.owner.check)
                <<" numeric="<<static_cast<int>(d.numeric)<<'\n';
            require(false,"fixture owner admitted");
        }
        if(!seh_p99_wrist_process(reinterpret_cast<const std::uint32_t*>(commands),words,
            reinterpret_cast<const void*>(process_parent),0,callback(),owner,&process,nullptr,&d)) {
            std::cerr<<"Process setup diagnostic reason="<<static_cast<int>(d.reason)
                <<" command="<<static_cast<int>(d.command)<<'\n';
            require(false,"fixture Process admitted");
        }
    }
    std::vector<std::byte> source_bytes() const {
        std::vector<std::byte> result(graph_bytes+module_pages.size()*0x1000);
        std::memcpy(result.data(),reinterpret_cast<const void*>(graph.base),graph_bytes);
        for(std::size_t i=0;i<module_pages.size();++i)
            std::memcpy(result.data()+graph_bytes+i*0x1000,reinterpret_cast<const void*>(module_pages[i]),0x1000);
        return result;
    }
};
bool empty(const s::Result& result) {
    return !result.copied() && std::ranges::all_of(result.payload.bytes,[](auto b){return b==std::byte{};});
}
s::Result dirty_result() { s::Result r; r.reason=s::Reject::None; r.payload.bytes.fill(std::byte{0xCC}); return r; }
void refuse_snapshot(Fixture& fixture) {
    auto result=dirty_result(); P99WristReadReject reason{};
    check(!fixture.capture(result,&reason),"snapshot rejects");
    check(reason!=P99WristReadReject::None,"failure reason set");
    check(empty(result),"entire failed payload zero, never partial");
}
void baseline(const std::vector<char>& asset) {
    current_case="real read-only data phase";
    for(bool sphere:{false,true}) {
        Fixture f(asset,sphere); auto before=f.source_bytes(); f.prepare();
        auto result=dirty_result(); P99WristReadReject reason{};
        check(f.capture(result,&reason) && result.copied() && reason==P99WristReadReject::None,"real production snapshot accepts");
        constexpr std::array<std::size_t,4> offsets{0x480,0x4E0,0x930,0x990};
        for(std::size_t i=0;i<4;++i)
            check(std::memcmp(result.payload.bytes.data()+48*i,reinterpret_cast<void*>(f.arena+offsets[i]),48)==0,"exact JP bytes including all lanes");
        check(std::memcmp(result.payload.bytes.data()+192,reinterpret_cast<void*>(f.arena+0x25C0),64)==0,"exact W22 bytes including padding");
        check(f.source_bytes()==before,"every committed source byte unchanged");
        check(!result.source_admitted()&&!result.final_render_admitted(),"no synthetic live authority");
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(reinterpret_cast<void*>(f.module.base+0x217B630),&info,sizeof(info))!=0 && info.State==MEM_RESERVE,"retail executor address uncommitted");
        check(VirtualQuery(reinterpret_cast<void*>(f.arena),&info,sizeof(info))!=0 && info.Protect==PAGE_READONLY,"source physically read-only");
        const auto owned=result.payload.bytes; edit(f.arena+0x480,std::uint32_t{0});
        check(result.payload.bytes==owned,"output independent after source changes");
    }
}
using Mutation=std::pair<const char*,std::function<void(Fixture&)>>;
void owner_mutations(const std::vector<char>& asset) {
    const std::vector<Mutation> changes{
        {"role",[](auto&){g_net_role_b=false;}}, {"network",[](auto&){g_state_net.enabled=false;}},
        {"player vtable",[](auto&f){edit(f.player,std::uintptr_t{1});}},
        {"look",[](auto&f){edit(f.player+0x170,std::uint32_t{0});}},
        {"player state",[](auto&f){edit(f.player+0x4010,std::uint32_t{1});}},
        {"retained generation",[](auto&f){edit(f.module.base+0x3EEBE88,std::uint32_t{7});}},
        {"retained object",[](auto&f){edit(f.retained+8,std::uintptr_t{});}},
        {"Fg owner generation",[](auto&f){edit(f.fg+0x1B0,std::uint32_t{7});}},
        {"Fg mode",[](auto&f){edit(f.fg+6,std::uint8_t{1});}},
        {"Fg cached art",[](auto&f){edit(f.fg+0x1A0,std::uintptr_t{});}},
        {"Fg override",[](auto&f){edit(f.fg+0x2A0,std::uintptr_t{1});}},
        {"forced cached mode",[](auto&f){edit(f.module.base+0x3682F4B,std::uint8_t{1});}},
        {"art id",[](auto&f){edit(f.art+0x24,std::uint32_t{});}},
        {"art count",[](auto&f){edit(f.art+0x34,std::uint32_t{579});}},
        {"raw id",[](auto&f){edit(f.raw+4,std::uint32_t{});}},
        {"raw count",[](auto&f){edit(f.raw+0x10,std::uint16_t{579});}},
        {"raw parameter count",[](auto&f){edit(f.raw+0x1A,std::uint16_t{6479});}},
        {"pose pointer",[](auto&f){edit(f.fg+0x100,std::uintptr_t{});}},
        {"controller vtable",[](auto&f){edit(f.controller,std::uintptr_t{1});}},
        {"controller owner",[](auto&f){edit(f.controller+0x108,std::uintptr_t{});}},
        {"controller state",[](auto&f){edit(f.controller+0x134,std::uint32_t{1});}},
        {"grenade vtable",[](auto&f){edit(f.grenade,std::uintptr_t{1});}},
        {"grenade owner",[](auto&f){edit(f.grenade+0x108,std::uintptr_t{});}},
        {"grenade state",[](auto&f){edit(f.grenade+0x120,std::uint32_t{1});}},
        {"weapon generation",[](auto&f){edit(f.weapon+0xBC,std::uint32_t{7});}},
        {"weapon slot generation",[](auto&f){edit(f.controller+0x170,std::uint32_t{7});}},
        {"weapon vtable",[](auto&f){edit(f.weapon,std::uintptr_t{1});}},
        {"weapon art",[](auto&f){edit(f.weapon+0x860,std::uint32_t{});}},
        {"weapon parent owner",[](auto&f){edit(f.parent+8,std::uintptr_t{});}},
        {"position NaN",[](auto&f){edit(f.player+0x240,std::uint32_t{0x7FC01234});}},
        {"position bounds",[](auto&f){edit(f.player+0x240,1000000.0F);}},
        {"quaternion NaN",[](auto&f){edit(f.player+0x140,std::uint32_t{0x7FC01234});}},
        {"quaternion norm",[](auto&f){edit(f.player+0x14C,0.0F);}},
    };
    for(const auto&[name,mutate]:changes) {
        current_case=name; Fixture f(asset); f.prepare(); mutate(f); auto before=f.source_bytes();
        refuse_snapshot(f);
        P99WristOwner result=f.owner; P99WristReadReject reason{};
        check(!seh_p99_wrist_owner(reinterpret_cast<void*>(f.job),&result,&reason),"fresh owner rejects");
        check(result.normal.fg==0 && result.owner.player==0 && result.owner.slot==-1,"failed owner cleared");
        check(before==f.source_bytes(),"failed read does not modify sources");
    }
    for(auto slot:{-1,18,22,INT_MIN,INT_MAX}) {
        current_case="invalid current slot"; Fixture f(asset); f.prepare(); edit(f.controller+0x120,slot); refuse_snapshot(f);
    }
    for(auto slot:{0,17}) {
        current_case="valid conservative slot endpoints"; Fixture f(asset);
        edit(f.controller+0x120,slot); edit(f.controller+0x148+16*slot,f.handle);
        edit(f.controller+0x150+16*slot,std::uint32_t{0x456}); f.prepare();
        auto r=dirty_result(); check(f.capture(r)&&r.copied(),"slot endpoint valid when selected weak pair matches");
    }
}
void process_mutations(const std::vector<char>& asset) {
    const std::vector<Mutation> changes{
        {"parent art",[](auto&f){edit(f.process_parent,std::uintptr_t{});}},
        {"parent sphere address",[](auto&f){edit(f.process_parent+0x10,f.fg+0x1C8);}},
        {"parent reserved",[](auto&f){edit(f.process_parent+0x20,std::uint32_t{1});}},
        {"scratch alignment",[](auto&f){edit(f.process_parent+0x18,f.scratch+1);}},
        {"custom count",[](auto&f){edit(f.module.base+0x300C938,std::uint16_t{5});}},
        {"custom handler",[](auto&f){edit(f.module.base+0x3026CB8,std::uintptr_t{});}},
        {"builtin handler",[](auto&f){edit(f.module.base+0x3026D20+41*8,std::uintptr_t{});}},
        {"program bytes",[](auto&f){edit(f.raw+0x5404,std::uint16_t{128});}},
        {"command opcode",[](auto&f){edit(f.commands+24*4,std::uint32_t{0x20009});}},
        {"command binding",[](auto&f){edit(f.commands+16*4,f.pose+16);}},
        {"scalar binding",[](auto&f){edit(f.fg+0x120,std::uintptr_t{});}},
        {"palette binding",[](auto&f){edit(f.fg+0x230,std::uintptr_t{});}},
    };
    for(const auto&[name,mutate]:changes) {
        current_case=name; Fixture f(asset); f.prepare(); mutate(f); refuse_snapshot(f);
        auto p=f.process; P99WristReadReject reason{};
        check(!seh_p99_wrist_process(reinterpret_cast<const std::uint32_t*>(f.commands),f.words,
            reinterpret_cast<const void*>(f.process_parent),0,f.callback(),f.owner,&p,&reason),"fresh Process rejects");
        check(p.scratch==0&&p.words==0&&p.parent==0&&std::ranges::all_of(p.commands,[](auto x){return x==0;}),"failed Process output zero");
    }
    current_case="accepted grammar changed after selection";
    Fixture f(asset); f.prepare(); edit(f.commands+4,std::uint32_t{0x55});
    // Padding itself remains legal, but comparison with the prior Process token
    // must still detect a changed command stream in this evaluation.
    P99WristProcessValues next{};
    check(seh_p99_wrist_process(reinterpret_cast<const std::uint32_t*>(f.commands),f.words,
        reinterpret_cast<const void*>(f.process_parent),0,f.callback(),f.owner,&next),"legal changed padding still independently admitted");
    auto r=dirty_result(); P99WristReadReject reason{};
    check(!f.capture(r,&reason)&&reason==P99WristReadReject::ProcessChanged&&empty(r),"changed command bytes cannot reuse old Process token");
}
void storage_mutations(const std::vector<char>& asset) {
    const std::vector<Mutation> changes{
        {"Segment reserved+08",[](auto&f){edit(f.segment+8,f.process_parent);}},
        {"Segment raw",[](auto&f){edit(f.segment+0x28,f.raw+16);}},
        {"Segment scratch",[](auto&f){edit(f.segment+0x30,f.scratch+16);}},
        {"Segment group",[](auto&f){edit(f.segment+0x48,std::uint16_t{1});}},
        {"Segment flags",[](auto&f){edit(f.segment+0x4A,std::uint16_t{1});}},
        {"Segment scalar",[](auto&f){edit(f.segment+0x60,f.arena+0x10000);}},
        {"JP exact start",[](auto&f){edit(f.segment+0x50,f.arena+16);}},
        {"arena extent",[](auto&f){edit(f.work+8,std::uint64_t{0x1FFF0});}},
        {"metadata extent",[](auto&f){edit(f.work+0x18,std::uint64_t{0xFF0});}},
        {"metadata table pointer",[](auto&f){edit(f.work+0x28,f.metadata+0xE08);}},
        {"front count",[](auto&f){edit(f.work+0x48,std::uint32_t{2});}},
        {"capacity",[](auto&f){edit(f.work+0x50,std::uint32_t{65});}},
        {"front cursor",[](auto&f){edit(f.work+0x30,std::uint64_t{0x4440});}},
        {"record count",[](auto&f){edit(f.metadata+24,std::uint64_t{66});}},
        {"record bytes",[](auto&f){edit(f.metadata+8,std::uint64_t{4976});}},
        {"record first",[](auto&f){edit(f.metadata+16,std::uint64_t{1});}},
        {"record flags",[](auto&f){edit(f.metadata+32,std::uint64_t{1});}},
        {"record type",[](auto&f){edit(f.metadata+40,std::uint64_t{3});}},
        {"duplicate table",[](auto&f){edit(f.metadata+0xE08,f.metadata);}},
        {"active record free",[](auto&f){edit(f.work+0x20,f.metadata);}},
        {"free list cycle",[](auto&f){edit(f.metadata+3*56+48,f.metadata+3*56);}},
        {"metadata wrap",[](auto&f){edit(f.work+0x10,UINT64_MAX-15);}},
        {"arena wrap",[](auto&f){edit(f.work,UINT64_MAX-15);}},
        {"owner token differs",[](auto&f){++f.owner.owner.weapon_id;}},
        {"Process scratch token differs",[](auto&f){f.process.scratch+=16;}},
    };
    for(const auto&[name,mutate]:changes) {current_case=name; Fixture f(asset); f.prepare(); mutate(f); refuse_snapshot(f);}
}

// Single-thread, fixture-only deterministic seam. The actual volatile copy and
// SEH containment remain production code; this never substitutes a Reader.
struct HookState {
    std::uintptr_t target=0, mutation_address=0;
    unsigned at=1, hits=0;
    std::uint32_t value=0x777;
    enum class Action { Count, TurnGeneration, NoAccess, ProtectOther, PartialFault } action=Action::Count;
    std::vector<std::pair<std::uintptr_t,std::size_t>>* transcript=nullptr;
} hook;
void copy_hook(std::uintptr_t address, void* output, std::size_t bytes) {
    if(hook.transcript) hook.transcript->emplace_back(address,bytes);
    if(address!=hook.target||++hook.hits!=hook.at) return;
    switch(hook.action) {
    case HookState::Action::TurnGeneration: edit(hook.mutation_address,hook.value); break;
    case HookState::Action::NoAccess: protect(address,bytes,PAGE_NOACCESS); break;
    case HookState::Action::ProtectOther: protect(hook.mutation_address,1,PAGE_NOACCESS); break;
    case HookState::Action::PartialFault:
        std::memset(output,0xA5,(std::min)(bytes,std::size_t{11}));
        RaiseException(0xE0429901,0,0,nullptr); break;
    default: break;
    }
}
void page_and_fault_cases(const std::vector<char>& asset) {
    // Each earlier phase succeeds before a NEW phase encounters inaccessible
    // memory. Reusing an old page certificate across phases would fail these.
    for(unsigned which=0;which<9;++which) {
        current_case="fresh post page checks"; Fixture f(asset); f.prepare();
        const std::array pages{f.player,f.raw,f.pose,f.controller,f.commands,f.segment,f.work,f.metadata,f.arena+0x1F000};
        protect(pages[which],0x1000,PAGE_NOACCESS); refuse_snapshot(f);
        protect(pages[which],0x1000,PAGE_READONLY);
    }
    current_case="real SEH after cached range";
    {
        Fixture f(asset); f.prepare(); hook={}; hook.target=f.arena+0x480; hook.action=HookState::Action::NoAccess;
        g_p99_wrist_copy_audit=&copy_hook; refuse_snapshot(f); check(hook.hits==1,"protected after range before actual copy");
        protect(f.arena,0x1000,PAGE_READONLY);
    }
    for(auto offset:{std::size_t{0x480},std::size_t{0x4E0},std::size_t{0x930},std::size_t{0x990},std::size_t{0x25C0}}) {
        current_case="real copy SEH contains partial output"; Fixture f(asset); f.prepare();
        hook={}; hook.target=f.arena+offset; hook.action=HookState::Action::PartialFault;
        g_p99_wrist_copy_audit=&copy_hook; refuse_snapshot(f); check(hook.hits==1,"one fault, no retry");
    }
    current_case="closing owner generation turnover";
    {
        Fixture f(asset); f.prepare(); hook={}; hook.target=f.arena+0x25C0;
        hook.mutation_address=f.weapon+0xBC; hook.action=HookState::Action::TurnGeneration;
        g_p99_wrist_copy_audit=&copy_hook;
        auto result=dirty_result(); P99WristReadReject reason{};
        check(!f.capture(result,&reason)&&empty(result),"complete private copy discarded after owner turnover");
        check(hook.hits==1,"turnover occurs at W22 copy boundary");
    }
    current_case="closing owner page certificate not reused";
    {
        Fixture f(asset); f.prepare(); hook={}; hook.target=f.arena+0x25C0;
        hook.mutation_address=f.controller; hook.action=HookState::Action::ProtectOther;
        g_p99_wrist_copy_audit=&copy_hook; P99WristReadDiagnostic diagnostic{};
        auto r=dirty_result(); P99WristReadReject reason{};
        check(!f.capture(r,&reason,&diagnostic)&&empty(r),"closing fresh page check rejects after full private copy");
        check(diagnostic.phase==P99WristReadPhase::ClosingOwner &&
            diagnostic.owner.check==p99_source::OwnerCheck::ControllerPointerAndRange &&
            !diagnostic.owner.initial_slot.attempted && diagnostic.snapshot_attempted &&
            diagnostic.snapshot==s::Reject::None,"closing range failure precedes slot read and retains successful private snapshot evidence");
        protect(f.controller,0x1000,PAGE_READONLY);
    }
}
void diagnostic_and_copy_coverage(const std::vector<char>& asset) {
    current_case="optional scalar diagnostics preserve read transcript";
    Fixture f(asset); f.prepare(); const auto before=f.source_bytes();
    using Copy=std::pair<std::uintptr_t,std::size_t>;
    std::vector<Copy> plain_copies, observed_copies;
    p99_prep_audit::QueryTrace plain_queries{},observed_queries{};
    p99_prep_audit::Stats plain_stats{},observed_stats{};
    hook={}; hook.transcript=&plain_copies; g_p99_wrist_copy_audit=&copy_hook;
    g_p99_preparation_audit.stats=&plain_stats; g_p99_preparation_audit.query_trace=&plain_queries;
    auto plain=dirty_result(); check(f.capture(plain),"null diagnostic capture");
    hook={}; hook.transcript=&observed_copies;
    g_p99_preparation_audit.stats=&observed_stats; g_p99_preparation_audit.query_trace=&observed_queries;
    auto observed=dirty_result(); P99WristReadDiagnostic diagnostic{};
    check(f.capture(observed,nullptr,&diagnostic),"observed diagnostic capture");
    check(plain.payload.bytes==observed.payload.bytes && plain_copies==observed_copies,"diagnostics preserve all ordered copy addresses and sizes");
    bool same_queries=plain_queries.count==observed_queries.count &&
        plain_queries.overflow==0 && observed_queries.overflow==0;
    for(std::size_t i=0;same_queries&&i<plain_queries.count;++i) {
        const auto& x=plain_queries.entries[i]; const auto& y=observed_queries.entries[i];
        same_queries=x.address==y.address&&x.region_base==y.region_base&&x.region_bytes==y.region_bytes&&x.flags==y.flags;
    }
    check(same_queries,"diagnostics preserve all ordered real VirtualQuery observations (timestamps excluded)");
    check(before==f.source_bytes(),"both diagnostic paths leave all sources unchanged");
    check(diagnostic.phase==P99WristReadPhase::ClosingOwner && diagnostic.reason==P99WristReadReject::None &&
        diagnostic.owner.check==p99_source::OwnerCheck::Complete &&
        diagnostic.numeric==p99_source::OwnerNumericCheck::Complete &&
        diagnostic.owner.initial_slot.read_ok&&diagnostic.owner.initial_slot.value==2&&
        diagnostic.owner.closing_slot.read_ok&&diagnostic.owner.closing_slot.value==2&&
        diagnostic.command_checked&&diagnostic.snapshot_attempted,"successful diagnostics describe reached closing phase");
    check(plain_stats.resolve_calls==0&&plain_stats.find_calls==0&&plain_stats.retarget_calls==0&&plain_stats.blend_calls==0,
        "no source lookup/native math helper entered");
    std::size_t payload_reads=0,payload_bytes=0,total_bytes=0;
    for(const auto&[address,bytes]:plain_copies) {
        total_bytes+=bytes;
        if(address>=f.arena&&address<f.arena+0x20000) {++payload_reads;payload_bytes+=bytes;}
    }
    check(payload_reads==5&&payload_bytes==256,"only five owned arena reads totaling exactly 256 bytes");
    std::cout<<"Instrumented owned fixture: "<<plain_copies.size()<<" copies, "<<plain_queries.count
        <<" VirtualQuery calls, "<<total_bytes<<" copied bytes incl admission; QPC wrapper timings are not production/game cost evidence\n";
    g_p99_preparation_audit={};
    // Fault each actual copy position, including repeated closing reads. The
    // hook writes part of its private destination before raising a real SEH.
    // No source is changed, so the same initial identity can be used each time.
    for(std::size_t i=0;i<plain_copies.size();++i) {
        current_case="every actual production copy failure is private";
        hook={}; hook.target=plain_copies[i].first; hook.at=1;
        for(std::size_t j=0;j<i;++j) if(plain_copies[j].first==hook.target) ++hook.at;
        hook.action=HookState::Action::PartialFault;
        g_p99_wrist_copy_audit=&copy_hook;
        auto result=dirty_result(); diagnostic={}; P99WristReadReject reason{};
        check(!f.capture(result,&reason,&diagnostic)&&empty(result),"injected partial copy refused with zero output");
        check(hook.hits==hook.at,"exact copy boundary reached once without retry");
        check(reason==diagnostic.reason&&reason!=P99WristReadReject::None,"diagnostic reason follows real rejection");
    }
    g_p99_wrist_copy_audit=nullptr;
    check(before==f.source_bytes(),"all fault injections preserve every source byte");
}
void diagnostic_refusals(const std::vector<char>& asset) {
    const std::vector<Mutation> invariant_failures{
        {"slot diagnostic refusal invariance",[](auto&f){edit(f.controller+0x120,std::int32_t{18});}},
        {"numeric diagnostic refusal invariance",[](auto&f){edit(f.player+0x240,std::uint32_t{0x7FC00001});}},
        {"Process diagnostic refusal invariance",[](auto&f){edit(f.commands+24*4,std::uint32_t{0x20009});}},
        {"allocation diagnostic refusal invariance",[](auto&f){edit(f.metadata+24,std::uint64_t{66});}},
        {"identity diagnostic refusal invariance",[](auto&f){++f.owner.owner.weapon_id;}},
    };
    for(const auto&[name,mutate]:invariant_failures) {
        current_case=name; Fixture f(asset); f.prepare(); mutate(f); const auto before=f.source_bytes();
        std::vector<std::pair<std::uintptr_t,std::size_t>> plain_trace,observed_trace;
        P99WristReadReject plain_reason{},observed_reason{}; P99WristReadDiagnostic diagnostic{};
        auto plain=dirty_result(),observed=dirty_result();
        hook={}; hook.transcript=&plain_trace; g_p99_wrist_copy_audit=&copy_hook;
        const bool plain_ok=f.capture(plain,&plain_reason);
        hook={}; hook.transcript=&observed_trace;
        const bool observed_ok=f.capture(observed,&observed_reason,&diagnostic);
        check(!plain_ok&&!observed_ok&&empty(plain)&&empty(observed),"both diagnostic variants refuse without output");
        check(plain_reason==observed_reason&&plain_reason==diagnostic.reason&&plain_trace==observed_trace,
            "diagnostics add no payload reads or changed rejection on failure");
        check(before==f.source_bytes(),"both refusing variants leave sources unchanged");
    }
    for(auto slot:{-1,18,22,INT_MIN,INT_MAX}) {
        current_case="slot read success distinguished from range refusal";
        Fixture f(asset); f.prepare(); edit(f.controller+0x120,slot);
        std::vector<std::pair<std::uintptr_t,std::size_t>> copies;
        hook={}; hook.transcript=&copies; g_p99_wrist_copy_audit=&copy_hook;
        auto r=dirty_result(); P99WristReadReject reason{}; P99WristReadDiagnostic d{};
        check(!f.capture(r,&reason,&d)&&empty(r),"bad slot refuses");
        check(reason==P99WristReadReject::Owner&&d.phase==P99WristReadPhase::Owner&&
            d.owner.check==p99_source::OwnerCheck::ControllerSlot&&
            d.owner.initial_slot.attempted&&d.owner.initial_slot.read_ok&&d.owner.initial_slot.value==slot&&
            !d.owner.closing_slot.attempted,"records actual bad slot without stale closing evidence");
        check(std::ranges::none_of(copies,[&](auto item){return item.first==f.controller+0x134;}),"range refusal occurs before controller state read");
    }
    current_case="failed original slot copy has no scalar value";
    {
        Fixture f(asset); f.prepare(); hook={}; hook.target=f.controller+0x120;
        hook.action=HookState::Action::PartialFault; g_p99_wrist_copy_audit=&copy_hook;
        auto r=dirty_result(); P99WristReadDiagnostic d{};
        check(!f.capture(r,nullptr,&d)&&empty(r),"faulted slot read refuses");
        check(d.owner.check==p99_source::OwnerCheck::ControllerSlot&&d.owner.initial_slot.attempted&&
            !d.owner.initial_slot.read_ok&&d.owner.initial_slot.value==0&&!d.owner.closing_slot.attempted,
            "partially scribbled slot value is not reported as a read");
    }
    current_case="closing slot mismatch is distinct from range rejection";
    {
        Fixture f(asset); f.prepare(); hook={}; hook.target=f.controller+0x120; hook.at=2;
        hook.mutation_address=hook.target; hook.value=3; hook.action=HookState::Action::TurnGeneration;
        g_p99_wrist_copy_audit=&copy_hook; auto r=dirty_result(); P99WristReadDiagnostic d{};
        check(!f.capture(r,nullptr,&d)&&empty(r),"closing slot turnover refuses");
        check(d.phase==P99WristReadPhase::Owner&&d.owner.check==p99_source::OwnerCheck::ClosingControllerSlot&&
            d.owner.initial_slot.read_ok&&d.owner.initial_slot.value==2&&
            d.owner.closing_slot.read_ok&&d.owner.closing_slot.value==3,"opening and closing slot observations remain distinct");
    }
    for(bool orientation:{false,true}) {
        current_case="real numeric owner validation"; Fixture f(asset); f.prepare();
        edit(f.player+(orientation?0x140:0x240),std::uint32_t{0x7FC00001});
        auto r=dirty_result(); P99WristReadDiagnostic d{};
        check(!f.capture(r,nullptr,&d)&&empty(r),"owner nonfinite rejection");
        check(d.reason==P99WristReadReject::Owner&&d.owner.check==p99_source::OwnerCheck::Complete&&
            d.numeric==(orientation?p99_source::OwnerNumericCheck::Orientation:p99_source::OwnerNumericCheck::Position),
            "numeric refusal not mislabeled as a structural/slot failure");
    }
    current_case="expected owner mismatch after successful fresh validation";
    {
        Fixture f(asset); f.prepare(); ++f.owner.owner.weapon_id;
        auto r=dirty_result(); P99WristReadDiagnostic d{};
        check(!f.capture(r,nullptr,&d)&&empty(r),"old expected identity refuses");
        check(d.reason==P99WristReadReject::OwnerIdentity&&d.phase==P99WristReadPhase::Owner&&
            d.owner.check==p99_source::OwnerCheck::Complete&&d.numeric==p99_source::OwnerNumericCheck::Complete,
            "completed fresh validation not overwritten by generic nested failure");
    }
    current_case="closing phase valid but different owner identity";
    {
        Fixture f(asset); edit(f.controller+0x178,f.handle); edit(f.controller+0x180,std::uint32_t{0x456}); f.prepare();
        hook={}; hook.target=f.arena+0x25C0; hook.mutation_address=f.controller+0x120;
        hook.value=3; hook.action=HookState::Action::TurnGeneration; g_p99_wrist_copy_audit=&copy_hook;
        auto r=dirty_result(); P99WristReadDiagnostic d{};
        check(!f.capture(r,nullptr,&d)&&empty(r),"different independently valid closing slot cancels full copy");
        check(d.reason==P99WristReadReject::ClosingOwner&&d.phase==P99WristReadPhase::ClosingOwner&&
            d.owner.check==p99_source::OwnerCheck::Complete&&d.owner.initial_slot.value==3&&
            d.snapshot_attempted&&d.snapshot==s::Reject::None,"closing failure retains full-copy/private status and its own owner observations");
    }
}
} // namespace wrist_read_test

int main(int argc,char**argv) {
    using namespace wrist_read_test;
    if(argc!=2){std::cerr<<"usage: wrist-read-tests path/to/nadine-dlc-base.pak\n";return 2;}
    try {
        const auto asset=load_asset(argv[1]);
        baseline(asset); owner_mutations(asset); process_mutations(asset); storage_mutations(asset);
        page_and_fault_cases(asset); diagnostic_and_copy_coverage(asset); diagnostic_refusals(asset);
    } catch(const std::exception& error) {
        std::cerr<<"fixture setup failure in "<<current_case<<": "<<error.what()<<'\n'; return 2;
    }
    std::cout<<checks<<" checks, "<<failures<<" failures; production data phase, owned memory only, no game/native evaluator\n";
    return failures?1:0;
}
