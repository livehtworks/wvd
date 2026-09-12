#include "../../core/device.hpp"
J real_device(const fs::path& config,const fs::path& out,Events& events){
 std::ifstream input(config);J cfg;input>>cfg;
 const auto adb=cfg.at("adb").get<std::string>(),serial=cfg.at("serial").get<std::string>();
 const auto method=cfg.value("screencap_method",MaaAdbScreencapMethod_Encode);
 MaaController* c=MaaAdbControllerCreate(adb.c_str(),serial.c_str(),method,MaaAdbInputMethod_AdbShell,cfg.value("config",J::object()).dump().c_str(),cfg.value("agent",std::string{}).c_str());
 ensure(c,"native adb create");
 struct Guard{MaaController* c;~Guard(){MaaControllerDestroy(c);}}guard{c};
 bool raw=true;ensure(MaaControllerSetOption(c,MaaCtrlOption_ScreenshotUseRawSize,&raw,sizeof(raw)),"real raw option");
 auto id=MaaControllerPostConnection(c);auto status=MaaControllerWait(c,id);
 events.emit("device.connect",{{"controller_action_id",id},{"status",status},{"serial",serial},{"screencap_method",method}});
 ensure(status==MaaStatus_Succeeded,"real connect failed");
 if(cfg.contains("start_intent")){
  auto intent=cfg.at("start_intent").get<std::string>();
  ensure(intent.starts_with("jp.co.drecom.wizardry.daphne/")||intent.starts_with("com.github.metacubex.clash.meta/")||intent.starts_with("com.android.settings/"),"unapproved app");
  auto cid=MaaControllerPostStartApp(c,intent.c_str());auto cs=MaaControllerWait(c,cid);events.emit("device.start_app",{{"intent",intent},{"status",cs},{"controller_action_id",cid}});ensure(cs==MaaStatus_Succeeded,"app launch failed");std::this_thread::sleep_for(1500ms);
 }
 if(cfg.value("stop_game",false)){
  auto cid=MaaControllerPostStopApp(c,"jp.co.drecom.wizardry.daphne");auto cs=MaaControllerWait(c,cid);events.emit("device.stop_game",{{"status",cs},{"controller_action_id",cid}});ensure(cs==MaaStatus_Succeeded,"game stop failed");
 }
 if(cfg.contains("boot_bundle")){
  MaaResource* resource=MaaResourceCreate();ensure(resource,"boot resource");
  struct RG{MaaResource* r;~RG(){MaaResourceDestroy(r);}}rg{resource};
  ensure(MaaResourceWait(resource,MaaResourcePostBundle(resource,cfg.at("boot_bundle").get<std::string>().c_str()))==MaaStatus_Succeeded,"boot bundle");
  MaaTasker* tasker=MaaTaskerCreate();ensure(tasker,"boot tasker");
  struct TG{MaaTasker* t;~TG(){MaaTaskerDestroy(t);}}tg{tasker};
  MaaTaskerBindResource(tasker,resource);MaaTaskerBindController(tasker,c);
  MaaTaskerAddContextSink(tasker,[](void*,const char* m,const char* d,void* a) noexcept {try{static_cast<Events*>(a)->emit(m,J::parse(d));}catch(...){}},&events);
  const std::vector<std::string> candidates={"CityReady","Attention","Download","Title"};
  J pipeline={{"Boot",{{"next",candidates},{"timeout",120000},{"rate_limit",1000}}},
    {"CityReady",{{"recognition","TemplateMatch"},{"template","Inn.png"},{"threshold",0.8},{"pre_delay",0},{"post_delay",0}}},
    {"Attention",{{"recognition","TemplateMatch"},{"template","boot_attention.png"},{"threshold",0.86},{"roi",{250,430,420,220}},{"action","Click"},{"target",{450,1450,1,1}},{"next",candidates},{"post_delay",1500}}},
    {"Download",{{"recognition","TemplateMatch"},{"template","startdownload.png"},{"threshold",0.8},{"roi",{222,901,465,84}},{"action","Click"},{"target",{450,940,1,1}},{"next",candidates},{"post_delay",1500}}},
    {"Title",{{"recognition","TemplateMatch"},{"template","boot_title_logo.png"},{"threshold",0.86},{"roi",{100,300,700,470}},{"action","Click"},{"target",{450,1450,1,1}},{"next",candidates},{"post_delay",3000}}}};
  for(auto& node:pipeline){node["timeout"]=120000;node["rate_limit"]=1000;}
  auto id=MaaTaskerPostTask(tasker,"Boot",pipeline.dump().c_str());auto deadline=Clock::now()+120s;
  while(Clock::now()<deadline){auto current=MaaTaskerStatus(tasker,id);if(current!=MaaStatus_Pending&&current!=MaaStatus_Running)break;std::this_thread::sleep_for(50ms);}
  bool timeout=Clock::now()>=deadline;
  if(timeout)MaaTaskerWait(tasker,MaaTaskerPostStop(tasker));
  auto ts=MaaTaskerWait(tasker,id);MaaNodeId city=MaaInvalidId;
  bool city_found=MaaTaskerGetLatestNode(tasker,"CityReady",&city);
  events.emit("boot.result",{{"timeout",timeout},{"framework_status",ts},{"city_node",city},{"city_found",city_found}});
  ensure(!timeout&&ts==MaaStatus_Succeeded&&city_found,"boot did not reach city");
 }
 int success=0,attempts=cfg.value("samples",1),warmup=cfg.value("warmups",0);J samples=J::array();
 auto capture=[&](std::string label){
  auto t=Clock::now();auto cid=MaaControllerPostScreencap(c);auto cs=MaaControllerWait(c,cid);Image img;
  bool captured=cs==MaaStatus_Succeeded&&MaaControllerCachedImage(c,img.p);
  int width=MaaImageBufferWidth(img.p),height=MaaImageBufferHeight(img.p);
  auto expected=cfg.value("expected_size",std::vector<int>{900,1600});
  bool valid=captured&&width==expected.at(0)&&height==expected.at(1);
  J row={{"label",label},{"status",cs},{"valid",valid},{"width",width},{"height",height},{"controller_action_id",cid},{"elapsed_ms",std::chrono::duration<double,std::milli>(Clock::now()-t).count()},{"requested_backend",method}};
  if(captured){auto data=MaaImageBufferGetEncoded(img.p);auto n=MaaImageBufferGetEncodedSize(img.p);ensure(data&&n>0,"encode capture");std::ofstream f(out/(label+".png"),std::ios::binary);f.write(reinterpret_cast<const char*>(data),n);}
  events.emit("device.capture",row);samples.push_back(row);return valid;
 };
 // 配置只接受明确的安全系统导航参数；没有游戏任务调度或消费入口。
 if(cfg.contains("click")){
  ensure(capture("before"),"before screenshot");auto xy=cfg.at("click");int x=xy.at(0),y=xy.at(1);auto dims=cfg.value("expected_size",std::vector<int>{900,1600});ensure(x>=0&&x<dims.at(0)&&y>=0&&y<dims.at(1),"click bounds");
  auto cid=MaaControllerPostClick(c,x,y);auto cs=MaaControllerWait(c,cid);events.emit("device.click",{{"x",x},{"y",y},{"status",cs},{"controller_action_id",cid}});ensure(cs==MaaStatus_Succeeded,"click failed");std::this_thread::sleep_for(800ms);ensure(capture("after"),"after screenshot");
 }
 for(int i=0;i<warmup;++i)capture("warmup-"+std::to_string(i));
 for(int i=0;i<attempts;++i)if(capture("sample-"+std::to_string(i)))++success;
 return {{"pass",success==attempts},{"attempts",attempts},{"valid",success},{"warmups",warmup},{"samples",samples}};
}
