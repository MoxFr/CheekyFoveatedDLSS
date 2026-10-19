#pragma once
#include "dlss_nr_lifetime.hpp"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <deque>
#include <vector>
#include <cstring>
#include <iterator>
#include <utility>
#include <memory>

namespace cheeky::foveated_dlss {
// Conservative edit reuse. Each instance belongs to ONE eye/pass.
// Never carry an edited image: reproject only the model's RGB correction.
// History is a fresh-model keyframe. Reuse is limited to the next evaluation;
// another fresh model evaluation is required before history can be reused again.
class NrEditCache {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Frame {
        Ptr<ID3D12Resource> color, neural, depth;
        std::vector<Ptr<ID3D12DescriptorHeap>> heaps;
        NrLifetime uses;
        bool reused{};
    };
    std::deque<Frame> frames;
    Ptr<ID3D12Device> device;
    Ptr<ID3D12RootSignature> root;
    Ptr<ID3D12PipelineState> pipeline;
    unsigned width{},height{};
    bool valid{};
    unsigned age{};
    static void transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
        if(a==b)return; D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);
    }
public:
    static constexpr const char* shader=R"(
Texture2D<float4> Current:register(t0);
Texture2D<float> Depth:register(t1);
Texture2D<float2> Motion:register(t2);
Texture2D<float4> Old:register(t3);
Texture2D<float4> Neural:register(t4);
Texture2D<float> OldDepth:register(t5);
RWTexture2D<float4> Result:register(u0);
cbuffer Params:register(b0){uint2 Size;uint2 Padding;};
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=Size))return;
 float4 color=Current.Load(int3(id.xy,0));
 float2 mv=Motion.Load(int3(id.xy,0));
 float2 previous=float2(id.xy)+mv*float2(Size);
 float depth=Depth.Load(int3(id.xy,0));
 if(any(!isfinite(color.rgb))||any(!isfinite(mv))||!isfinite(depth)||any(previous<0)||any(previous>float2(Size)-1)) {Result[id.xy]=color;return;}
 int2 lo=int2(floor(previous));float2 f=frac(previous);float3 correction=0;float coverage=0;
 [unroll]for(uint y=0;y<2;y++)[unroll]for(uint x=0;x<2;x++){
  int2 p=min(lo+int2(x,y),int2(Size)-1);
  float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
  // Do not let an unused NaN tap poison an otherwise valid correspondence.
  if(weight<=0)continue;
  float3 old=Old.Load(int3(p,0)).rgb;float3 neural=Neural.Load(int3(p,0)).rgb;
  float oldDepth=OldDepth.Load(int3(p,0));
  if(any(!isfinite(old))||any(!isfinite(neural))||!isfinite(oldDepth))continue;
  float difference=max(max(abs(color.r-old.r),abs(color.g-old.g)),abs(color.b-old.b));
  float depthDifference=abs(depth-oldDepth)/max(max(abs(depth),abs(oldDepth)),0.001);
  float confidence=saturate(1-difference/0.08)*saturate(1-depthDifference/0.02);
  correction+=(neural-old)*(weight*confidence);
  coverage+=weight*confidence;
 }
 // Missing/disoccluded history fades to current color. Never normalize the
 // remaining taps: that would amplify a small surviving foreground sample.
 float3 result=color.rgb+(coverage>0?correction:float3(0,0,0));
 Result[id.xy]=float4(all(isfinite(result))?max(result,0):color.rgb,color.a);
})";
    void invalidate() noexcept {valid=false;}
    void collect() {
        for(auto i=frames.begin();i!=frames.end();){
            i->uses.collect();
            if(i!=std::prev(frames.end())&&i->uses.empty() &&
                (i->color->GetDesc().Width!=width||i->color->GetDesc().Height!=height))i=frames.erase(i);else ++i;
        }
    }
    bool ready(unsigned maximum_age=1) const noexcept {return valid&&age<maximum_age&&!frames.empty()&&!frames.back().reused;}
    unsigned history_age()const noexcept{return age;}
    bool capture(ID3D12GraphicsCommandList* c,ID3D12Resource* color,ID3D12Resource* neural,ID3D12Resource* depth,bool carried=false) {
        collect(); invalidate();
        Ptr<ID3D12Device> incoming;if(FAILED(c->GetDevice(IID_PPV_ARGS(&incoming))))return false;
        if(device&&device!=incoming)return false;device=incoming;
        const auto desc=color->GetDesc();width=static_cast<unsigned>(desc.Width);height=desc.Height;
        Frame frame;
        for(auto i=frames.begin();i!=frames.end();++i){
            if(i!=std::prev(frames.end()) && i->uses.empty() && i->color->GetDesc().Width==width && i->color->GetDesc().Height==height){
                frame=std::move(*i);frames.erase(i);frame.heaps.clear();frame.reused=false;break;
            }
        }
        if(!frame.color&&frames.size()>=12)return false;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(auto pair:{std::pair{color,std::addressof(frame.color)},std::pair{neural,std::addressof(frame.neural)},std::pair{depth,std::addressof(frame.depth)}}){
            auto d=pair.first->GetDesc();d.Flags=D3D12_RESOURCE_FLAG_NONE;
            if(!*pair.second&&FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(pair.second->ReleaseAndGetAddressOf()))))return false;
        }
        if(!frame.uses.record(c))return false;
        for(auto pair:{std::pair{color,frame.color.Get()},std::pair{neural,frame.neural.Get()},std::pair{depth,frame.depth.Get()}}){
            const auto original=pair.first==neural?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            transition(c,pair.first,original,D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(c,pair.second,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            c->CopyResource(pair.second,pair.first);
            transition(c,pair.second,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(c,pair.first,D3D12_RESOURCE_STATE_COPY_SOURCE,original);
        }
        frames.push_back(std::move(frame));age=carried?age+1:0;valid=true;return true;
    }
    bool apply(ID3D12GraphicsCommandList* c,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* out) {
        if(!ready(3)||color->GetDesc().Width!=width||color->GetDesc().Height!=height)return false;
        if(!pipeline){
            D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,6,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,6}};
            D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};
            params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,4};
            D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;Ptr<ID3DBlob> blob,error;
            if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
            if(FAILED(D3DCompile(shader,strlen(shader),"CheekyNR edit reuse",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))return false;
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
            if(FAILED(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline))))return false;
        }
        auto& frame=frames.back();Ptr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,7,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))return false;
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(hd.Type);
        for(auto* r:{color,depth,motion,frame.color.Get(),frame.neural.Get(),frame.depth.Get()}){
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=r->GetDesc().Format;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(r,&sd,cpu);cpu.ptr+=step;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=out->GetDesc().Format;ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(out,nullptr,&ud,cpu);
        if(!frame.uses.record(c))return false;
        // Retain descriptors through reset/destruction AND completion of all executions.
        frame.heaps.push_back(heap);
        c->SetDescriptorHeaps(1,heap.GetAddressOf());c->SetComputeRootSignature(root.Get());c->SetPipelineState(pipeline.Get());c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
        const unsigned constants[]{width,height,0,0};c->SetComputeRoot32BitConstants(1,4,constants,0);c->Dispatch((width+7)/8,(height+7)/8,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=out;c->ResourceBarrier(1,&barrier);
        frame.reused=true;return true;
    }
};
}
