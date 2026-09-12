#include "../../core/probe.hpp"
namespace {
template<class F> MaaBool guard(void* arg,F f) noexcept {try{return f(*static_cast<State*>(arg));}catch(...){static_cast<State*>(arg)->failed=true;return false;}}
MaaBool connect(void* a){return guard(a,[](State& s){s.events->emit("fake.connect");return true;});}
MaaBool yes(void*){return true;}
MaaBool no(void*){return false;}
MaaBool uuid(void* a,MaaStringBuffer* b){return guard(a,[&](State&){return bool(MaaStringBufferSet(b,"offline-probe"));});}
MaaControllerFeature features(void*){return MaaControllerFeature_None;}
MaaBool app(const char*,void*){return false;}
MaaBool capture(void* a,MaaImageBuffer* b){return guard(a,[&](State& s){++s.captures;return bool(MaaImageBufferSetRawData(b,s.pixels.data(),s.width,s.height,16));});}
MaaBool click(int32_t x,int32_t y,void* a){return guard(a,[&](State& s){
 if(s.stop||s.recovery){s.events->emit("input.rejected_terminal");return false;}
 ++s.inputs;s.last_x=x;s.last_y=y;if(s.stop)++s.after_stop;
 s.events->emit("fake.input",{{"x",x},{"y",y},{"generation",s.generation},{"after_stop_request",s.stop.load()}});
 if(s.fail_click){s.failed=true;return false;}return true;
});}
MaaBool swipe(int32_t,int32_t,int32_t,int32_t,int32_t,void*){return false;}
MaaBool touch(int32_t,int32_t,int32_t,int32_t,void*){return false;}
MaaBool key(int32_t,void*){return false;}
MaaBool text(const char*,void*){return false;}
MaaBool move(int32_t,int32_t,void*){return false;}
MaaBool shell(const char*,int64_t,void*,MaaStringBuffer*){return false;}
MaaBool info(void* a,MaaStringBuffer* b){return guard(a,[&](State&){return bool(MaaStringBufferSet(b,"{}"));});}
}
MaaCustomControllerCallbacks fake_callbacks(){
 return {connect,yes,uuid,features,app,app,capture,click,swipe,touch,touch,key,key,text,key,key,move,move,shell,no,info};
}
