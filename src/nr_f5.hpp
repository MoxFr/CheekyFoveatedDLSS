#pragma once
// Cheeky F5 preview: upstream ViT decisions with private fenced output copies.
// Unexpected launches during reuse abort the NR evaluation before its decoder.
#include "runtime.hpp"
#include "dlss_nr_lifetime.hpp"
#include "f5/DlssNrVitReuse.h"
#include "f5/DlssNrVitCopyKernel.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <map>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstring>

namespace cheeky::foveated_dlss::nr_f5 {
using Microsoft::WRL::ComPtr;
struct Dim { unsigned x,y,z; };
struct Launch { void* function; Dim grid,block; unsigned shared; const void* params; unsigned size; };
static_assert(sizeof(Launch)==56); // NVIDIA nvapi.h, pinned 9b181ea5.
using CreateModule=int(__cdecl*)(ID3D12Device*,const void*,unsigned,void**);
using CreateFunction=int(__cdecl*)(ID3D12Device*,void*,const char*,void**);
using LaunchChain=int(__cdecl*)(ID3D12GraphicsCommandList*,const Launch*,unsigned);
using DestroyFunction=int(__cdecl*)(ID3D12Device*,void*);
struct CopyState {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> buffer;
    NrLifetime uses;
    ID3D12GraphicsCommandList* list{};
    std::uint64_t address{},bytes{},signature{};
    bool valid{},last_reused{},retired{};
};
struct Context { const void* feature{}; bool reset{}; unsigned runs{}; bool enabled{}; std::uint64_t slot{}; bool open{},skipping{},aborted{}; DlssNrVitReuse::KernelSet set{}; };
inline thread_local Context context;
struct State {
    std::recursive_mutex mutex;
    CreateModule module{}; CreateFunction create{}; LaunchChain launch{}; DestroyFunction destroy{}; DestroyFunction destroy_module{};
    void* create_target{}; void* launch_target{}; void* destroy_target{}; void* destroy_module_target{};
    bool installed{},attempted{}; DlssNrVitReuse::Filter filter;
    std::map<void*,std::pair<DlssNrVitReuse::Role,DlssNrVitReuse::KernelSet>> roles;
    std::map<const void*,CopyState> copies;
    std::map<ID3D12Device*,void*> copy_functions;
    std::atomic<unsigned long long> computed{},reused{},unsupported{};
};
inline State& state(){static auto* p=new State;return *p;}
inline void barrier(ID3D12GraphicsCommandList* c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;c->ResourceBarrier(1,&b);}
inline int __cdecl created(ID3D12Device* d,void* m,const char* name,void** out){
 auto& s=state();std::lock_guard lock(s.mutex);const int r=s.create(d,m,name,out);
 if(r==0&&out&&*out){s.roles.erase(*out);const auto role=DlssNrVitReuse::RoleOf(name);if(role!=DlssNrVitReuse::Role::None)s.roles[*out]={role,DlssNrVitReuse::SetOf(name)};}
 return r;
}
inline int __cdecl destroyed(ID3D12Device* d,void* fn){auto& s=state();std::lock_guard lock(s.mutex);s.roles.erase(fn);return s.destroy(d,fn);}
inline int __cdecl module_destroyed(ID3D12Device* d,void* m){auto& s=state();std::lock_guard lock(s.mutex);s.filter.Clear();s.roles.clear();for(auto& [key,c]:s.copies)c.valid=false;return s.destroy_module(d,m);}
inline std::uint64_t signature(const Launch& l){
 std::uint64_t h=1469598103934665603ULL;
 const auto add=[&](const void* ptr,std::size_t n){auto* p=static_cast<const unsigned char*>(ptr);for(std::size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ULL;}};
 add(&l.function,sizeof(l.function));add(&l.grid,sizeof(l.grid));add(&l.block,sizeof(l.block));add(&l.shared,sizeof(l.shared));
 if(l.params&&l.size<=65536)add(l.params,l.size);return h;
}

inline bool copy_output(ID3D12GraphicsCommandList* cmd,const Launch& tail,DlssNrVitReuse::Copy kind){
 auto& s=state();auto& cache=s.copies[context.feature];
 std::uint64_t address=0,bytes=0;
 const std::uint64_t threads=std::uint64_t(tail.grid.x)*tail.grid.y*tail.grid.z*tail.block.x*tail.block.y*tail.block.z;
 if(DlssNrVitReuse::CheckOutput(tail.params,tail.size,threads,s.filter.LastSet(),address,bytes)||bytes>32ULL*1024*1024)return false;
 ComPtr<ID3D12Device> device;if(FAILED(cmd->GetDevice(IID_PPV_ARGS(&device))))return false;
 auto& copyfn=s.copy_functions[device.Get()];
 if(!copyfn){void* module=nullptr;if(s.module(device.Get(),DlssNrVitCopyKernel::fatbin,unsigned(sizeof DlssNrVitCopyKernel::fatbin),&module)!=0||s.create(device.Get(),module,"nr_vit_copy16",&copyfn)!=0)return false;}
 if(kind==DlssNrVitReuse::Copy::Save){
  if(!cache.uses.empty())return false;
  if(!cache.buffer||cache.bytes!=bytes||cache.device.Get()!=device.Get()){
   D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
   D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   cache.buffer.Reset();if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&cache.buffer))))return false;cache.device=device;cache.bytes=bytes;
  }
  cache.address=address;cache.list=cmd;
 } else if(!cache.valid||!cache.buffer||cache.address!=address||cache.bytes!=bytes||cache.device.Get()!=device.Get()||cache.list!=cmd||!cache.uses.empty())return false;
 if(!cache.uses.record(cmd))return false;
 const std::uint64_t elements=bytes/16;
 struct Params{std::uint64_t dst,src,n;}p{kind==DlssNrVitReuse::Copy::Restore?address:cache.buffer->GetGPUVirtualAddress(),kind==DlssNrVitReuse::Copy::Restore?cache.buffer->GetGPUVirtualAddress():address,elements};
 Launch copy{copyfn,{unsigned((std::min<std::uint64_t>)((elements+255)/256,1024)),1,1},{256,1,1},0,&p,sizeof(p)};
 barrier(cmd);const auto result=s.launch(cmd,&copy,1);barrier(cmd);cache.valid=result==0;return result==0;
}
inline int abort_evaluation(const char* reason){
 auto& s=state();s.filter.Disable(reason);++s.unsupported;context.aborted=true;s.copies[context.feature].valid=false;
 trace_event("F5 Cheeky disabled for session: %s; aborting NR evaluation before dependent kernels",reason);return -1;
}
inline int __cdecl launched(ID3D12GraphicsCommandList* cmd,const Launch* kernels,unsigned count){
 auto& s=state();std::lock_guard lock(s.mutex);
 if(context.aborted)return -1;
 if(!context.enabled||!context.feature||!kernels||!count)return s.launch(cmd,kernels,count);
 using namespace DlssNrVitReuse;
 // Preserve the normal chain as a single driver call until a boundary requires
 // a copy/barrier. Validate each chunk before issuing any dependent work.
 unsigned first=0;
 const auto flush=[&](unsigned end){const auto n=end-first;const int r=n?s.launch(cmd,kernels+first,n):0;first=end;return r;};
 for(unsigned i=0;i<count;++i){
  auto it=s.roles.find(kernels[i].function);const auto role=it==s.roles.end()?Role::None:it->second.first;
  const auto set=it==s.roles.end()?KernelSet::Unknown:it->second.second;
  if(context.open&&context.skipping&&(role==Role::None||set!=context.set||role==Role::Start))return abort_evaluation("unexpected launch inside reused ViT range");
  if(role==Role::Start){if(context.open||context.runs++){if(context.skipping)return abort_evaluation("multiple ViT ranges");s.filter.Disable("multiple ViT ranges");++s.unsupported;context.enabled=false;return flush(count);}context.open=true;context.set=set;}
  const auto decision=s.filter.One(role,set,role==Role::Start?signature(kernels[i]):0);
  if(role==Role::Start)context.skipping=!decision.launch;
  if(!decision.launch||decision.barrier||decision.copy!=Copy::None){
   if(const int r=flush(i);r!=0)return abort_evaluation("driver rejected prefix chain");
   if(decision.barrier)barrier(cmd);
   if(decision.launch){if(s.launch(cmd,kernels+i,1)!=0)return abort_evaluation("driver rejected ViT tail");}
   if(decision.copy!=Copy::None&&!copy_output(cmd,kernels[i],decision.copy)){
    if(decision.copy==Copy::Restore)return abort_evaluation("ViT restore failed");
    s.filter.Disable("ViT save unavailable");++s.unsupported;context.enabled=false;
   }
   first=i+1;
  }
  if(role==Role::End){context.open=false;context.skipping=false;}
 }
 const int r=flush(count);if(r!=0)return abort_evaluation("driver rejected chain");return r;
}

inline bool install(){
 auto& s=state();std::lock_guard lock(s.mutex);if(s.attempted)return s.installed;s.attempted=true;
 const auto nv=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!nv)return false;
 const auto query=reinterpret_cast<void*(__cdecl*)(unsigned)>(GetProcAddress(nv,"nvapi_QueryInterface"));if(!query)return false;
 const auto init=reinterpret_cast<int(__cdecl*)()>(query(0x0150e828));if(!init||init()!=0)return false;
 s.module=reinterpret_cast<CreateModule>(query(0xad1a677d));s.create_target=query(0xe2436e22);s.launch_target=query(0x24973538);s.destroy_target=query(0xdf295ea6);s.destroy_module_target=query(0x41c65285);
 if(!s.module||!s.create_target||!s.launch_target||!s.destroy_target||!s.destroy_module_target)return false;
 const auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return false;
 std::vector<void*> owned;
 const auto hook=[&](void* target,void* detour,void** original){if(MH_CreateHook(target,detour,original)!=MH_OK)return false;owned.push_back(target);return true;};
 if(!hook(s.create_target,reinterpret_cast<void*>(&created),reinterpret_cast<void**>(&s.create))||!hook(s.launch_target,reinterpret_cast<void*>(&launched),reinterpret_cast<void**>(&s.launch))||!hook(s.destroy_target,reinterpret_cast<void*>(&destroyed),reinterpret_cast<void**>(&s.destroy))||!hook(s.destroy_module_target,reinterpret_cast<void*>(&module_destroyed),reinterpret_cast<void**>(&s.destroy_module))){for(auto* p:owned)MH_RemoveHook(p);return false;}
 for(auto* p:owned)if(MH_EnableHook(p)!=MH_OK){for(auto* t:owned){MH_DisableHook(t);MH_RemoveHook(t);}return false;}
 s.installed=true;trace_event("F5 Cheeky NvAPI interception installed; full-chain validation required before reuse");return true;
}
inline void retire(const void* key) noexcept {auto& s=state();std::lock_guard lock(s.mutex);auto it=s.copies.find(key);if(it==s.copies.end())return;it->second.valid=false;it->second.retired=true;it->second.uses.collect();if(it->second.uses.empty())s.copies.erase(it);s.filter.Clear();}
inline void shutdown() noexcept {auto& s=state();std::lock_guard lock(s.mutex);if(!s.installed)return;s.installed=false;s.filter.Disable("runtime detached; restart process to rearm F5");for(auto* p:{s.create_target,s.launch_target,s.destroy_target,s.destroy_module_target}){MH_DisableHook(p);MH_RemoveHook(p);}for(auto& [key,c]:s.copies)c.valid=false;trace_event("F5 Cheeky hooks removed; GPU resources retained until process exit");}
struct Scope {
 Context previous;
 Scope(const void* feature,ID3D12GraphicsCommandList* list,bool enabled,bool reset):previous(context){
  if(!enabled){context={};return;}auto& s=state();std::lock_guard lock(s.mutex);const auto slot=nr_recording_token(list);
  for(auto it=s.copies.begin();it!=s.copies.end();){if(it->second.retired){it->second.uses.collect();if(it->second.uses.empty()){it=s.copies.erase(it);continue;}}++it;}
  auto& c=s.copies[feature];c.retired=false;c.uses.collect();const bool available=c.uses.empty();
  context={feature,reset,0,enabled&&available&&slot!=0&&s.installed&&!s.filter.Disabled(),slot};
  if(!available)c.valid=false;
  if(context.enabled)s.filter.Begin(feature,reset||!c.valid,2,static_cast<long long>(slot),2,DlssNrVitReuse::kCopyPlain|DlssNrVitReuse::kCopyFp8);
 }
 ~Scope(){auto& s=state();std::lock_guard lock(s.mutex);if(context.enabled){if(!s.filter.End()){++s.unsupported;s.copies[context.feature].valid=false;}s.computed=s.filter.Computed();s.reused=s.filter.Reused();if((s.computed+s.reused)%120==1)trace_event("F5 Cheeky ViT computed=%llu reused=%llu unsupported=%llu",s.computed.load(),s.reused.load(),s.unsupported.load());}context=previous;}
};
} // namespace cheeky::foveated_dlss::nr_f5
