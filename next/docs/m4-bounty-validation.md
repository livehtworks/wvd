# M4 悬赏揭榜与交付

固定旧源码蝎女、蝎女加六手与吉尔共用的公会菜单环节，尚不是这些任务的整条链。
`tasks/bounty_visit`编译两个有限Maa图：揭榜和交付。沿用guild/guildRequest/Bounties、
CompletionReported和600/1400→300/1400横向菜单滑动；未知页不发送滑动。

交付先在WvdRunState记待确认意图；新帧确认CompletionReported消失，随后逐次返回，
确认EdgeOfTown后才增加bounty_reports。揭榜不增加交付次数。两份悬赏各自重新识别、
点击、退出菜单，不把同一页面连点两次当成交付两份。稳定回执防止重复确认计数。
待确认意图跨恢复保留，恢复决策不重启重发可能已生效的交付。

只链接原生业务库和现有离线夹具，不操作游戏、不新增任务ID或生产入口。
初轮 `c7411bb`：状态26方法通过；流程5方法中4通过、1失败，212.521秒。
私有证据 `next/.local/m4-workflow-9wroft0c`。六个实际运行场景的EXE身份、输入和静止均在重建前核对：
单次交付6输入、双次交付12输入、菜单横滑2输入、原按钮不变Failed/停止UserStopped各1输入、未知页Interrupted零输入。

揭榜在设备连接前因 `BUSINESS_UNIT_INVALID` 被拒绝：其图没有任何业务确认，
因此未生成检查点，却配置了WvdRunState。修正加入 `bounty_revealed` 独立回执，
只表示已操作揭榜菜单并回到EdgeOfTown，不声称某个指定悬赏已经接取，也不增加奖励计数。
修正一 `d88a348`：五方法/七场景全部通过（249.348秒，
`m4-bounty-correction1-workflow.log`、`m4-workflow-ex7o57pz`），其中揭榜4输入、
reveals=1/reports=0。七例均在重建前核对EXE身份、输入、无错点与静止。
完整任务分母仍是58，不能因该子流程而关闭整任务。
