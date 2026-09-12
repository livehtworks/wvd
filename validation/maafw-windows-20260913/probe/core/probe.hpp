#pragma once
#include <MaaFramework/MaaAPI.h>
#include "json.hpp"
#include "platform.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
using J=nlohmann::json;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
struct Events {
 std::mutex mutex;std::ofstream stream;uint64_t seq=0;Clock::time_point start=Clock::now();std::string case_id;
 Events(const fs::path& path,std::string id):stream(path),case_id(std::move(id)) {if(!stream)throw std::runtime_error("event log open");}
 void emit(std::string name,J data=J::object()){
  std::lock_guard lock(mutex);
  J e={{"seq",++seq},{"utc",platform::utc()},{"monotonic_us",std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count()},
  {"case",case_id},{"source",name},{"data",std::move(data)}};
  stream<<e.dump()<<'\n';stream.flush();
 }
};
struct State {
 Events* events;std::atomic<bool> stop=false,checkpoint=false,recovery=false,failed=false,terminal=false;
 std::atomic<int> inputs=0,captures=0,after_stop=0,last_x=-1,last_y=-1;
 int generation=1,width=900,height=1600;bool fail_click=false;std::vector<unsigned char> pixels;
 std::mutex mutex;std::condition_variable cv;
 explicit State(Events& e):events(&e),pixels(900*1600*3,45){}
 void mark(){{std::lock_guard lock(mutex);checkpoint=true;}cv.notify_all();}
};
MaaCustomControllerCallbacks fake_callbacks();
struct Image {
 MaaImageBuffer* p=MaaImageBufferCreate();
 ~Image(){MaaImageBufferDestroy(p);}
 Image()=default;Image(const Image&)=delete;
};
struct String {
 MaaStringBuffer* p=MaaStringBufferCreate();
 ~String(){MaaStringBufferDestroy(p);}
 std::string str()const{return MaaStringBufferGet(p);}
};
inline void ensure(bool good,const char* message){if(!good)throw std::runtime_error(message);}
inline std::string utf8path(const fs::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
inline void write_json(const fs::path& p,const J& j){std::ofstream f(p);ensure(bool(f),"json open");f<<j.dump(2);}
