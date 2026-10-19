#pragma once
#include "dlss_nr_lifetime.hpp"
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
namespace cheeky::foveated_dlss {
// Timestamp reads are asynchronous, with the SAME recording/reset/fence rules
// as NR resources. CPU wall time is never used as a GPU-cost estimate.
class NrGpuBudget {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    struct Sample {Ptr<ID3D12QueryHeap> queries;Ptr<ID3D12Resource> readback;NrLifetime uses;bool pending{},model{},valid{};};
private:
    std::array<Sample,8> samples;
    double model_ms{},reuse_ms{};
    unsigned model_count{},reuse_count{};
    ULONGLONG model_updated{},reuse_updated{};
public:
    void collect() {
        for(auto& s:samples)if(s.pending){
            const auto frequency=s.uses.timestamp_frequency();s.uses.collect();
            if(!s.uses.empty())continue;
            s.pending=false;if(!frequency||!s.valid)continue;
            const UINT64* ticks{};D3D12_RANGE read{0,16};
            if(FAILED(s.readback->Map(0,&read,reinterpret_cast<void**>(const_cast<UINT64**>(&ticks)))))continue;
            const auto start=ticks[0],end=ticks[1];D3D12_RANGE written{0,0};s.readback->Unmap(0,&written);
            const double ms=end>=start?1000.*double(end-start)/double(frequency):-1.;
            if(!(ms>0&&ms<1000&&std::isfinite(ms)))continue;
            auto& mean=s.model?model_ms:reuse_ms;auto& count=s.model?model_count:reuse_count;
            mean=count?.85*mean+.15*ms:ms;++count;(s.model?model_updated:reuse_updated)=GetTickCount64();
        }
    }
    Sample* begin(ID3D12GraphicsCommandList* c) {
        collect();Sample* slot{};for(auto& s:samples)if(!s.pending&&s.uses.empty()){slot=&s;break;}if(!slot)return nullptr;
        Ptr<ID3D12Device> d;if(FAILED(c->GetDevice(IID_PPV_ARGS(&d))))return nullptr;
        if(!slot->queries){D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
            if(FAILED(d->CreateQueryHeap(&q,IID_PPV_ARGS(&slot->queries))))return nullptr;
            D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=16;b.Height=1;b.DepthOrArraySize=b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if(FAILED(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&slot->readback)))){slot->queries.Reset();return nullptr;}}
        if(!slot->uses.record(c))return nullptr;c->EndQuery(slot->queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);return slot;
    }
    void end(ID3D12GraphicsCommandList*c,Sample*s,bool model,bool valid=true){if(!s)return;c->EndQuery(s->queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);c->ResolveQueryData(s->queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,s->readback.Get(),0);s->model=model;s->valid=valid;s->pending=true;}
    double model_cost()const{return GetTickCount64()-model_updated<3000?model_ms:0;}
    double reuse_cost()const{return GetTickCount64()-reuse_updated<3000?reuse_ms:0;}
    unsigned valid_samples()const{return model_count+reuse_count;}
};
// Stereo total budget is shared by two eyes and all enabled NR passes.
inline unsigned nr_budget_interval(bool adaptive,double budget_ms,unsigned passes,
    double model_ms,double reuse_ms,unsigned maximum,float gaze_motion) noexcept {
    maximum=std::clamp(maximum,2U,4U);unsigned interval=2;
    if(adaptive&&budget_ms>0&&model_ms>0){
        const double target=budget_ms/(2.*std::max(1U,passes));
        interval=target<=reuse_ms?maximum:static_cast<unsigned>(std::clamp(std::ceil(std::max(0.,model_ms-reuse_ms)/(target-reuse_ms)),1.,double(maximum)));
    }
    if(adaptive&&gaze_motion>.10F)return 1;
    if(adaptive&&gaze_motion>.02F)interval=std::min(interval,2U);
    return std::clamp(interval,1U,maximum);
}
}
