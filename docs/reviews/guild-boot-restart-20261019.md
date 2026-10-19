# 公会主菜单触发启动重启

## 结论

本次是启动入口漏接已有公会主菜单识别，造成主动超时重启，不是素材不清晰或点击失败。没有现场交互试验证明游戏卡死，不能将启动判断的NoHit称为卡死。

上一轮只完成哈肯补给等待修复，未把启动入口漏接一并修正，属于遗漏。初次排查按用户要求保持循环运行，只做隔离原图验证。随后用户授权战斗结束后停止、修复并继续；当前实施及部署结果见下节。

## 事件链

- 正式实例87BA907F-E0A1-4093-B382-EACDB76F33D2/run1/generation1，请求giant-85-harken-handoff-20261019-7959df634026457ab4139c4956c95b64。
- 根入口先运行Boot，任务业务入口必须等Boot就绪，不能直接绕过。
- 起始现场为冒险者公会主菜单。Boot_Ready失配，各启动提示也不成立，反复Boot_Entry → Boot_Poll；重启前没有一次input.attempt，也没有pending输入。
- 事件69：CONTINUOUS_EXCEPTION_TIMEOUT / bound_game.restart，61604ms，来源Boot_Poll；事件70保存重启前frame17/action_epoch0原图，SHA256见next/.local/guild-boot-261019/summary.json。
- 重启后的免责声明输入事件115确认，之后已推进跳轮、公会任务及住宿，当前续跑并未停在旧现场。新段run1随后Completed，累计原15+新1=16/100；失败和旧历史不另加。

## 同版本实际识别结果

使用正式同一候选EXE6108abc4ce9dd4abb6698398476bb459d42d7ff02571571ce64c1404b34b4024，在独立data-root、端口17658上传事件70原图。没有连接设备、执行动作或向正式服务提交识别测试。

| 条件 | 结果 |
| --- | --- |
| boot_ready | NoHit |
| blocking_screen | NoHit |
| guild.menu | Hit，0.994756，原阈值0.8 |
| guild.commissions.page | NoHit |
| guild.bounties.page | NoHit |
| guild.bounty.reveal.close | NoHit |
| city.any | NoHit |

因此能排除“缺主菜单素材”“主菜单分数不够”“阻挡页抢占”作为本次入口失配原因。识别存在与流程引用是两件事，本例明确是后者缺失。

## 源码职责与缺口

- `next/native/games/wvd/vision/boot_probes.hpp:boot_probes`：注释声称支持公会页，实际只列委托列表、悬赏列表、揭示关闭，不含guild.menu。
- `next/native/games/wvd/vision/location_probes.hpp:resource`：原生固定配方表同样未登记guild.menu，不能只向boot_probes填一个尚未登记的map.at键。
- `next/resources/authoring/semantic-assets.json:guild.menu`：繁中复用guild.commissions.entry（公会主菜单“委託”按钮），素材、阈值、较宽搜索区均已存在。本次原图0.994756命中，无需降低阈值、缩ROI、加OCR或新模板。
- `next/native/games/wvd/tasks/bounty_cycle.cpp:inspect_ready_pages/InspectReady`：悬赏业务已经接受guild.menu，之后调用既有开委托/悬赏列表流程；缺口在它之前的Boot，并非公会业务完全没有分支。
- `next/native/games/wvd/recovery/boot.cpp:ready_to_resume_task/attach_root_recovery`：就绪条件依赖boot_ready，根入口强制先Boot；失败时无法进入本来会接主菜单的业务。
- `next/native/runtime/flow_executor.cpp:FlowExecutor::tick`：连续异常到期后以新帧重查原已声明候选，仍NoHit才请求重启。重查无法补出一个根本未声明的主菜单候选；61.604秒不是冻结测量。
- 旧`src/script.py:WaitGameBootReady`同样没有把guildRequest列入稳定列表，且有未知页盲点兜底；不能冒称照搬旧实现即可正确承接本例，也不能恢复盲点作为修法。

修复已登记并复用已有guild.menu，接入共用启动就绪候选，正常交回原任务；城市身份检查、救人/Unknown保护和原输入账目保持，不新建第二个公会退出流程、不扩大city.any，也不把任意游戏页都视为就绪。变更仅两处原生配方登记/引用，原图及真实入口验证如下。

## 修复与交付结果

- `location_probes.hpp`登记现有guild.menu；`boot_probes.hpp`纳入同一配方，冷启动、公共清理及恢复入口共用。不改任何ROI、阈值、重试次数、超时或输入时长。
- 真实原图/Service/NativeInputGate/FlowExecutor回放：冷启动2991ms、公共清理2820ms均0输入正常结束；完整巨人启动及生命周期恢复入口分别2851/3170ms进入已有委託开页、各1次正确输入。它们只证明入口推进，不冒充整轮完成。
- 原哈肯无菜单展示图仍NoHit、0输入、不放行，防止用宽泛就绪定义掩盖返城无响应。
- 最终recognition c2的19入口complete=true，包含上述5种原图场景、已有哈肯交接3场景、LV3及其他受影响合同；资源工具6项通过。c1检查实际完成19项，但更正原图来源generation由1为2后触发CONTRACT_SOURCE_CHANGED_DURING_VALIDATION，保留其complete=false，不充作最终同源验收。
- 原build.py辅助函数、package_functional、manage_service构建/冻结/校验/部署完成。新候选next/.local/gb-candidate-261019，EXE6ed7b83dc47f5e876c04e1edcd306d42ca4771fdd3d35466a84a100fae86ca70，输入2c60f869c25c89bccfcdefd5a7b5b2ed033a6954e046fd8aeaed076d2b0e2da7/1315，与最终c2一致。
- 新正式实例EED629D7-2CD2-4299-9BDE-DFD9C93AA9DC/PID45836/start_filetime134368244079173126，端口17654/data-root保持。27份profile/workflows哈希未变，旧候选hk保留，不自动回退，不commit/push，未新增定时任务。
- 原87BA907F的run2在真实战斗结束后、返城再次无进展的既有恢复过程中按本次授权停止，UserStopped/STOP_REQUESTED、quiescent=true、repeat inactive。未交账，不计成功；其死亡观察3118、清除3221、再起观察3247及resurrected3286回执全部保留，没有由代理取消救人或接受死亡。
- 总目标仍100，原43E2B469的run1–15加87BA907F的run1保存Completed共16成功；新请求giant-84-guild-boot-20261019-e229fa1818d54a81948f2f31b3dee475续84，只累计16+新段completed_cycles。2026-10-19 03:21实核新run1保存Completed、result_saved=true、quiescent=true、completed_business_units=3、unresolved_inputs=null；战斗、返城、交账、住宿完成，Terminal事件4517，整轮bound_game.restart事件0。run2已实际Running推进Task_TimeLeap_LeapEn@await，累计17/100，继续原目标剩余83，不另开100。证据next/.local/gbfix-261019/completed-run1-result.json及full-round-confirmation.json；一轮成功不代表100轮完成或独立哈肯停滞根因已解决。

## 返城无菜单停滞边界

这次返城不是同一个“楼层菜单仍在而漏接”的缺陷：87BA907F/run2事件4638按(453,1051)接受归还，原图494/495明确命中归还行；菜单随后消失。frame582仍只有哈肯展示，没有HUD、楼层/郊外/城市或剧情可操作入口，事件4699在60892ms请求原有游戏恢复。原run1同一位置点击在10.285秒后确认，这次等待含原观察暂停后共66.836秒被重启打断。

故障来源未被现有证据归因到官方游戏、网络或输入传输；不能把它写成已修，也不能为避免重启把无菜单图当城市/返城成功。保持原保护与有界恢复，原帧和pending13字段、成功/失败对照保存在next/.local/gbfix-261019/。若再次自然出现，需基于新现场继续区分输入释放、过渡和游戏无响应，而不是增加猜测点击或伪造成功分支。

回滚：先按匹配请求停止并确认静止，再用原manage_service关闭当前身份的服务；以旧hk候选、原17654和原data-root部署。源码前态与权威哈希在next/.local/gbfix-261019/before/及authority-before.json；不通过双服务/双配置自动回退。

## 证据与检查边界

原帧、seq1–133启动事件、7项实际Service响应、最终进度保存于next/.local/guild-boot-261019/。隔离服务8F372EBA-CA43-4622-9AA3-143BDA519252/PID2648已按自身data-root和17658端口正常停止；正式服务PID1668/实例87BA907F保持不变，27份profile/workflows哈希未变。

隔离Start共用候选目录时，原管理器会改写生成的service-launch.json到测试端口；本次已据正式服务真实身份恢复原17654/data-root并读回核对，正式进程未因此中断。该工具副作用单独记录，后续隔离启动不可共享正式候选的生成启动指针。
