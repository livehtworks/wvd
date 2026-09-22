import type { RunState } from "../api/types";
// 引擎终态不是“设备已经释放”。Failed + quiescent=false 必须继续保持忙碌和停止入口。
export function runBusy(run?: RunState): boolean {
  if (!run) return false;
  return run.busy === true || run.submission?.state === "preparing" ||
    ["Preparing", "Running", "Recovering", "StopRequested"].includes(run.state ?? "") ||
    (run.quiescent === false && Boolean(run.run_id));
}
export function displayRunState(run?: RunState): string {
  if (run?.submission?.state === "preparing") return "正在准备启动";
  if (run?.submission?.state === "cancelled" && !runBusy(run)) return "启动准备已取消";
  if (run?.submission?.state === "failed" && !runBusy(run)) return "启动失败";
  if (run?.quiescent === false && run.state === "Failed") return "失败，仍在清理（设备未释放）";
  return run?.state ?? "Idle";
}
