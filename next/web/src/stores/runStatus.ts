import type { RunState } from "../api/types";
// 引擎终态不是“设备已经释放”。Failed + quiescent=false 必须继续保持忙碌和停止入口。
export function runBusy(run?: RunState): boolean {
  if (!run) return false;
  return run.busy === true || run.repeat?.active === true || run.submission?.state === "preparing" ||
    ["Preparing", "Running", "Recovering", "StopRequested"].includes(run.state ?? "") ||
    (run.quiescent === false && Boolean(run.run_id));
}
export function displayRunState(run?: RunState): string {
  if (run?.repeat?.state === "stopping") return "正在停止循环";
  if (run?.repeat?.active && run.repeat.state === "waiting") return "等待下一轮";
  if (run?.repeat?.active && run.repeat.state === "starting") return "正在启动下一轮";
  if (run?.repeat?.state === "failed") return "循环已因异常停止";
  if (run?.submission?.state === "preparing") return "正在准备启动";
  if (run?.submission?.state === "cancelled" && !runBusy(run)) return "启动准备已取消";
  if (run?.submission?.state === "failed" && !runBusy(run)) return "启动失败";
  if (run?.quiescent === false && run.state === "Failed") return "失败，仍在清理（设备未释放）";
  return run?.state ?? "Idle";
}
