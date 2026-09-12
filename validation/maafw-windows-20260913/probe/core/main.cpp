#include "probe.hpp"
#include "device.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
static std::atomic<int> owners=0;
static MaaBool legacy_match(MaaContext* ctx,MaaTaskId,const char*,const char*,const char* params,const MaaImageBuffer* image,const MaaRect*,void* arg,MaaRect* box,MaaStringBuffer* detail) noexcept {
 auto& s=*static_cast<State*>(arg);
 try {
  auto p=J::parse(params);auto id=MaaContextRunRecognitionDirect(ctx,"TemplateMatch",p.dump().c_str(),image);
  MaaBool hit=false;MaaRect found{};String name,algorithm,data;
  ensure(MaaTaskerGetRecognitionDetail(MaaContextGetTasker(ctx),id,name.p,algorithm.p,&hit,&found,data.p,nullptr,nullptr),"nested template detail");
  s.events->emit("legacy.native_match",{{"recognition_id",id},{"hit",bool(hit)},{"box",{found.x,found.y,found.width,found.height}},{"detail",J::parse(data.str())}});
  if(!hit)return false;
  // WVD 返回模板中心；用 1px 框消除框架在整个模板内随机点击的偏移。
  *box={found.x+found.width/2,found.y+found.height/2,1,1};MaaStringBufferSet(detail,data.str().c_str());return true;
 }catch(...){s.failed=true;return false;}
}
static MaaBool reco(MaaContext*,MaaTaskId,const char*,const char*,const char*,const MaaImageBuffer*,const MaaRect*,void* arg,MaaRect*,MaaStringBuffer*) noexcept {
 try {auto& s=*static_cast<State*>(arg);s.mark();s.events->emit("recognition.wait_checkpoint");return false;}catch(...){static_cast<State*>(arg)->failed=true;return false;}
}
static MaaBool action(MaaContext* ctx,MaaTaskId task,const char* node,const char*,const char* param,MaaRecoId,const MaaRect*,void* arg) noexcept {
 auto& s=*static_cast<State*>(arg);
 try {
  auto p=J::parse(param);auto op=p.value("op",std::string{});
  s.events->emit("custom.enter",{{"node",node},{"task_id",task},{"operation",op},{"generation",s.generation}});
  if(op=="terminal"){s.terminal=true;return true;}
  if(op=="throw")throw std::runtime_error("intentional callback exception");
  if(op=="cooperate"){
   s.mark();for(int i=0;i<1000;++i){if(s.stop){s.events->emit("custom.stop_seen");return false;}std::this_thread::sleep_for(10ms);}
   return false;
  }
  if(op=="nested"||op=="clone_nested"){
   MaaContext* nested=op=="clone_nested"?MaaContextClone(ctx):ctx;
   ensure(nested!=nullptr,"clone null");
   auto id=MaaContextRunTask(nested,p.value("entry","Child").c_str(),"{}");
   s.events->emit("custom.nested_return",{{"child_task_id",id}});
   return !s.stop&&!s.recovery&&id!=MaaInvalidId;
  }
  if(op=="recovery"){s.recovery=true;s.events->emit("business.recovery_required");return false;}
  if(op=="stale"){
   int expected=p.at("generation");if(expected!=s.generation){s.failed=true;s.events->emit("generation.reject",{{"expected",expected},{"actual",s.generation}});return false;}
   return true;
  }
  if(op=="clone"){
   String original,changed,original_after;
   ensure(MaaContextGetNodeData(ctx,"ProbeData",original.p),"node missing");
   MaaContext* child=MaaContextClone(ctx);ensure(child!=nullptr,"clone missing");
   ensure(MaaContextOverridePipeline(child,R"({"ProbeData":{"roi":[15,25,5,5]}})"),"clone override");
   ensure(MaaContextGetNodeData(child,"ProbeData",changed.p),"clone get");
   ensure(MaaContextGetNodeData(ctx,"ProbeData",original_after.p),"parent get");
   auto a=J::parse(original.str()),b=J::parse(changed.str()),c=J::parse(original_after.str());
   ensure(a==c&&a!=b,"clone override leaked");
   s.events->emit("context.override_verified",{{"parent",a},{"clone",b}});
   return true;
  }
  throw std::runtime_error("unknown custom action");
 }catch(const std::exception& e){s.failed=true;try{s.events->emit("callback.exception",{{"message",e.what()}});}catch(...){}return false;}
 catch(...){s.failed=true;try{s.events->emit("callback.unknown_exception");}catch(...){}return false;}
}
static void sink(void*,const char* message,const char* details,void* arg) noexcept{
 try {static_cast<State*>(arg)->events->emit(message,J::parse(details));}catch(...){static_cast<State*>(arg)->failed=true;}
}
struct Session {
 State state;MaaCustomControllerCallbacks callbacks=fake_callbacks();MaaResource* resource=nullptr;MaaController* controller=nullptr;MaaTasker* tasker=nullptr;
 explicit Session(Events& e,const fs::path& bundle,int generation=1):state(e){
  ++owners;state.generation=generation;
  try{
   resource=MaaResourceCreate();ensure(resource,"resource create");
   ensure(MaaResourceWait(resource,MaaResourcePostBundle(resource,utf8path(bundle).c_str()))==MaaStatus_Succeeded,"bundle load");
   ensure(MaaResourceRegisterCustomAction(resource,"Probe",action,&state),"register action");
   ensure(MaaResourceRegisterCustomRecognition(resource,"Never",reco,&state),"register recognition");
   ensure(MaaResourceRegisterCustomRecognition(resource,"WvdBase",legacy_match,&state),"register WVD recognition");
   controller=MaaCustomControllerCreate(&callbacks,&state);ensure(controller,"controller create");
   bool raw=true;ensure(MaaControllerSetOption(controller,MaaCtrlOption_ScreenshotUseRawSize,&raw,sizeof(raw)),"raw option");
   ensure(MaaControllerWait(controller,MaaControllerPostConnection(controller))==MaaStatus_Succeeded,"fake connection");
   tasker=MaaTaskerCreate();ensure(tasker,"tasker create");
   ensure(MaaTaskerBindResource(tasker,resource)&&MaaTaskerBindController(tasker,controller)&&MaaTaskerInited(tasker),"tasker bind");
   MaaTaskerAddSink(tasker,sink,&state);MaaTaskerAddContextSink(tasker,sink,&state);
  }catch(...){clear();throw;}
 }
 void clear(){
  // 回调持有 State：先等待任务完全退出，再销毁任务、控制器、资源，最后释放 State。
  if(tasker){if(MaaTaskerRunning(tasker)){state.stop=true;MaaTaskerWait(tasker,MaaTaskerPostStop(tasker));}MaaTaskerDestroy(tasker);tasker=nullptr;}
  if(controller){MaaControllerDestroy(controller);controller=nullptr;}
  if(resource){MaaResourceDestroy(resource);resource=nullptr;}
  --owners;
 }
 ~Session(){clear();}
 Session(const Session&)=delete;
};
static J custom(std::string op){return {{"action","Custom"},{"custom_action","Probe"},{"custom_action_param",{{"op",op}}}};}
static J pipeline(){
 J p=J::object();
 p["Terminal"]=custom("terminal");
 p["Start"]={{"next",{"Click"}}};
 p["Click"]={{"action","Click"},{"target",{120,240,1,1}},{"next",{"Terminal"}}};
 p["Wait"]={{"next",{"Never"}},{"timeout",-1}};
 p["Never"]={{"recognition","Custom"},{"custom_recognition","Never"},{"next",{"Click"}}};
 p["Cooperate"]=custom("cooperate");p["Cooperate"]["next"]={"Click"};
 p["Nested"]=custom("nested");p["Nested"]["next"]={"Click"};
 p["Child"]=custom("cooperate");p["Child"]["next"]={"Click"};
 p["CloneNested"]=custom("clone_nested");p["CloneNested"]["next"]={"Click"};
 p["Throw"]=custom("throw");p["Throw"]["next"]={"Terminal"};
 p["RecoveryRoot"]=custom("nested");p["RecoveryRoot"]["custom_action_param"]["entry"]="RecoveryChild";p["RecoveryRoot"]["next"]={"Click"};
 p["RecoveryChild"]=custom("recovery");p["RecoveryChild"]["next"]={"Click"};
 p["Stale"]=custom("stale");p["Stale"]["custom_action_param"]["generation"]=1;p["Stale"]["next"]={"Click"};
 p["Clone"]=custom("clone");p["Clone"]["next"]={"Terminal"};
 p["ProbeData"]={{"roi",{1,2,3,4}}};
 p["InterruptRoot"]={{"next",{"[JumpBack]Interrupt","Terminal"}}};
 p["Interrupt"]={{"max_hit",1},{"action","Click"},{"target",{90,100,1,1}}};
 for(auto& n:p){n["pre_delay"]=0;n["post_delay"]=0;n["rate_limit"]=10;if(!n.contains("timeout"))n["timeout"]=1000;}
 return p;
}
static std::string terminal(State& s,MaaStatus framework){
 if(s.stop)return "UserStopped";
 if(s.recovery)return "RecoveryRequired";
 if(s.failed||framework!=MaaStatus_Succeeded)return "Failed";
 return s.terminal?"Completed":"Failed";
}
static J run_case(Session& s,std::string entry,bool stop=false){
 const auto begin=Clock::now();auto id=MaaTaskerPostTask(s.tasker,entry.c_str(),"{}");ensure(id!=MaaInvalidId,"post task");
 std::thread stopper;
 std::atomic<bool> reached=false;double stop_ms=0;
 if(stop)stopper=std::thread([&]{
  std::unique_lock lock(s.state.mutex);
  reached=s.state.cv.wait_for(lock,5s,[&]{return s.state.checkpoint.load();});
  auto t=Clock::now();s.state.events->emit("stop.request",{{"checkpoint_reached",reached.load()}});
  s.state.stop=true;auto sid=MaaTaskerPostStop(s.tasker);MaaTaskerWait(s.tasker,sid);
  stop_ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();s.state.events->emit("stop.confirmed",{{"elapsed_ms",stop_ms},{"inputs",s.state.inputs.load()}});
 });
 auto status=MaaTaskerWait(s.tasker,id);
 if(stopper.joinable())stopper.join();
 auto business=terminal(s.state,status);
 J result={{"framework_status",status},{"business_terminal",business},{"task_id",id},{"inputs",s.state.inputs.load()},
 {"inputs_after_stop_request",s.state.after_stop.load()},{"stop_ms",stop_ms},{"checkpoint_reached",reached.load()},
 {"elapsed_ms",std::chrono::duration<double,std::milli>(Clock::now()-begin).count()}};
 s.state.events->emit("task.result",result);return result;
}
static J history(Session& s,int count){
 J p=J::object();
 for(int i=0;i<count;++i){
  std::string name="H"+std::to_string(i);
  p[name]={{"pre_delay",0},{"post_delay",0},{"rate_limit",0},{"timeout",1000}};
  if(i+1<count)p[name]["next"]={"H"+std::to_string(i+1)};
  else {p[name]=custom("terminal");p[name]["pre_delay"]=0;p[name]["post_delay"]=0;}
 }
 ensure(MaaResourceOverridePipeline(s.resource,p.dump().c_str()),"history pipeline");
 auto r=run_case(s,"H0");MaaSize n=0;MaaStatus status=0;String entry;
 ensure(MaaTaskerGetTaskDetail(s.tasker,r["task_id"].get<MaaTaskId>(),entry.p,nullptr,&n,&status),"history size");
 std::vector<MaaNodeId> ids(n);
 ensure(MaaTaskerGetTaskDetail(s.tasker,r["task_id"].get<MaaTaskId>(),entry.p,ids.data(),&n,&status),"history query");
 int readable=0;for(auto id:ids){String name;MaaRecoId reco_id=0;MaaActId act_id=0;MaaBool done=false;if(MaaTaskerGetNodeDetail(s.tasker,id,name.p,&reco_id,&act_id,&done)&&done)++readable;}
 r["node_count"]=n;r["readable_nodes"]=readable;r["runtime_cache_object_count"]="NOT_OBSERVABLE_BY_PUBLIC_API";r["async_status_map_count"]="NOT_OBSERVABLE_BY_PUBLIC_API";
 ensure(n==count&&readable==count,"history not fully queryable");
 return r;
}
static J offline(const std::string& name,const fs::path& bundle,Events& e,const fs::path& out){
 if(name=="P4-PACK-ISOLATION"){
  Session a(e,bundle.parent_path()/"pack-a"),b(e,bundle.parent_path()/"pack-b");
  auto load=[&](Session& s,const fs::path& dir){
   std::ifstream cf(dir/"image_case.json");J cfg;cf>>cfg;std::ifstream in(fs::u8path(cfg.at("frame").get<std::string>()),std::ios::binary);
   std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{});Image img;ensure(MaaImageBufferSetEncoded(img.p,bytes.data(),bytes.size()),"pack decode");
   auto raw=static_cast<const unsigned char*>(MaaImageBufferGetRawData(img.p));s.state.pixels.assign(raw,raw+900*1600*3);
   J node={{"recognition","Custom"},{"custom_recognition","WvdBase"},{"custom_recognition_param",cfg.at("recognition")},{"action","Click"},{"next",{"Terminal"}},{"pre_delay",0},{"post_delay",0}};
   ensure(MaaResourceOverridePipeline(s.resource,J{{"SameNode",node}}.dump().c_str()),"pack pipeline");
  };
  load(a,bundle.parent_path()/"pack-a");load(b,bundle.parent_path()/"pack-b");
  auto ar=run_case(a,"SameNode"),br=run_case(b,"SameNode");
  bool pass=ar["business_terminal"]=="Completed"&&br["business_terminal"]=="Completed"&&a.state.last_x==220&&a.state.last_y==315&&b.state.last_x==670&&b.state.last_y==865;
  e.emit("pack.isolation",{{"pack_a_xy",{a.state.last_x.load(),a.state.last_y.load()}},{"pack_b_xy",{b.state.last_x.load(),b.state.last_y.load()}},{"same_node","SameNode"},{"same_image","target.png"}});
  return {{"pass",pass},{"a",ar},{"b",br}};
 }
 if(name=="IMAGE"){
  std::ifstream f(bundle/"image_case.json");J cfg;f>>cfg;
  Session s(e,bundle);
  auto framepath=fs::u8path(cfg.at("frame").get<std::string>());std::ifstream imagefile(framepath,std::ios::binary);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(imagefile)),{});Image img;
  ensure(MaaImageBufferSetEncoded(img.p,bytes.data(),bytes.size()),"decode fixture");
  int w=MaaImageBufferWidth(img.p),h=MaaImageBufferHeight(img.p);
  if(w*16!=h*9){e.emit("frame.aspect_rejected",{{"width",w},{"height",h}});return {{"pass",cfg.value("reject_aspect",false)&&s.state.inputs==0},{"inputs",s.state.inputs.load()},{"rejected",true}};}
  s.state.width=w;s.state.height=h;auto raw=static_cast<const unsigned char*>(MaaImageBufferGetRawData(img.p));s.state.pixels.assign(raw,raw+w*h*3);
  bool rawsize=false;int32_t shortside=900;
  ensure(MaaControllerSetOption(s.controller,MaaCtrlOption_ScreenshotUseRawSize,&rawsize,sizeof(rawsize)),"disable raw");
  ensure(MaaControllerSetOption(s.controller,MaaCtrlOption_ScreenshotTargetShortSide,&shortside,sizeof(shortside)),"900 base");
  J node={{"recognition","Custom"},{"custom_recognition","WvdBase"},{"custom_recognition_param",cfg.at("recognition")},{"action","Click"},{"next",{"Terminal"}},{"pre_delay",0},{"post_delay",0}};
  if(cfg.value("ocr",false)){node={{"recognition","OCR"},{"expected",cfg.at("expected_text")},{"roi",cfg.at("roi")},{"next",{"Terminal"}},{"pre_delay",0},{"post_delay",0}};}
  if(!cfg.value("click",false))node["action"]="DoNothing";
  ensure(MaaResourceOverridePipeline(s.resource,J{{"ImageProbe",node}}.dump().c_str()),"image pipeline");
  auto result=run_case(s,"ImageProbe");bool hit=result["business_terminal"]=="Completed";
  bool pass=hit==cfg.at("expected_hit").get<bool>();
  if(cfg.contains("expected_device")){auto pos=cfg.at("expected_device");pass=pass&&std::abs(s.state.last_x.load()-pos[0].get<int>())<=1&&std::abs(s.state.last_y.load()-pos[1].get<int>())<=1;}
  result["pass"]=pass;result["hit"]=hit;result["device_x"]=s.state.last_x.load();result["device_y"]=s.state.last_y.load();result["frame_width"]=w;result["frame_height"]=h;
  return result;
 }
 if(name=="P4-LIFETIME"){
  J samples=J::array();
  for(int i=-5;i<30;++i){
   {Session s(e,bundle);history(s,100);}
   std::this_thread::sleep_for(250ms);
   auto m=platform::memory();m["round"]=i;m["own_sessions_alive"]=owners.load();samples.push_back(m);
   std::ofstream f(out/"memory_samples.jsonl",std::ios::app);f<<m.dump()<<'\n';
  }
  auto median=[&](int from,int to,std::string key){std::vector<double> v;for(int i=from;i<to;++i)v.push_back(samples[i+5][key].get<double>());std::sort(v.begin(),v.end());return (v[4]+v[5])/2;};
  double delta=median(20,30,"private_bytes")-median(0,10,"private_bytes");
  double sy=0,sxy=0;for(int i=0;i<30;++i){double y=samples[i+5]["private_bytes"];sy+=y;sxy+=i*y;}
  double slope=(30*sxy-435*sy)/(30*8555.0-435*435);
  double hd=median(20,30,"handles")-median(0,10,"handles"),td=median(20,30,"threads")-median(0,10,"threads");
  return {{"pass",delta<=32*1024*1024&&slope<=256*1024&&hd<=8&&td<=2&&owners==0},{"private_median_delta",delta},{"private_slope_bytes_per_round",slope},{"handle_delta",hd},{"thread_delta",td},{"rounds",30},{"warmups",5}};
 }
 if(name=="P3-RESTART"){
  J first;{Session s(e,bundle,1);first=run_case(s,"RecoveryRoot");ensure(first["business_terminal"]=="RecoveryRequired"&&s.state.inputs==0,"recovery unwind");ensure(!MaaTaskerRunning(s.tasker),"recovery not idle");}
  ensure(owners==0,"old session alive");e.emit("session.recreated",{{"generation",2}});
  Session fresh(e,bundle,2);auto second=run_case(fresh,"Start");
  return {{"pass",second["business_terminal"]=="Completed"&&fresh.state.inputs==1},{"old",first},{"new",second}};
 }
 Session s(e,bundle,name=="P3-STALE"?2:1);
 J r;bool pass=false;
 if(name=="E01-NATIVE"||name=="E01-UNICODE"||name=="P2-NORMAL"){
  r=run_case(s,"Start");pass=r["business_terminal"]=="Completed"&&s.state.inputs==1;
 }else if(name=="P2-STOP-WAIT"||name=="P2-STOP-CUSTOM"||name=="P2-STOP-NESTED"||name=="P4-CLONE-STOP"){
  std::string entry=name=="P2-STOP-WAIT"?"Wait":name=="P2-STOP-CUSTOM"?"Cooperate":name=="P4-CLONE-STOP"?"CloneNested":"Nested";
  r=run_case(s,entry,true);pass=r["checkpoint_reached"]==true&&r["business_terminal"]=="UserStopped"&&r["stop_ms"].get<double>()<1000&&s.state.inputs==0&&s.state.after_stop==0;
 }else if(name=="P2-FAIL"){
  s.state.fail_click=true;r=run_case(s,"Start");pass=r["business_terminal"]=="Failed"&&r["framework_status"]==MaaStatus_Failed&&s.state.inputs==1&&!s.state.terminal;
 }else if(name=="P2-EXCEPTION"){
  r=run_case(s,"Throw");pass=r["business_terminal"]=="Failed"&&r["framework_status"]==MaaStatus_Failed&&!s.state.terminal;
 }else if(name=="P3-INTERRUPT"){
  r=run_case(s,"InterruptRoot");pass=r["business_terminal"]=="Completed"&&s.state.inputs==1;
 }else if(name=="P3-STALE"){
  r=run_case(s,"Stale");pass=r["business_terminal"]=="Failed"&&s.state.inputs==0;
 }else if(name=="P4-CONTEXT"){
  r=run_case(s,"Clone");pass=r["business_terminal"]=="Completed";
 }else if(name=="P4-HISTORY"){
  r=history(s,1000);pass=r["business_terminal"]=="Completed";
 }else throw std::runtime_error("unknown case");
 r["pass"]=pass;return r;
}
int main(int argc,char** argv){
 try{
  if(argc!=4)throw std::runtime_error("usage: probe CASE bundle output");
  std::string name=argv[1];fs::path bundle=fs::u8path(argv[2]),out=fs::u8path(argv[3]);fs::create_directories(out);
  Events events(out/"events.jsonl",name);
  auto log=utf8path(out/"maa");MaaGlobalSetOption(MaaGlobalOption_LogDir,log.data(),log.size());int32_t level=2;MaaGlobalSetOption(MaaGlobalOption_StdoutLevel,&level,sizeof(level));
  auto env=platform::identity();env["maa_version"]=MaaVersion();write_json(out/"environment.private.json",env);events.emit("process.start",env);
  ensure(std::string(MaaVersion())=="v5.13.0"||std::string(MaaVersion())=="5.13.0","wrong Maa version");
  if(name!="DEVICE"&&!fs::exists(bundle/"pipeline"/"probe.json")){
   fs::create_directories(bundle/"pipeline");
   write_json(bundle/"pipeline"/"probe.json",pipeline());
   write_json(bundle/"default_pipeline.json",{{"Default",{{"pre_delay",0},{"post_delay",0},{"rate_limit",10},{"timeout",1000}}}});
  }
  J r=name=="DEVICE"?real_device(bundle,out,events):offline(name,bundle,events,out);r["case"]=name;r["status"]=r["pass"]==true?"PASS":"FAIL";r["own_sessions_alive"]=owners.load();
  ensure(owners==0,"session leaked");
  write_json(out/"case_results.json",r);std::cout<<r.dump()<<std::endl;return r["pass"]==true?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;if(argc==4)try{write_json(fs::u8path(argv[3])/"case_results.json",{{"case",argv[1]},{"status","FAIL"},{"reason",e.what()}});}catch(...){}return 2;}
 catch(...){return 3;}
}
