// 仅编译/模型检查；不创建 Controller、不连接设备、不发送输入。
#include "games/wvd/tasks/public_flow_library.hpp"
#include "authoring/document_parameters.hpp"
#include "authoring/semantic_assets.hpp"
#include <fstream>
#include <iostream>
using J=nlohmann::json;
using namespace wvd;
static J read(const char* path){std::ifstream f(path);if(!f)throw std::runtime_error("INPUT_MISSING");J j;f>>j;return j;}
int main(int argc,char** argv) {
 try {
  if(argc!=3)throw std::runtime_error("USAGE: test_public_flow <public-flows.json> <semantic-assets.json>");
  auto rows=read(argv[1]);auto resources=read(argv[2]);J docs=J::object();
  for(const auto &d:rows){games::tasks::validate_author_workflow(d);docs[d.at("flow").at("id").get<std::string>()]=d;}
  int checks=0;
  auto check=[&](bool v,const char* code){if(!v)throw std::runtime_error(code);++checks;std::cout<<"PASS "<<code<<'\n';};
  auto rejects=[&](const std::function<void()>& f,const std::string& code){try{f();}catch(const std::exception& e){check(std::string(e.what()).starts_with(code),code.c_str());return;}throw std::runtime_error("EXPECTED_REJECT:"+code);};
  const auto source=docs.at("city-enter-guild");
  const auto d=authoring::instantiate_document(source,{{"location",1}});
  check(d.at("nodes").dump()!=source.at("nodes").dump(),"location_parameter_applied");
  check(source.at("interface").at("parameters").at(0).at("default")==0,"definition_not_mutated");
  auto malformed=source;malformed["revision"]="wrong";
  rejects([&]{games::tasks::validate_author_workflow(malformed);},"AUTHOR_REVISION_INVALID");
  authoring::SemanticAssets assets(resources);
  rejects([&]{assets.condition("city.royal.identity","zh-Hant",authoring::ResourceUse::Position);},"SEMANTIC_OBSERVATION_NOT_POSITIONAL");
  rejects([&]{assets.condition("guild.commissions.entry","zh-Hant");},"SEMANTIC_LOCALE_UNAVAILABLE");
  rejects([&]{assets.condition("guild.bounties.page","en");},"SEMANTIC_LOCALE_UNAVAILABLE");
  const games::tasks::PublicFlowLibrary library(docs,resources);
  const auto compiled=library.compile(docs.at("ore-reset-to-royal"),{},J::object(),"zh-Hant");
  check(!compiled.workflow.nodes.empty(),"real_pipeline_compilation");
  check(compiled.workflow.authoring.at("documents").size()>1,"definition_closure_frozen");
  bool nested=false;for(const auto &path:compiled.source_paths)nested=nested||path.size()>1;
  check(nested,"nested_source_path");
  auto caller=docs.at("ore-reset-to-royal");
  for(auto &n:caller["nodes"])if(n["type"]=="call"){n["parameters"]["flow_id"]="ore-reset-to-royal";break;}
  auto cyclic=docs;cyclic["ore-reset-to-royal"]=caller;
  rejects([&]{games::tasks::PublicFlowLibrary(cyclic,resources).compile(caller,{},J::object(),"zh-Hant");},"FLOW_REFERENCE_CYCLE");
  rejects([&]{library.compile(docs.at("bounty-refresh"),{},J::object(),"zh-Hant");},"SEMANTIC_LOCALE_UNAVAILABLE");
  std::cout<<"TOTAL "<<checks<<"; device_operations=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
